@echo off

setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
  echo Could not locate Visual Studio with the C++ toolset.
  pause & exit /b 1
)

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" || (pause & exit /b 1)
cmake --workflow --preset ninja-debug-windows
