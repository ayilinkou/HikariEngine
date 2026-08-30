@echo off
setlocal

REM Thin wrapper: the file list and the invocation live in cmake\Format.cmake so
REM that this, the .sh and the `format` build target share one implementation.
REM No preset argument — formatting needs no configured tree.
cmake -DFIX=ON -P "%~dp0..\cmake\Format.cmake"
if errorlevel 1 exit /b %errorlevel%

endlocal
