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

REM vcvars64.bat without -vcvars_ver pins to the toolset recorded in
REM Microsoft.VCToolsVersion.v143.default.txt, which can lag behind the
REM newest MSVC toolset installed side-by-side (Microsoft.VCToolsVersion.
REM default.txt). vcpkg's own compiler detection always picks the latest
REM installed toolset regardless of that pin. If the two disagree, packages
REM vcpkg builds (e.g. Catch2) get compiled against a newer/older STL than
REM the one this script sets up for the main build, causing unresolved
REM external symbol linker errors (e.g. __std_search_1). Force the same
REM "latest" toolset vcpkg uses so both stay in sync.
set "VCVER="
set /p VCVER=<"%VSPATH%\VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt"

REM Scripts like precommit.bat `call` several sub-scripts in the same cmd.exe
REM session, and each of those calls this file. Once vcvars64.bat has already
REM set up this exact toolset in the current session, skip re-running it —
REM it's a no-op anyway, but it reprints the noisy "Developer Command Prompt"
REM banner every time.
if /i "%VCToolsVersion%"=="%VCVER%" (
    exit /b 0
)

set "VSCMD_SKIP_SENDTELEMETRY=1"

if defined VCVER (
    call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=%VCVER%
) else (
    call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat"
)
if errorlevel 1 exit /b %errorlevel%

exit /b 0
