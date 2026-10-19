<#
.SYNOPSIS
    Automated WebAssembly and WebGPU build, size profiling, and local server pipeline for Eatsrealms.
#>

[CmdletBinding()]
param (
    [switch]$Clean,
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [int]$Port = 8080,
    [switch]$NoServe,
    [switch]$NoRun,
    [switch]$NoLaunch,
    [switch]$Serve,
    [switch]$Run,
    [switch]$Detached,
    [switch]$BootstrapEmsdk
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

Write-Host "==========================================================================================" -ForegroundColor Cyan
Write-Host "  Eatsrealms WebAssembly & WebGPU HD-2D Diorama Engine Build & Profiling Script" -ForegroundColor Cyan
Write-Host "==========================================================================================" -ForegroundColor Cyan

function Format-ByteSize([long]$Bytes) {
    if ($Bytes -ge 1GB) {
        return ("{0,7:N2} GB" -f ($Bytes / 1GB))
    } elseif ($Bytes -ge 1MB) {
        return ("{0,7:N2} MB" -f ($Bytes / 1MB))
    } elseif ($Bytes -ge 1KB) {
        return ("{0,7:N2} KB" -f ($Bytes / 1KB))
    } else {
        return ("{0,7:N0} B " -f $Bytes)
    }
}

function Stop-ExistingWebServer {
    param([int]$TargetPort = 8080)
    Write-Host "`n[*] Ensuring existing web servers on port $TargetPort are closed..." -ForegroundColor Yellow
    $found = $false

    try {
        $connections = Get-NetTCPConnection -LocalPort $TargetPort -ErrorAction SilentlyContinue
        if ($connections) {
            $pidsToKill = $connections | Select-Object -ExpandProperty OwningProcess -Unique | Where-Object { $_ -gt 0 -and $_ -ne $PID }
            foreach ($p in $pidsToKill) {
                $pName = (Get-Process -Id $p -ErrorAction SilentlyContinue).ProcessName
                Write-Host "    [!] Terminating existing process on port $TargetPort (PID: $p, Name: $pName)..." -ForegroundColor Yellow
                Stop-Process -Id $p -Force -ErrorAction SilentlyContinue
                $found = $true
            }
        }
    } catch {}

    try {
        $serveProcesses = Get-CimInstance Win32_Process -Filter "CommandLine LIKE '%serve.py%'" -ErrorAction SilentlyContinue
        if ($serveProcesses) {
            foreach ($proc in $serveProcesses) {
                if ($proc.ProcessId -ne $PID) {
                    Write-Host "    [!] Terminating orphaned serve.py server (PID: $($proc.ProcessId))..." -ForegroundColor Yellow
                    Stop-Process -Id $proc.ProcessId -Force -ErrorAction SilentlyContinue
                    $found = $true
                }
            }
        }
    } catch {}

    if (-not $found) {
        Write-Host "    [+] Port $TargetPort is clear. No conflicting servers running." -ForegroundColor Green
    } else {
        Start-Sleep -Milliseconds 300
    }
}

function Show-WebPayloadDetails {
    param (
        [string]$buildDir,
        [string]$Config
    )

    Write-Host "`n==========================================================================================" -ForegroundColor Cyan
    Write-Host "  Eatsrealms WebAssembly & WebGPU Payload Size Report [$Config Target]" -ForegroundColor Cyan
    Write-Host "==========================================================================================" -ForegroundColor Cyan

    $runtimeFiles = @(
        @{
            Name     = "eatsrealms.wasm"
            Role     = "WebAssembly Core Binary"
            Path     = (Join-Path $buildDir "eatsrealms.wasm")
            Category = "Core WebAssembly"
            Required = $true
        },
        @{
            Name     = "eatsrealms.data"
            Role     = "Preloaded Scenes & Assets Archive"
            Path     = (Join-Path $buildDir "eatsrealms.data")
            Category = "Virtual Filesystem"
            Required = $true
        },
        @{
            Name     = "eatsrealms.js"
            Role     = "Emscripten JS Runtime Bridge"
            Path     = (Join-Path $buildDir "eatsrealms.js")
            Category = "JavaScript Runtime"
            Required = $true
        },
        @{
            Name     = "index.html"
            Role     = "WebGPU Diorama Host Shell"
            Path     = (Join-Path $buildDir "index.html")
            Category = "HTML Entrypoint"
            Required = $true
        }
    )

    $devFiles = @(
        @{
            Name     = "libeatsrealms_core.a"
            Role     = "HD-2D World, PropFactory & Foley Archive"
            Path     = (Join-Path $buildDir "libeatsrealms_core.a")
            Category = "Build Static Lib"
        },
        @{
            Name     = "libeats_script.a"
            Role     = "Eatscript VM & Compiler Static Archive"
            Path     = (Join-Path $buildDir "libeats_script.a")
            Category = "Build Static Lib"
        },
        @{
            Name     = "libeatsrealms_render.a"
            Role     = "WebGPU HD-2D Diorama Renderer Archive"
            Path     = (Join-Path $buildDir "libeatsrealms_render.a")
            Category = "Build Static Lib"
        }
    )

    $runtimeTotalBytes = 0
    $wireTotalBytes = 0
    $resolvedRuntime = @()

    foreach ($item in $runtimeFiles) {
        if (Test-Path $item.Path) {
            $fi = Get-Item $item.Path
            $rawBytes = $fi.Length
            $runtimeTotalBytes += $rawBytes

            # Compute Gzip wire transfer size
            $rawBuffer = [System.IO.File]::ReadAllBytes($item.Path)
            $ms = New-Object System.IO.MemoryStream
            $gz = New-Object System.IO.Compression.GZipStream($ms, [System.IO.Compression.CompressionMode]::Compress)
            $gz.Write($rawBuffer, 0, $rawBuffer.Length)
            $gz.Close()
            $wireBytes = $ms.ToArray().Length
            $ms.Close()
            $wireTotalBytes += $wireBytes

            $savingsPct = if ($rawBytes -gt 0) { [math]::Round((1.0 - ($wireBytes / $rawBytes)) * 100.0, 1) } else { 0.0 }

            $resolvedRuntime += [PSCustomObject]@{
                Name       = $item.Name
                Role       = $item.Role
                Category   = $item.Category
                Bytes      = $rawBytes
                WireBytes  = $wireBytes
                SavingsPct = $savingsPct
                Exists     = $true
                Path       = $item.Path
            }
        } else {
            $resolvedRuntime += [PSCustomObject]@{
                Name       = $item.Name
                Role       = $item.Role
                Category   = $item.Category
                Bytes      = 0
                WireBytes  = 0
                SavingsPct = 0.0
                Exists     = $false
                Path       = $item.Path
            }
        }
    }

    Write-Host "`n1. Standalone Web Runtime Distribution (Minimum Required to Ship & Host over CDN):" -ForegroundColor Green
    Write-Host ("{0,-20} {1,-34} {2,12} {3,12} {4,10}" -f "File Name", "Role", "Raw Size", "Wire (Gzip)", "Savings") -ForegroundColor Gray
    Write-Host ("-" * 92) -ForegroundColor DarkGray

    foreach ($f in $resolvedRuntime) {
        if ($f.Exists) {
            $rawFmt = Format-ByteSize $f.Bytes
            $wireFmt = Format-ByteSize $f.WireBytes
            $savingsStr = "-{0:N1}%" -f $f.SavingsPct
            Write-Host ("{0,-20} {1,-34} {2,12} {3,12} {4,10}" -f $f.Name, $f.Role, $rawFmt, $wireFmt, $savingsStr) -ForegroundColor White
        } else {
            Write-Host ("{0,-20} {1,-34} {2,12} {3,12} {4,10}" -f $f.Name, $f.Role, "[MISSING]", "N/A", "0.0%") -ForegroundColor Red
        }
    }
    Write-Host ("-" * 92) -ForegroundColor DarkGray
    $totalRawFmt = Format-ByteSize $runtimeTotalBytes
    $totalWireFmt = Format-ByteSize $wireTotalBytes
    $totalSavings = if ($runtimeTotalBytes -gt 0) { [math]::Round((1.0 - ($wireTotalBytes / $runtimeTotalBytes)) * 100.0, 1) } else { 0.0 }
    $totalSavingsStr = "-{0:N1}%" -f $totalSavings
    Write-Host ("{0,-20} {1,-34} {2,12} {3,12} {4,10}" -f "STANDALONE TOTAL", "Complete WebGPU distribution", $totalRawFmt, $totalWireFmt, $totalSavingsStr) -ForegroundColor Yellow

    Write-Host "`n2. WebAssembly Build & Static Library Archives:" -ForegroundColor Cyan
    Write-Host ("{0,-26} {1,-34} {2,14} {3,12}" -f "File Name", "Role", "Size (Bytes)", "Formatted") -ForegroundColor Gray
    Write-Host ("-" * 90) -ForegroundColor DarkGray

    $devTotalBytes = 0
    foreach ($item in $devFiles) {
        if (Test-Path $item.Path) {
            $fi = Get-Item $item.Path
            $devTotalBytes += $fi.Length
            $fmtSize = Format-ByteSize $fi.Length
            $bytesStr = "{0:N0} B" -f $fi.Length
            Write-Host ("{0,-26} {1,-34} {2,14} {3,12}" -f $item.Name, $item.Role, $bytesStr, $fmtSize) -ForegroundColor White
        }
    }
    Write-Host ("-" * 90) -ForegroundColor DarkGray
    $devTotalFmt = Format-ByteSize $devTotalBytes
    $devTotalBytesStr = "{0:N0} B" -f $devTotalBytes
    Write-Host ("{0,-26} {1,-34} {2,14} {3,12}" -f "DEV ARTIFACTS TOTAL", "Static archives & intermediates", $devTotalBytesStr, $devTotalFmt) -ForegroundColor Cyan

    $wasmEntry = $resolvedRuntime | Where-Object { $_.Name -eq "eatsrealms.wasm" }
    $dataEntry = $resolvedRuntime | Where-Object { $_.Name -eq "eatsrealms.data" }
    $jsEntry   = $resolvedRuntime | Where-Object { $_.Name -eq "eatsrealms.js" }

    $wasmBytes = if ($wasmEntry) { $wasmEntry.Bytes } else { 0 }
    $dataBytes = if ($dataEntry) { $dataEntry.Bytes } else { 0 }
    $jsBytes   = if ($jsEntry) { $jsEntry.Bytes } else { 0 }

    Write-Host "`n3. Size & Web Distribution Highlights:" -ForegroundColor Green
    Write-Host ("  * WebAssembly Engine Binary : {0} ({1:N0} bytes)" -f (Format-ByteSize $wasmBytes), $wasmBytes)
    Write-Host ("  * Virtual Asset Filesystem  : {0} ({1:N0} bytes)" -f (Format-ByteSize $dataBytes), $dataBytes)
    Write-Host ("  * JavaScript Runtime Loader : {0} ({1:N0} bytes)" -f (Format-ByteSize $jsBytes), $jsBytes)
    Write-Host ("  * Total Uncompressed Payload: {0} ({1:N0} bytes)" -f $totalRawFmt, $runtimeTotalBytes)
    Write-Host ("  * Compressed CDN Wire Size  : {0} ({1:N0} bytes) [{2} bandwidth savings]" -f $totalWireFmt, $wireTotalBytes, $totalSavingsStr)
    Write-Host "==========================================================================================`n" -ForegroundColor Cyan
}

# ------------------------------------------------------------------
# STEP 1: Verify & Activate Emscripten Toolchain
# ------------------------------------------------------------------
$emccCmd = Get-Command "emcc" -ErrorAction SilentlyContinue
if (-not $emccCmd) {
    $knownEmsdkRoots = @(
        "C:\git\eatsbits\emsdk",
        "C:\emsdk",
        "$env:USERPROFILE\emsdk",
        (Join-Path $PSScriptRoot "emsdk")
    )
    foreach ($cand in $knownEmsdkRoots) {
        $envScript = Join-Path $cand "emsdk_env.ps1"
        if (Test-Path $envScript) {
            Write-Host "[*] Auto-activating Emscripten SDK from $cand..." -ForegroundColor Cyan
            $env:EMSDK_QUIET = "1"
            . $envScript
            break
        }
    }
    $emccCmd = Get-Command "emcc" -ErrorAction SilentlyContinue
}

if (-not $emccCmd -and $BootstrapEmsdk) {
    $emsdkDir = Join-Path $PSScriptRoot "emsdk"
    if (-not (Test-Path $emsdkDir)) {
        Write-Host "[*] Cloning Emscripten SDK into '$emsdkDir'..." -ForegroundColor Cyan
        git clone https://github.com/emscripten-core/emsdk.git $emsdkDir
    }
    Set-Location $emsdkDir
    .\emsdk.bat install latest
    .\emsdk.bat activate latest
    . .\emsdk_env.ps1
    Set-Location $PSScriptRoot
    $emccCmd = Get-Command "emcc" -ErrorAction SilentlyContinue
}

if (-not $emccCmd) {
    Write-Host "`n[!] Emscripten compiler (emcc) was not found in PATH." -ForegroundColor Red
    Write-Host "    To install or activate Emscripten:" -ForegroundColor Yellow
    Write-Host "      1. Run: .\build-web.ps1 -BootstrapEmsdk"
    Write-Host "      OR download from: https://github.com/emscripten-core/emsdk"
    Write-Host "      OR activate manually: C:\git\eatsbits\emsdk\emsdk_env.ps1"
    exit 1
}

$emccVer = (emcc --version)[0]
Write-Host "[+] Found Emscripten: $emccVer" -ForegroundColor Green

# ------------------------------------------------------------------
# STEP 2: Configure & Build Web Target
# ------------------------------------------------------------------
$buildDir = Join-Path $PSScriptRoot "build-web"

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "[*] Cleaning previous web build directory ('$buildDir')..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $buildDir
}

