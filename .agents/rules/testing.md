---
description: Strict policy against running automated tests, test executables, and background GPU processes.
always_on: true
---

# Strict Testing and Process Execution Policy

## 1. Absolute Prohibition of Automated Testing
- **NEVER run tests automatically under any circumstances.**
- This strictly includes:
  - Unit test binaries (e.g., `eatsrealms_tests.exe`, `test_eatsterm.exe`, `test_diag.exe`, or any `*test*.exe`).
  - Test runner commands (e.g., `ctest`, `ninja test`, `make test`).
  - Build script test switches (e.g., `.\build.ps1 -Test`).
  - Headless smoke tests or CLI smoke test flags (`--smoke-test`, `--headless`).
  - Diagnostic test binaries created or run during debugging, bug-hunting, or investigation.
- **No Exceptions for "Diagnostics" or "Verifications"**: Do NOT execute test binaries to "check behavior", "diagnose issues", "benchmark", "verify fixes", or "inspect output".
- **User Exclusively Handles Testing**: The user actively tests updates manually. Automated testing is strictly prohibited unless the user explicitly and manually gives an unambiguous instruction in the current conversation (e.g., "run tests now").

## 2. GPU and Process Safety on Windows
- **Severe System Stalls**: Test and engine binaries in this repository initialize hardware graphics contexts (WebGPU / `wgpu-native`, DirectX, Vulkan, OpenGL). When executed in non-interactive subshells or sent to background tasks, they hang, leak GPU device contexts, and cause Windows and Antigravity IDE to stall severely.
- **Never Launch Background Interactive/GPU Processes**:
  - Never run commands that spawn long-running GUI or GPU processes into the background.
  - If compiling or building is explicitly requested by the user, ALWAYS use flags that prevent launching or running the application:
    - In `build.ps1`: ALWAYS provide `-NoRun -NoLaunch` (e.g., `powershell -ExecutionPolicy Bypass -File .\build.ps1 -NoRun -NoLaunch`).
    - In `cmake --build`: only build the target requested, never build or run test targets.
