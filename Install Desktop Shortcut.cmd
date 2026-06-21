@echo off
:: One-click setup: puts the DuneBox Magic-Sand C++ shortcut on your desktop.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-desktop-shortcut.ps1"
echo.
pause
