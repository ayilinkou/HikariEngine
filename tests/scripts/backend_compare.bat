@echo off
setlocal

set "PRESET="

:parse
if "%~1"=="" goto parsed
set "ARG=%~1"
if "%ARG:~0,1%"=="-" (
    echo Unknown option: %ARG% 1>&2
    echo Usage: backend_compare.bat [preset] 1>&2
    exit /b 3
)
set "PRESET=%ARG%"
shift
goto parse
:parsed

if "%PRESET%"=="" set "PRESET=ninja-debug-windows"

REM Fixed names in the gitignored reports and screenshots directories, because the
REM runs and the comparison that follows have to agree on where the files went.
set "VULKAN_REPORT=tests/reports/backend_vulkan.json"
set "D3D12_REPORT=tests/reports/backend_d3d12.json"
set "VULKAN_CAPTURE=tests/screenshots/backend_vulkan.png"
set "D3D12_CAPTURE=tests/screenshots/backend_d3d12.png"

REM The same run twice, one per backend, on each backend's default adapter.
REM Headless, so no present mode or window gates anything. Both signals are
REM compared: the counters exactly, since both backends must make the same
REM decisions, and the pixels within the tolerance measured for the build type,
REM since two backends round differently on one adapter (plan D26). The pixels are
REM compared only when the PCI identifiers say both runs had the same adapter, so a
REM machine whose backends pick different default adapters gets a skip naming them
REM rather than a verdict. The tolerance was measured from exactly these flags: the
REM scene, camera and extent, and frame 100, which is also past the first frames'
REM one-off work.
REM
REM D3D12 validates on the GPU in full rather than at its default: the counters
REM are compared across backends only when each backend's own validation sub-mode
REM is at its strongest, and full is where the resource-state checks run.
set "RUN_FLAGS=--frames 100 --fixed-dt --scene scenes/test_scene.map --camera-preset 1 --resolution 1920x1080 --no-ui"

build\%PRESET%\HikariHeadless.exe --backend Vulkan --report "%VULKAN_REPORT%" --screenshot "%VULKAN_CAPTURE%" %RUN_FLAGS%
if errorlevel 1 exit /b %errorlevel%

build\%PRESET%\HikariHeadless.exe --backend D3D12 --report "%D3D12_REPORT%" --screenshot "%D3D12_CAPTURE%" %RUN_FLAGS% --d3d12-gpu-based-validation full
if errorlevel 1 exit /b %errorlevel%

echo.

REM Exit 0 is the expected verdict: every counter matched, and the pixels differ no
REM more than the tolerance allows. 1 means a counter differs between the backends,
REM which is a bug in one of them, or the pixels moved past the tolerance, which is a
REM rendering change or a driver update to measure and explain again.
build\%PRESET%\HikariCompare.exe --actual-report "%D3D12_REPORT%" --expected-report "%VULKAN_REPORT%" --actual-image "%D3D12_CAPTURE%" --expected-image "%VULKAN_CAPTURE%"
exit /b %errorlevel%
