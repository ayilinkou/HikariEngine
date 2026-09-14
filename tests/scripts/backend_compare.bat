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

REM Fixed names in the gitignored reports directory, because the runs and the
REM comparison that follows have to agree on where the files went.
set "VULKAN_REPORT=tests/reports/backend_vulkan.json"
set "D3D12_REPORT=tests/reports/backend_d3d12.json"

REM The same run twice, one per backend, on each backend's default adapter.
REM Headless, so no present mode or window gates anything, and without captures:
REM across backends the pixels are skipped until D3D12 reaches parity, and what
REM this checks is that both backends made the same decisions — the counters, held
REM exact across backends (plan D26). The frame count only needs to be past the
REM first frames' one-off work; the counters describe the last frame drawn.
REM
REM D3D12 validates on the GPU in full rather than at its default: the counters
REM are compared across backends only when each backend's own validation sub-mode
REM is at its strongest, and full is where the resource-state checks run.
set "RUN_FLAGS=--frames 100 --fixed-dt --scene scenes/test_scene.map --camera-preset 1 --resolution 1920x1080 --no-ui"

build\%PRESET%\HikariHeadless.exe --backend Vulkan --report "%VULKAN_REPORT%" %RUN_FLAGS%
if errorlevel 1 exit /b %errorlevel%

build\%PRESET%\HikariHeadless.exe --backend D3D12 --report "%D3D12_REPORT%" %RUN_FLAGS% --d3d12-gpu-based-validation full
if errorlevel 1 exit /b %errorlevel%

echo.

REM Exit 2 is the expected verdict until parity: nothing moved, and the pixels
REM were not compared. 1 means a counter differs between the backends, which is a
REM bug in one of them.
build\%PRESET%\HikariCompare.exe --actual-report "%D3D12_REPORT%" --expected-report "%VULKAN_REPORT%"
exit /b %errorlevel%
