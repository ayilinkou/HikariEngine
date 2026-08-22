@echo off
setlocal

if not "%~1"=="" (
    set "PRESET=%~1"
) else (
    set "PRESET=ninja-debug-windows"
)

REM Separate from run_unit_tests.bat because these need a Vulkan ICD, and CI has
REM none. They skip rather than fail without one, so precommit runs them and a
REM developer with no GPU is not blocked — but that same silence is why CI does
REM not run them: a machine with no ICD would report a green run of nothing.
REM Anything relying on these having actually executed must check that they did.
ctest --test-dir "build\%PRESET%" -L gpu --output-on-failure
if errorlevel 1 exit /b %errorlevel%

endlocal
