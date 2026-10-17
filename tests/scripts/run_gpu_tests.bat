@echo off
setlocal

if not "%~1"=="" (
    set "PRESET=%~1"
) else (
    set "PRESET=ninja-debug-windows"
)

REM Developers without a device get skips. CI supplies lavapipe/WARP and sets
REM HIKARI_TESTS_REQUIRE_DEVICE=1 so missing device coverage fails there.
ctest --test-dir "build\%PRESET%" -L gpu --no-tests=error --output-on-failure
if errorlevel 1 exit /b %errorlevel%

endlocal
