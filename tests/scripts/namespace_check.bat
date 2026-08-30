@echo off
setlocal

REM Thin wrapper: the check itself is a CMake script so that this and the .sh
REM share one implementation. See cmake\NamespaceCheck.cmake for what it does
REM and why.
cmake -P "%~dp0..\..\cmake\NamespaceCheck.cmake"
if errorlevel 1 exit /b %errorlevel%

endlocal
