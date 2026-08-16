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

cmake --build build\%PRESET% --target format-check
if errorlevel 1 exit /b %errorlevel%

endlocal