# Auto-purge corrupted cache if previous configure used unexpanded variable
$cacheFile = Join-Path $buildDir "CMakeCache.txt"
if (Test-Path $cacheFile) {
    try {
        $cacheContent = Get-Content $cacheFile -Raw
        if ($cacheContent -match '\$Config') {
            Write-Host "[*] Purging broken CMakeCache from previous failed configure..." -ForegroundColor Yellow
            Remove-Item -Recurse -Force $buildDir
        }
    } catch {}
}

if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

try {
    $ninjaDir = python -c "import ninja; print(ninja.BIN_DIR)" 2>$null
    if ($ninjaDir -and (Test-Path $ninjaDir)) {
        $env:PATH = "$ninjaDir;$env:PATH"
    }
    $userBase = python -m site --user-base 2>$null
    if ($userBase) {
        $userScripts = Join-Path $userBase "Scripts"
        if (Test-Path $userScripts) {
            $env:PATH = "$userScripts;$env:PATH"
        }
    }
} catch {}

$generator = if (Get-Command "ninja" -ErrorAction SilentlyContinue) { @("-G", "Ninja") } else { @() }
$cmakeBuildTypeArg = "-DCMAKE_BUILD_TYPE=$Config"

Write-Host "[*] Configuring CMake with Emscripten in '$buildDir' ($cmakeBuildTypeArg)..." -ForegroundColor Cyan
emcmake cmake -B build-web -S . @generator $cmakeBuildTypeArg
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[*] Compiling Eatsrealms WebAssembly & WebGPU pipeline..." -ForegroundColor Cyan
cmake --build build-web --config "$Config" --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Copy index.html and any web assets to root of build-web
if (Test-Path (Join-Path $buildDir "eatsrealms.html")) {
    Copy-Item (Join-Path $buildDir "eatsrealms.html") (Join-Path $buildDir "index.html") -Force
}

