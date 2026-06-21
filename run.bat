@echo off
:: DuneBox Launcher - runs the pre-built exe, or fetches it automatically.
:: Order: local exe -> published release -> latest successful CI artifact.
setlocal enabledelayedexpansion
title DuneBox AR Sandbox

set "ROOT=%~dp0"
set "BINDIR=%ROOT%bin"
set "EXE=%BINDIR%\Magic-Sand.exe"
set "REPO=Manaiakalani/DuneBox"
set "ARTIFACT=DuneBox-windows-x64"

:: 1) Already installed? Just run it.
if exist "%EXE%" goto :run

echo.
echo  DuneBox binary not found - fetching the pre-built app...
echo.

:: Ensure GitHub CLI is available (needed for private-repo downloads).
where gh >nul 2>&1
if %errorlevel% neq 0 (
    echo  Installing GitHub CLI...
    winget install --id GitHub.cli -e --silent --accept-source-agreements --accept-package-agreements >nul 2>&1
    set "PATH=%PATH%;%ProgramFiles%\GitHub CLI"
)

:: Ensure we're signed in (private repo).
gh auth status >nul 2>&1
if %errorlevel% neq 0 (
    echo  One-time GitHub sign-in required...
    gh auth login --hostname github.com --git-protocol https --web
)

:: 2) Try the latest published release asset.
set "ZIPFILE=%TEMP%\DuneBox-windows-x64.zip"
if exist "%ZIPFILE%" del "%ZIPFILE%" >nul 2>&1
gh release download --repo %REPO% --pattern "%ARTIFACT%.zip" --output "%ZIPFILE%" >nul 2>&1
if exist "%ZIPFILE%" goto :extract

:: 3) Fall back to the artifact from the latest successful build (no tag needed).
echo  No release yet - checking latest successful CI build...
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
:: Expand, then strip Mark-of-the-Web from every extracted file. Downloaded
:: zips tag their contents with MOTW, which Smart App Control / SmartScreen use
:: to block unsigned binaries (the app is an unsigned OpenFrameworks build).
:: Unblock-File removes that tag so the freshly-downloaded exe launches normally.
powershell -NoProfile -Command "Expand-Archive -Path '%ZIPFILE%' -DestinationPath '%BINDIR%' -Force; Get-ChildItem -LiteralPath '%BINDIR%' -Recurse -File | Unblock-File"
del "%ZIPFILE%" >nul 2>&1
if not exist "%EXE%" (
    echo  Extract failed - Magic-Sand.exe not found.
    pause
    exit /b 1
)

:run
echo  Starting DuneBox...
cd /d "%BINDIR%"
start "" "Magic-Sand.exe"
exit /b 0
