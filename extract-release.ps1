# Extract a DuneBox release zip into bin\ without clobbering live settings.
# Usage: extract-release.ps1 <zip> <bindir>
param(
    [Parameter(Mandatory = $true)][string]$Zip,
    [Parameter(Mandatory = $true)][string]$BinDir
)
$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $Zip)) {
    throw "Release zip not found: $Zip"
}
if (-not (Test-Path -LiteralPath $BinDir)) {
    New-Item -ItemType Directory -Path $BinDir | Out-Null
}
$tmp = Join-Path $env:TEMP "dunebox-extract"
if (Test-Path -LiteralPath $tmp) {
    Remove-Item -LiteralPath $tmp -Recurse -Force
}
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    Expand-Archive -LiteralPath $Zip -DestinationPath $tmp -Force
    Get-ChildItem -LiteralPath $tmp -Recurse -File | Where-Object {
        $_.Extension -in ".exe", ".dll"
    } | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $BinDir -Force
    }
    $data = Get-ChildItem -LiteralPath $tmp -Recurse -Directory -Filter data |
        Select-Object -First 1
    if ($data) {
        $destData = Join-Path $BinDir "data"
        $srcSettings = Join-Path $data.FullName "settings"
        $destSettings = Join-Path $destData "settings"
        # Refresh shaders / color maps / fonts. Never overwrite live settings.
        Get-ChildItem -LiteralPath $data.FullName -Force | Where-Object {
            $_.Name -ne "settings"
        } | ForEach-Object {
            $destItem = Join-Path $destData $_.Name
            if ($_.PSIsContainer) {
                & robocopy $_.FullName $destItem /E /NFL /NDL /NJH /NJS /nc /ns /np | Out-Null
                if ($LASTEXITCODE -ge 8) { throw "robocopy failed with exit $LASTEXITCODE" }
            } else {
                Copy-Item -LiteralPath $_.FullName -Destination $destItem -Force
            }
        }
        if (Test-Path -LiteralPath $srcSettings) {
            & robocopy $srcSettings $destSettings /E /XC /XN /XO /NFL /NDL /NJH /NJS /nc /ns /np | Out-Null
            if ($LASTEXITCODE -ge 8) { throw "robocopy failed with exit $LASTEXITCODE" }
        }
    }
    Get-ChildItem -LiteralPath $BinDir -Recurse -File | Unblock-File
} finally {
    if (Test-Path -LiteralPath $tmp) {
        Remove-Item -LiteralPath $tmp -Recurse -Force
    }
}
