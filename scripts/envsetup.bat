@echo off
REM Ninja (unlike the Visual Studio generator) has no built-in knowledge of
REM where MSVC lives — cl.exe/link.exe/the Windows SDK only become reachable
REM after vcvars64.bat has run in this shell session. Only needed for the
REM Ninja+MSVC presets; skip it for the "msvc" (Visual Studio generator)
REM preset, which finds the toolset itself via MSBuild.

REM Deliberately NOT wrapped in setlocal/endlocal: this is meant to be
REM `call`ed from inside another script's own setlocal scope, so its PATH/
REM INCLUDE/LIB changes become part of THAT script's environment for the rest
REM of its execution, and get cleaned up automatically when that script's own
REM setlocal scope ends — rather than vanishing the moment this script
REM itself returns.

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    echo Could not locate Visual Studio with the C++ toolset.
    exit /b 1
)

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%

exit /b 0
