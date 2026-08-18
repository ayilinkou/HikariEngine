@echo off
setlocal

if not "%~1"=="" (
    set "PRESET=%~1"
) else (
    set "PRESET=ninja-debug-windows"
)

if not "%PRESET%"=="msvc" (
    call "%~dp0..\..\scripts\envsetup.bat"
    if errorlevel 1 exit /b %errorlevel%
)

REM Aggregate target: compiles every src/ header and every engine module's
REM public headers on their own, with no PCH.
cmake --build build\%PRESET% --target HeaderSelfContainment
if errorlevel 1 exit /b %errorlevel%

endlocal
