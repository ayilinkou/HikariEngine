@echo off
setlocal

if not "%~1"=="" (
    set "PRESET=%~1"
) else (
    set "PRESET=ninja-debug-windows"
)

if not "%PRESET%"=="msvc" (
    call "%~dp0envsetup.bat"
    if errorlevel 1 exit /b %errorlevel%
)

ctest --test-dir "build\%PRESET%" -L unit --output-on-failure
if errorlevel 1 exit /b %errorlevel%

endlocal
