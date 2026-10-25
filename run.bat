@echo off
:: DuneBox Launcher - fast-forwards this checkout, then runs the pre-built exe
:: or fetches it. A published release downloads with no GitHub sign-in.
:: An older install is replaced when that release is newer; live settings stay put.
:: Order: checkout fast-forward -> newer published release -> local exe -> CI artifact.
:: Windows reads this file as it goes, so a pull that replaces run.bat restarts
:: the launcher once (--updated) before the release check.
setlocal enabledelayedexpansion
title DuneBox AR Sandbox

set "ROOT=%~dp0"
set "BINDIR=%ROOT%bin"
set "EXE=%BINDIR%\Magic-Sand.exe"
set "REPO=Manaiakalani/DuneBox"
set "ARTIFACT=DuneBox-windows-x64"

:: The parenthesis block is read in full before git runs, so the relaunch still
:: happens when the pull replaces run.bat.
if /I "%~1"=="--updated" goto :afterupdate

(
echo  Checking the DuneBox checkout...
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%update-checkout.ps1"
if errorlevel 10 (
    "%~f0" --updated
    exit /b 0
)
)

:afterupdate
echo.
echo  NOTE: sandcam and DuneBox (C++) can run together. In sandcam, turn on
echo        "Share this sensor with DuneBox", and set kinectVersion to 4 in
echo        bin\data\settings\kinectProjectorSettings.xml.
echo        Opening Kinect v2 from both apps at once makes the camera look disconnected.
echo.

:: 1) An existing install with no gh CLI just runs. Do not winget on a kiosk boot.
set "NEED_FETCH=0"
if not exist "%EXE%" set "NEED_FETCH=1"
where gh >nul 2>&1
if errorlevel 1 (
    if "!NEED_FETCH!"=="0" goto :run
    echo  Installing GitHub CLI...
    winget install --id GitHub.cli -e --silent --accept-source-agreements --accept-package-agreements >nul 2>&1
    set "PATH=%PATH%;%ProgramFiles%\GitHub CLI"
)

if "!NEED_FETCH!"=="0" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%release-is-newer.ps1" -Repo "%REPO%" -BinDir "%BINDIR%"
    if errorlevel 1 goto :run
    echo.
    echo  A newer DuneBox release is available. Updating...
    echo.
) else (
    echo.
    echo  DuneBox binary not found - fetching the pre-built app...
    echo.
)

:: 2) Public release asset. No sign-in.
set "ZIPFILE=%TEMP%\DuneBox-windows-x64.zip"
if exist "%ZIPFILE%" del "%ZIPFILE%" >nul 2>&1
gh release download --repo %REPO% --pattern "%ARTIFACT%.zip" --output "%ZIPFILE%" >nul 2>&1
if exist "%ZIPFILE%" goto :extract

if exist "%EXE%" (
    echo  Could not download the update. Starting the installed copy.
    goto :run
)

:: 3) CI artifacts are not public downloads. Sign in only for this fallback.
echo  No release yet - checking latest successful CI build...
gh auth status >nul 2>&1
if errorlevel 1 (
    echo  One-time GitHub sign-in required to download a CI build...
    gh auth login --hostname github.com --git-protocol https --web
)
for /f "usebackq delims=" %%i in (`gh run list --repo %REPO% --workflow "Build & Release" --status success --limit 1 --json databaseId --jq ".[0].databaseId" 2^>nul`) do set "RUNID=%%i"
if not "%RUNID%"=="" (
    set "ADIR=%TEMP%\dunebox-artifact"
    if exist "!ADIR!" rmdir /s /q "!ADIR!" >nul 2>&1
    gh run download !RUNID! --repo %REPO% --name %ARTIFACT% --dir "!ADIR!" >nul 2>&1
    for %%z in ("!ADIR!\*.zip") do set "ZIPFILE=%%z"
    if exist "!ZIPFILE!" goto :extract
)

echo.
echo  No pre-built binary is available yet.
echo  A build may still be running. Check:
echo     gh run list --repo %REPO%
echo  Then double-click run.bat again once it succeeds.
echo.
pause
exit /b 1

:extract
echo  Extracting to bin\...
if not exist "%BINDIR%" mkdir "%BINDIR%"
:: extract-release.ps1 unpacks to TEMP, copies exe/DLLs, and merges data/
:: without overwriting a live calibration.xml. A multi-line -Command here
:: would be split into separate PowerShell arguments by cmd.exe.
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%extract-release.ps1" "%ZIPFILE%" "%BINDIR%"
if errorlevel 1 (
    echo  Extract failed.
    pause
    exit /b 1
)
del "%ZIPFILE%" >nul 2>&1
if not exist "%EXE%" (
    echo  Extract failed - Magic-Sand.exe not found.
    pause
    exit /b 1
)

:run
call :check_v2_runtime
echo  Starting DuneBox...
cd /d "%BINDIR%"
start "" "Magic-Sand.exe"
exit /b 0

:check_v2_runtime
:: If configured for Kinect v2, verify the Kinect v2 runtime is installed.
:: Kinect20.dll ships in System32 via the "Kinect for Windows Runtime 2.0"
:: installer, not alongside the app. Non-fatal: just warn so a missing runtime
:: is an obvious explanation for "no sensor detected".
set "SETTINGS=%BINDIR%\data\settings\kinectProjectorSettings.xml"
if not exist "%SETTINGS%" goto :eof
findstr /i /c:"<kinectVersion>2</kinectVersion>" "%SETTINGS%" >nul 2>&1
if errorlevel 1 goto :eof
if exist "%WINDIR%\System32\Kinect20.dll" goto :eof
echo.
echo  [warning] kinectVersion=2 but the Kinect v2 runtime was not found.
echo            Install "Kinect for Windows Runtime 2.0":
echo            https://www.microsoft.com/download/details.aspx?id=44559
echo.
goto :eof
