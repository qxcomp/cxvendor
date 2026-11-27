# Install libretv, enabling the nodejs22 feature only when no usable
# system Node.js is available.
param(
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$Triplet = "",
    [string]$HostTriplet = ""
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$overlayDir = Split-Path -Parent $scriptDir

if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    foreach ($candidate in @(
        (Join-Path (Get-Location) 'vcpkg'),
        (Join-Path $env:USERPROFILE '.vcpkg'),
        'C:\vcpkg', 'C:\src\vcpkg')) {
        if (Test-Path (Join-Path $candidate 'vcpkg.exe')) {
            $VcpkgRoot = $candidate
            break
        }
    }
}
if ([string]::IsNullOrWhiteSpace($VcpkgRoot) -or
    -not (Test-Path (Join-Path $VcpkgRoot 'vcpkg.exe'))) {
    throw 'Cannot find vcpkg.exe; set VCPKG_ROOT or pass -VcpkgRoot.'
}
$vcpkg = Join-Path $VcpkgRoot 'vcpkg.exe'

if ([string]::IsNullOrWhiteSpace($Triplet)) {
    switch ($env:PROCESSOR_ARCHITECTURE) {
        'AMD64' { $Triplet = 'x64-windows' }
        'ARM64' { $Triplet = 'arm64-windows' }
        default { throw 'Cannot detect triplet; pass -Triplet.' }
    }
}
if ([string]::IsNullOrWhiteSpace($HostTriplet)) {
    $HostTriplet = $Triplet
}

# LibreTV requires Node.js 18+ at runtime.
function Test-SystemNode {
    foreach ($dir in ($env:PATH -split ';')) {
        if ([string]::IsNullOrWhiteSpace($dir)) { continue }
        foreach ($name in @('node.exe', 'node')) {
            $bin = Join-Path $dir $name
            if (-not (Test-Path $bin -PathType Leaf)) { continue }
            try {
                $raw = & $bin -p process.versions.node 2>$null
            } catch {
                continue
            }
            if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($raw)) { continue }
            $v = $raw.Trim()
            $parts = $v.Split('.')
            if ($parts.Count -lt 2) { continue }
            $major = [int]$parts[0]
            $minor = [int]$parts[1]
            if (($major -eq 18 -and $minor -ge 17) -or
                ($major -eq 20 -and $minor -ge 3) -or
                ($major -ge 21)) {
                return $v
            }
        }
    }
    return $null
}

$version = Test-SystemNode
if ($version) {
    Write-Host "System Node $version is usable; installing libretv without nodejs22."
    $spec = "libretv:$Triplet"
} else {
    Write-Host 'No usable system Node found; enabling the nodejs22 feature.'
    $spec = "libretv[nodejs22]:$Triplet"
}

& $vcpkg install $spec "--overlay-ports=$overlayDir" "--host-triplet=$HostTriplet"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
