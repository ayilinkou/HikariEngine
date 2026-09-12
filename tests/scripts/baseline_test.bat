@echo off
setlocal

set "PRESET="
set "UPDATE="

REM One argument per pass, and each one read into ARG first: a substring test
REM inside a parenthesised block is expanded when the block is parsed rather
REM than when it runs, so the value has to already be in a variable by then.
:parse
if "%~1"=="" goto parsed
set "ARG=%~1"
if /i "%ARG%"=="--update" (
    set "UPDATE=--update"
    shift
    goto parse
)
if "%ARG:~0,1%"=="-" (
    echo Unknown option: %ARG% 1>&2
    echo Usage: baseline_test.bat [preset] [--update] 1>&2
    exit /b 3
)
set "PRESET=%ARG%"
shift
goto parse
:parsed

if "%PRESET%"=="" set "PRESET=ninja-debug-windows"

REM Fixed names rather than the app's timestamped defaults, because the run and
REM the comparison that follows it have to agree on where the files went. Both
REM directories are gitignored, so each run overwrites the last; the committed
REM pair under tests\baseline\ is the only copy worth keeping.
set "REPORT=tests/reports/baseline.json"
set "CAPTURE=tests/screenshots/baseline.png"

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
REM
REM --no-ui is what makes the capture about the scene. The editor panel is a fifth
REM of the frame, and it is not reproducible on principle: nothing warps the
REM cursor at startup, so the panel carries a hover highlight on whichever widget
REM the mouse was last over. It has been stable in practice only because the mouse
REM did not move between runs. ImGui still initialises and its pass still records,
REM so the counters in the report are unaffected by the flag.
build\%PRESET%\HikariEditor.exe --report "%REPORT%" --screenshot "%CAPTURE%" ^
    --frames --fixed-dt --scene --camera-preset 1 ^
    --resolution 1920x1080 --borderless --no-ui
if errorlevel 1 exit /b %errorlevel%

echo.

REM The comparison decides the exit status: 1, 2 and 3 all mean something
REM different and the caller needs to see which.
build\%PRESET%\HikariCompare.exe ^
    --actual-report "%REPORT%" --expected-report tests/baseline/report.json ^
    --actual-image "%CAPTURE%" --expected-image tests/baseline/screenshot.png ^
    %UPDATE%
exit /b %errorlevel%
