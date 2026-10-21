<#
.SYNOPSIS
    Automated build, test, size profiling, and launch script for Eatsrealms Native HD-2D RPG & Diorama Engine.
#>

[CmdletBinding()]
param (
    [switch]$Clean,
    [ValidateSet("Release", "Debug", "RelWithDebInfo")]
    [string]$Config = "Release",
    [switch]$Test,
    [switch]$RenderPreview,
    [switch]$Run,
    [switch]$NoLaunch,
    [switch]$NoRun,
    [switch]$Detached
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

Write-Host "==========================================================================================" -ForegroundColor Cyan
Write-Host "  Eatsrealms Native 2.5D (HD-2D) RPG & Diorama Engine Build & Profiling Script" -ForegroundColor Cyan
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

function Show-BinarySizeDetails {
    param (
        [string]$exeDir,
        [string]$Config
    )

    Write-Host "`n==========================================================================================" -ForegroundColor Cyan
    Write-Host "  Eatsrealms Runtime & Binary Size Report [$Config Target]" -ForegroundColor Cyan
    Write-Host "==========================================================================================" -ForegroundColor Cyan

    $runtimeFiles = @(
        @{
            Name     = "eatsrealms.exe"
            Role     = "Primary Engine Binary (x64 /O2)"
            Path     = (Join-Path $exeDir "eatsrealms.exe")
            Category = "Core Executable"
            Required = $true
        },
        @{
            Name     = "wgpu_native.dll"
            Role     = "WebGPU Native Driver Runtime"
            Path     = (Join-Path $exeDir "wgpu_native.dll")
            Category = "Native Shared Library"
            Required = $true
        },
        @{
            Name     = "starfall_vale.eats"
            Role     = "Eatscript Scene Recipe Script"
            Path     = (Join-Path $PSScriptRoot "scenes\starfall_vale.eats")
            Category = "Procedural Script"
            Required = $true
        }
    )

    $glfwLibPath = Join-Path $PSScriptRoot "build\third_party\glfw\src\$Config\glfw3.lib"
    if (-not (Test-Path $glfwLibPath)) {
        $glfwLibPath = Join-Path $PSScriptRoot "build\third_party\glfw\src\glfw3.lib"
    }

    $devFiles = @(
        @{
            Name     = "eatsrealms_tests.exe"
            Role     = "Unit Test Suite Runner"
            Path     = (Join-Path $exeDir "eatsrealms_tests.exe")
            Category = "Test Binary"
        },
        @{
            Name     = "eatsrealms_core.lib"
            Role     = "HD-2D World, PropFactory & Foley Static Archive"
            Path     = (Join-Path $exeDir "eatsrealms_core.lib")
            Category = "Build Static Lib"
        },
        @{
            Name     = "eats_script.lib"
            Role     = "Eatscript VM & Compiler Static Archive"
            Path     = (Join-Path $exeDir "eats_script.lib")
            Category = "Build Static Lib"
        },
        @{
            Name     = "eatsrealms_render.lib"
            Role     = "WebGPU HD-2D Diorama Renderer Static Archive"
            Path     = (Join-Path $exeDir "eatsrealms_render.lib")
            Category = "Build Static Lib"
        },
        @{
            Name     = "glfw3.lib"
            Role     = "Bundled GLFW 3.4 Static Archive"
            Path     = $glfwLibPath
            Category = "Build Static Lib"
        }
    )

    # Discover any debug PDBs
    $pdbFiles = Get-ChildItem -Path $exeDir -Filter "*.pdb" -ErrorAction SilentlyContinue | ForEach-Object {
        @{
            Name     = $_.Name
            Role     = "MSVC Debug Symbols"
            Path     = $_.FullName
            Category = "Debug Symbols"
        }
    }
    if ($pdbFiles) {
        $devFiles += $pdbFiles
    }

    # Calculate Standalone Runtime Package Total
    $runtimeTotalBytes = 0
    $resolvedRuntime = @()
    foreach ($item in $runtimeFiles) {
        if (Test-Path $item.Path) {
            $fi = Get-Item $item.Path
            $runtimeTotalBytes += $fi.Length
            $resolvedRuntime += [PSCustomObject]@{
                Name     = $item.Name
                Role     = $item.Role
                Category = $item.Category
                Bytes    = $fi.Length
                Exists   = $true
                Path     = $item.Path
            }
        } else {
            $resolvedRuntime += [PSCustomObject]@{
                Name     = $item.Name
                Role     = $item.Role
                Category = $item.Category
                Bytes    = 0
                Exists   = $false
                Path     = $item.Path
            }
        }
    }

    Write-Host "`n1. Standalone Runtime Distribution (Minimum Required to Ship & Run):" -ForegroundColor Green
    Write-Host ("{0,-24} {1,-34} {2,14} {3,12} {4,9}" -f "File Name", "Role", "Size (Bytes)", "Formatted", "% Total") -ForegroundColor Gray
    Write-Host ("-" * 98) -ForegroundColor DarkGray

    foreach ($f in $resolvedRuntime) {
        if ($f.Exists) {
            $pct = if ($runtimeTotalBytes -gt 0) { ($f.Bytes / $runtimeTotalBytes) * 100 } else { 0 }
            $fmtSize = Format-ByteSize $f.Bytes
            $bytesStr = "{0:N0} B" -f $f.Bytes
            $pctStr = "{0:N1}%" -f $pct
            Write-Host ("{0,-24} {1,-34} {2,14} {3,12} {4,9}" -f $f.Name, $f.Role, $bytesStr, $fmtSize, $pctStr) -ForegroundColor White
        } else {
            Write-Host ("{0,-24} {1,-34} {2,14} {3,12} {4,9}" -f $f.Name, $f.Role, "[MISSING]", "N/A", "0.0%") -ForegroundColor Red
        }
    }
    Write-Host ("-" * 98) -ForegroundColor DarkGray
    $totalFmt = Format-ByteSize $runtimeTotalBytes
    $totalBytesStr = "{0:N0} B" -f $runtimeTotalBytes
    Write-Host ("{0,-24} {1,-34} {2,14} {3,12} {4,9}" -f "STANDALONE RUNTIME TOTAL", "Full self-contained app package", $totalBytesStr, $totalFmt, "100.0%") -ForegroundColor Yellow

    Write-Host "`n2. Development, Test & Static Build Archives:" -ForegroundColor Cyan
    Write-Host ("{0,-24} {1,-34} {2,14} {3,12}" -f "File Name", "Role", "Size (Bytes)", "Formatted") -ForegroundColor Gray
    Write-Host ("-" * 88) -ForegroundColor DarkGray

    $devTotalBytes = 0
    foreach ($item in $devFiles) {
        if (Test-Path $item.Path) {
            $fi = Get-Item $item.Path
            $devTotalBytes += $fi.Length
            $fmtSize = Format-ByteSize $fi.Length
            $bytesStr = "{0:N0} B" -f $fi.Length
            Write-Host ("{0,-24} {1,-34} {2,14} {3,12}" -f $item.Name, $item.Role, $bytesStr, $fmtSize) -ForegroundColor White
        }
    }
    Write-Host ("-" * 88) -ForegroundColor DarkGray
    $devTotalFmt = Format-ByteSize $devTotalBytes
    $devTotalBytesStr = "{0:N0} B" -f $devTotalBytes
    Write-Host ("{0,-24} {1,-34} {2,14} {3,12}" -f "DEV ARTIFACTS TOTAL", "Unit tests & static archives", $devTotalBytesStr, $devTotalFmt) -ForegroundColor Cyan

    $exeEntry = $resolvedRuntime | Where-Object { $_.Name -eq "eatsrealms.exe" }
    $wgpuEntry = $resolvedRuntime | Where-Object { $_.Name -eq "wgpu_native.dll" }
    $sceneEntry = $resolvedRuntime | Where-Object { $_.Name -eq "starfall_vale.eats" }

    $exeBytes = if ($exeEntry) { $exeEntry.Bytes } else { 0 }
    $wgpuBytes = if ($wgpuEntry) { $wgpuEntry.Bytes } else { 0 }
    $sceneBytes = if ($sceneEntry) { $sceneEntry.Bytes } else { 0 }

    Write-Host "`n3. Size & Distribution Highlights:" -ForegroundColor Green
    Write-Host ("  * Core Engine Executable  : {0} ({1:N0} bytes)" -f (Format-ByteSize $exeBytes), $exeBytes)
    Write-Host ("  * WebGPU Driver Runtime   : {0} ({1:N0} bytes)" -f (Format-ByteSize $wgpuBytes), $wgpuBytes)
    Write-Host ("  * Scene Recipe Script     : {0} ({1:N0} bytes)" -f (Format-ByteSize $sceneBytes), $sceneBytes)
    Write-Host ("  * Complete Distribution   : {0} ({1:N0} bytes) [Zero external installer or network needed]" -f $totalFmt, $runtimeTotalBytes)
    Write-Host "==========================================================================================`n" -ForegroundColor Cyan
}

$buildDir = Join-Path $PSScriptRoot "build"
if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "[*] Cleaning build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $buildDir
}

