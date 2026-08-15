@echo off
setlocal

set "PRESET=ninja-debug-windows"

cmake --build build\%PRESET% --target format
if errorlevel 1 exit /b %errorlevel%

endlocal
