param(
    [string]$Version,
    [string]$InstallDir = "$HOME\\.local\\bin",
    [string]$Repository = "nburrus/zv"
)

$ErrorActionPreference = "Stop"
if (-not $Version) {
    $Version = (Invoke-WebRequest -MaximumRedirection 0 -ErrorAction SilentlyContinue "https://github.com/$Repository/releases/latest").Headers.Location.Split('/')[-1]
}
if (-not $Version.StartsWith('v')) { throw "Version must be a Git tag such as v0.2.0" }

$archive = "zv-$Version-windows-x86_64.zip"
$baseUrl = "https://github.com/$Repository/releases/download/$Version"
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("zv-install-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    Invoke-WebRequest "$baseUrl/$archive" -OutFile "$tmp/$archive"
    Invoke-WebRequest "$baseUrl/SHA256SUMS" -OutFile "$tmp/SHA256SUMS"
    $expected = ((Get-Content "$tmp/SHA256SUMS") | Where-Object { $_ -match [regex]::Escape($archive) } | Select-Object -First 1).Split()[0]
    if ((Get-FileHash "$tmp/$archive" -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected.ToLowerInvariant()) { throw "Checksum verification failed" }
    Expand-Archive "$tmp/$archive" -DestinationPath $tmp
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    Copy-Item "$tmp/zv.exe" "$InstallDir/zv.exe" -Force
    Write-Host "Installed $InstallDir/zv.exe"
} finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
