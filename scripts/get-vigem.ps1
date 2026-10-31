<#
.SYNOPSIS
    Downloads and verifies the official ViGEmBus installer (unified ARM64 / x64 / x86).
.DESCRIPTION
    ViGEmBus is the Virtual Gamepad Emulation Bus required by xwa-arm64 to create
    virtual Xbox 360 controllers in Windows. This script downloads the official
    signed installer from GitHub and verifies its SHA-256 integrity checksum.
#>

[CmdletBinding()]
param(
    [string]$DestinationDir = "",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$ViGemUrl = "https://github.com/nefarius/ViGEmBus/releases/download/v1.22.0/ViGEmBus_1.22.0_x64_x86_arm64.exe"
$ExpectedHash = "89220A7865076B342892F98865F3499FB7C4CFD673159E89D352C360FD014C6A"
$FileName = "ViGEmBus_1.22.0_x64_x86_arm64.exe"

if ([string]::IsNullOrWhiteSpace($DestinationDir)) {
    $DestinationDir = Resolve-Path "$PSScriptRoot\.."
}

if (-not (Test-Path $DestinationDir)) {
    New-Item -ItemType Directory -Path $DestinationDir -Force | Out-Null
}

$targetPath = Join-Path $DestinationDir $FileName

# Check if already present in target location and valid
if (-not $Force -and (Test-Path $targetPath)) {
    $existingHash = (Get-FileHash -Path $targetPath -Algorithm SHA256).Hash
    if ($existingHash -eq $ExpectedHash) {
        Write-Host "[+] ViGEmBus installer already present and verified: $targetPath" -ForegroundColor Green
        return $targetPath
    } else {
        Write-Warning "Existing installer hash mismatch. Re-downloading..."
    }
}

# Check temp cache as well
$tempCached = Join-Path $env:TEMP $FileName
if (-not $Force -and (Test-Path $tempCached)) {
    $cachedHash = (Get-FileHash -Path $tempCached -Algorithm SHA256).Hash
    if ($cachedHash -eq $ExpectedHash) {
        Write-Host "[+] Using verified ViGEmBus installer from temp cache..." -ForegroundColor Green
        Copy-Item -Path $tempCached -Destination $targetPath -Force
        return $targetPath
    }
}

Write-Host "[*] Downloading signed ViGEmBus installer from GitHub..." -ForegroundColor Cyan
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Invoke-WebRequest -Uri $ViGemUrl -OutFile $targetPath -UseBasicParsing

Write-Host "[*] Verifying SHA-256 checksum..." -ForegroundColor Cyan
$actualHash = (Get-FileHash -Path $targetPath -Algorithm SHA256).Hash
if ($actualHash -ne $ExpectedHash) {
    Remove-Item -Path $targetPath -Force -ErrorAction SilentlyContinue
    throw "SHA-256 verification failed! Expected: $ExpectedHash, Got: $actualHash"
}

Write-Host "[+] Integrity verified: $actualHash" -ForegroundColor Green
Write-Host "[+] ViGEmBus installer saved to: $targetPath" -ForegroundColor Green

return $targetPath
