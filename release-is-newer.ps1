# Exit 0 when the latest published release is newer than bin\data\VERSION.
# Exit 1 when it is not, or when the check cannot be completed.
# A missing or unreadable VERSION counts as 0.0.0, so an old install updates.
param(
    [Parameter(Mandatory = $true)][string]$Repo,
    [Parameter(Mandatory = $true)][string]$BinDir
)

$ErrorActionPreference = "Stop"

function ConvertTo-ReleaseVersion([string]$Text) {
    if ($Text -match '(\d+)\.(\d+)\.(\d+)') {
        return [version]::new([int]$Matches[1], [int]$Matches[2], [int]$Matches[3])
    }
    return $null
}

$local = $null
$versionFile = Join-Path $BinDir "data\VERSION"
if (Test-Path -LiteralPath $versionFile) {
    $line = Get-Content -LiteralPath $versionFile -TotalCount 1
    if ($line) { $local = ConvertTo-ReleaseVersion $line.Trim() }
}
if (-not $local) { $local = [version]::new(0, 0, 0) }

# A hung GitHub call must not keep a kiosk from starting.
$outFile = Join-Path $env:TEMP "dunebox-release-tag.txt"
$errFile = Join-Path $env:TEMP "dunebox-release-tag.err"
Remove-Item -LiteralPath $outFile, $errFile -Force -ErrorAction SilentlyContinue
$proc = Start-Process -FilePath "gh" -ArgumentList @(
    "release", "view", "--repo", $Repo, "--json", "tagName", "--jq", ".tagName"
) -PassThru -WindowStyle Hidden -RedirectStandardOutput $outFile -RedirectStandardError $errFile
if (-not $proc.WaitForExit(20000)) {
    try { $proc.Kill() } catch {}
    exit 1
}
if ($proc.ExitCode -ne 0) { exit 1 }
if (-not (Test-Path -LiteralPath $outFile)) { exit 1 }
$tag = Get-Content -LiteralPath $outFile -Raw
if (-not $tag) { exit 1 }
$remote = ConvertTo-ReleaseVersion $tag.Trim()
if (-not $remote) { exit 1 }
if ($remote -gt $local) { exit 0 }
exit 1