# Output binary and payload size report
Show-WebPayloadDetails -buildDir $buildDir -Config $Config

# ------------------------------------------------------------------
# STEP 3: Auto-Run & Serve Web App
# ------------------------------------------------------------------
$shouldLaunch = ($Run -or $Serve -or ((-not $NoLaunch) -and (-not $NoRun) -and (-not $NoServe)))
if ($shouldLaunch) {
    Stop-ExistingWebServer -TargetPort $Port

    Write-Host "[*] Starting local HTTP server on port $Port with COOP/COEP headers..." -ForegroundColor Green
    Write-Host "    [+] WebGPU HD-2D Diorama: http://localhost:$Port/index.html" -ForegroundColor Cyan

    $serverScript = @"
import sys, os, http.server, socketserver

web_dir = r'$buildDir'
if os.path.exists(web_dir):
    os.chdir(web_dir)

class CustomHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        super().end_headers()

Handler = CustomHandler
socketserver.TCPServer.allow_reuse_address = True
with socketserver.TCPServer(('', $Port), Handler) as httpd:
    print('Serving Eatsrealms WebGPU on http://localhost:$Port/index.html', flush=True)
    while True:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            break
        except Exception:
            pass
"@

    $serverScriptPath = Join-Path $buildDir "serve.py"
    Set-Content -Path $serverScriptPath -Value $serverScript

    Write-Host "[*] Launching browser to http://localhost:$Port/index.html..." -ForegroundColor Green
    Start-Process "http://localhost:$Port/index.html"

    if ($Detached) {
        Start-Process "python" -ArgumentList "`"$serverScriptPath`"" -WorkingDirectory $buildDir
        Write-Host "  -> Web server launched detached in background on port $Port." -ForegroundColor Green
    } else {
        python $serverScriptPath
    }
} else {
    Write-Host "[*] Server launch skipped (-NoServe / -NoRun active)." -ForegroundColor DarkGray
}
