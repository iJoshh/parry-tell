@echo off
REM swap-mode.bat — switch parry-tell-probe between smoke / qualification / discovery
REM
REM Usage:   swap-mode.bat smoke
REM          swap-mode.bat qualification
REM          swap-mode.bat discovery
REM
REM Run from anywhere. Reads the staged INIs from C:\Projects\elden-ring\probe\stage\
REM and copies the matching one over Game\mods\parry-tell-probe.ini.
REM
REM IMPORTANT: Elden Ring must be CLOSED when you run this. Files are
REM locked while the game is running.

setlocal

REM Use quoted-set form so a `&` or other metachar in the argument can't
REM break out of the assignment before the whitelist checks below.
set "MODE=%~1"
if "%MODE%"=="" goto usage
if /I "%MODE%"=="smoke" goto valid
if /I "%MODE%"=="qualification" goto valid
if /I "%MODE%"=="discovery" goto valid
goto usage

:valid
set "SRC=C:\Projects\elden-ring\probe\stage\parry-tell-probe.ini.%MODE%"
set "DST=C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mods\parry-tell-probe.ini"

if not exist "%SRC%" (
    echo [ERROR] staged INI not found: %SRC%
    exit /b 1
)

if not exist "%DST%" (
    echo [ERROR] mods folder INI not found at expected path:
    echo   %DST%
    echo If your Elden Ring is installed elsewhere, edit this script's DST line.
    exit /b 2
)

copy /Y "%SRC%" "%DST%" >nul
if errorlevel 1 (
    echo [ERROR] copy failed. Is Elden Ring still running? Close it and retry.
    exit /b 3
)

echo OK: swapped to %MODE% mode.
echo You can now launch Elden Ring.
exit /b 0

:usage
echo Usage: %~nx0 ^<smoke^|qualification^|discovery^>
echo.
echo Examples:
echo   %~nx0 smoke
echo   %~nx0 qualification
echo   %~nx0 discovery
exit /b 64
