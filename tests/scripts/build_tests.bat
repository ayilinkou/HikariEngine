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

cmake --build "build\%PRESET%" --target core_tests platform_tests rhi_tests rhi_gpu_tests
if errorlevel 1 exit /b %errorlevel%

endlocal