if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

$cmakeArgs = @("-B", "build", "-S", ".")
if ($Test) {
    $cmakeArgs += "-DBUILD_TESTING=ON"
} else {
    $cmakeArgs += "-DBUILD_TESTING=OFF"
}
cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build build --config $Config --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$exeDir = Join-Path $buildDir $Config
if (-not (Test-Path (Join-Path $exeDir "eatsrealms.exe"))) {
    $exeDir = $buildDir
}

$wgpuDest = Join-Path $exeDir "wgpu_native.dll"
$wgpuSrc = Join-Path $PSScriptRoot "third_party\wgpu\lib\wgpu_native.dll"
if ((-not (Test-Path $wgpuDest)) -and (Test-Path $wgpuSrc)) {
    Copy-Item $wgpuSrc $wgpuDest -Force
}

if ($Test) {
    Write-Host "`n[*] Running Eatsrealms HD-2D Unit Tests..." -ForegroundColor Yellow
    & (Join-Path $exeDir "eatsrealms_tests.exe")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

if ($RenderPreview) {
    Write-Host "`n[*] Rendering Starfall Vale HD-2D Diorama Preview PNG..." -ForegroundColor Yellow
    & (Join-Path $exeDir "eatsrealms.exe") --render-png "assets/starfall_vale_hd2d.png" --width 1280 --height 720
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

# Always output full runtime and binary size details
Show-BinarySizeDetails -exeDir $exeDir -Config $Config

# Launch the app once done (by default, unless -NoLaunch / -NoRun is specified)
$shouldLaunch = ($Run -or ((-not $NoLaunch) -and (-not $NoRun)))
if ($shouldLaunch) {
    Write-Host "[*] Launching Eatsrealms Interactive WebGPU HD-2D Diorama Viewer..." -ForegroundColor Green
    $mainExe = Join-Path $exeDir "eatsrealms.exe"
    if ($Detached) {
        Start-Process -FilePath $mainExe -WorkingDirectory $PSScriptRoot
        Write-Host "  -> Process launched detached in background." -ForegroundColor Green
    } else {
        & $mainExe
    }
} else {
    Write-Host "[*] Application launch skipped (-NoLaunch / -NoRun active)." -ForegroundColor DarkGray
}
