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
        # robocopy uses 0-7 for success (including "files copied").
        & robocopy $data.FullName $destData /E /XC /XN /XO /NFL /NDL /NJH /NJS /nc /ns /np | Out-Null
        if ($LASTEXITCODE -ge 8) {
            throw "robocopy failed with exit $LASTEXITCODE"
        }
    }
    Get-ChildItem -LiteralPath $BinDir -Recurse -File | Unblock-File
} finally {
    if (Test-Path -LiteralPath $tmp) {
        Remove-Item -LiteralPath $tmp -Recurse -Force
    }
}
