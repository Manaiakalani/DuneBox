@echo off
:: DuneBox Launcher — runs the pre-built exe or downloads the latest release
setlocal enabledelayedexpansion

title DuneBox AR Sandbox

set "BINDIR=%~dp0bin"
set "EXE=%BINDIR%\Magic-Sand.exe"

:: If exe exists, just run it
if exist "%EXE%" (
    echo Starting DuneBox...
    cd /d "%BINDIR%"
    start "" "Magic-Sand.exe"
    exit /b
)

:: No exe found — try to download latest release
echo.
echo  DuneBox is not built yet.
echo  Downloading latest release from GitHub...
echo.

where gh >nul 2>&1
if %errorlevel% neq 0 (
    echo  gh CLI not found. Installing via winget...
    winget install --id GitHub.cli --accept-source-agreements --accept-package-agreements >nul 2>&1
    set "PATH=%PATH%;%ProgramFiles%\GitHub CLI"
)

:: Download latest release asset
set "ZIPFILE=%TEMP%\DuneBox-windows-x64.zip"
gh release download --repo Manaiakalani/DuneBox --pattern "DuneBox-windows-x64.zip" --output "%ZIPFILE%" 2>nul

if not exist "%ZIPFILE%" (
    echo.
    echo  ❌ No release found. The first CI build hasn't run yet.
    echo.
    echo  Options:
    echo    1. Push a tag to trigger a release:
    echo       git tag v0.1.0 ^&^& git push origin v0.1.0
    echo.
    echo    2. Or build manually with Visual Studio:
    echo       Open Magic-Sand.sln → x64 Release → Build
    echo.
    pause
    exit /b 1
)

:: Extract
echo  Extracting to bin\...
if not exist "%BINDIR%" mkdir "%BINDIR%"
powershell -Command "Expand-Archive -Path '%ZIPFILE%' -DestinationPath '%BINDIR%' -Force"
del "%ZIPFILE%"

if exist "%EXE%" (
    echo  ✅ Ready! Starting DuneBox...
    cd /d "%BINDIR%"
    start "" "Magic-Sand.exe"
) else (
    echo  ❌ Extract failed — exe not found.
    pause
)
