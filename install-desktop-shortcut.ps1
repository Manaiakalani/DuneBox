<#
.SYNOPSIS
    Installs (or refreshes) a desktop shortcut for the DuneBox Magic-Sand C++ app.
    Safe to re-run.

.DESCRIPTION
    Creates a "DuneBox (Magic-Sand C++)" desktop shortcut that runs run.bat,
    which launches bin\Magic-Sand.exe (auto-downloading the prebuilt binary from
    the GitHub release / latest successful CI build if it isn't present yet).

    If you also have the DuneBox-sandcam repo checked out next to this one, its
    install-desktop-shortcuts.ps1 is the preferred entry point — it sets up both
    the primary "DuneBox" icon and this C++ one together.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File .\install-desktop-shortcut.ps1
#>
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = $PSScriptRoot
$Desktop  = [Environment]::GetFolderPath('Desktop')
$RunBat   = Join-Path $RepoRoot 'run.bat'
$IconPath = Join-Path $RepoRoot 'icon.ico'

if (-not (Test-Path $RunBat)) { throw "Missing launcher: $RunBat" }

$Shell    = New-Object -ComObject WScript.Shell
$linkPath = Join-Path $Desktop 'DuneBox (Magic-Sand C++).lnk'
$sc = $Shell.CreateShortcut($linkPath)
$sc.TargetPath       = $RunBat
$sc.WorkingDirectory = $RepoRoot
$sc.WindowStyle      = 7          # launch the console minimized — stays clean
$sc.Description      = 'DuneBox Magic-Sand C++ renderer'
if (Test-Path $IconPath) { $sc.IconLocation = "$IconPath,0" }
$sc.Save()

Write-Host "Installed desktop shortcut: $linkPath"
Write-Host "Double-click 'DuneBox (Magic-Sand C++)' on your desktop to run."
