@echo off
setlocal

if not "%~1"=="" (
    set "PRESET=%~1"
) else (
    set "PRESET=ninja-debug-windows"
)

build\%PRESET%\VulkanApp.exe --report --screenshot --frames --fixed-dt --scene --camera-preset 1
if errorlevel 1 exit /b %errorlevel%

endlocal
