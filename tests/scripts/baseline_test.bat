@echo off
setlocal

if not "%~1"=="" (
    set "PRESET=%~1"
) else (
    set "PRESET=ninja-debug-windows"
)

REM --borderless is what fixes the screenshot's size, not --resolution. A window
REM size is a request the window system may refuse, and a tiling compositor
REM (Hyprland, sway, i3) always does — it puts the window in whatever tile the
REM layout says, so the captured frame comes out at the tile's size and differs
REM between machines and between layouts. Covering the display is honoured, so
REM it gives the same extent on every run.
REM
REM --resolution is still passed: it is what a non-tiling window system uses,
REM and it is the size the window is created at before the mode change, so a
REM compositor that refuses fullscreen degrades to the right size rather than to
REM three quarters of the display.
REM
REM Both only pin the extent to *this* display's. A capture that does not depend
REM on the display at all needs an offscreen render target (Part IV steps 38-39).
build\%PRESET%\HikariEngine.exe --report --screenshot --frames --fixed-dt --scene --camera-preset 1 ^
    --resolution 1920x1080 --borderless
if errorlevel 1 exit /b %errorlevel%

endlocal
