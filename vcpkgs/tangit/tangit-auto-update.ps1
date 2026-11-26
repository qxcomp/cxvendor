$ErrorActionPreference = 'Stop'

# Keep lazygitGo's updater preference off by default. MyGo stores the choice
# in updater.json under PathUserData; an absent automaticChecks field means
# automatic checks are enabled, so the launchers seed it before starting.
if (-not $env:APPDATA) {
    exit 0
}

switch ($env:TANGIT_AUTO_UPDATE) {
    '1' { $want = 'true'; $explicit = $true }
    '0' { $want = 'false'; $explicit = $true }
    default { $want = 'false'; $explicit = $false }
}

$dir = Join-Path $env:APPDATA 'lazygitGo'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$cfg = Join-Path $dir 'updater.json'

if (-not (Test-Path $cfg)) {
    Set-Content -Path $cfg -NoNewline -Value ('{"automaticChecks":' + $want + '}')
    exit 0
}

$text = Get-Content -Path $cfg -Raw
if ($text -match '"automaticChecks"\s*:\s*(true|false)') {
    if ($explicit) {
        $text = $text -replace '("automaticChecks"\s*:\s*)(true|false)', ('${1}' + $want)
        Set-Content -Path $cfg -NoNewline -Value $text
    }
    exit 0
}

if ($text -match '^\s*\{') {
    $text = $text -replace '^(\s*\{\s*)', ('${1}"automaticChecks":' + $want + ',')
} else {
    $text = '{"automaticChecks":' + $want + '}'
}
Set-Content -Path $cfg -NoNewline -Value $text
