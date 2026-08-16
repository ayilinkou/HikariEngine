@echo off
setlocal

if not "%~1"=="" (
    set "PRESET=%~1"
) else (
    set "PRESET=ninja-debug-windows"
)

echo Running precommit for %PRESET%...

call build.bat "%PRESET%"
if errorlevel 1 exit /b %errorlevel%

call tests\scripts\build_tests.bat "%PRESET%"
if errorlevel 1 exit /b %errorlevel%

call tests\scripts\run_unit_tests.bat "%PRESET%"
if errorlevel 1 exit /b %errorlevel%

call scripts\format_check.bat "%PRESET%"
if errorlevel 1 exit /b %errorlevel%

echo Precommit succeeded!

endlocal
