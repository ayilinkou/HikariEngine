@echo off

call "%~dp0scripts/envsetup.bat"
    if errorlevel 1 exit /b %errorlevel%

cmake --preset msvc
