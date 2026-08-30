@echo off
setlocal

REM Thin wrapper: the file list and the invocation live in cmake\Format.cmake so
REM that this, the .sh and the `format-check` build target share one
REM implementation. No preset argument — checking formatting needs no configured
REM tree, which is what lets CI run it on a bare runner.
cmake -P "%~dp0..\..\cmake\Format.cmake"
if errorlevel 1 exit /b %errorlevel%

endlocal
