<#
.SYNOPSIS
    Acquires the official Xbox Wireless Adapter firmware (FW_ACC_00U.bin).
.DESCRIPTION
    Searches local and Windows DriverStore sources first (offline), then downloads
    the official Microsoft driver cabinet package from Microsoft Windows Update CDN.
    The firmware is hash-verified against the official Microsoft SHA-256 hashes before
    it is accepted. Proprietary firmware cannot be redistributed directly; this retrieves
    it directly from official Microsoft sources, verifying integrity.
#>
[CmdletBinding()]
param(
    [string]$DestinationDir = ""
)

$ErrorActionPreference = "Stop"

# Official Microsoft firmware hashes for FW_ACC_00U.bin:
#   1. Canonical current firmware (v1.0.46.x, 70,620 bytes)
#   2. Original release firmware  (v1.0.0.x, 70,008 bytes)
$ExpectedHashes = @(
    "48084D9FA53B9BB04358F3BB127B7495DC8F7BB0B3CA1437BD24EF2B6EABDF66",
    "080CE4091E53A4EF3E5FE29939F51FD91F46D6A88BE6D67EB6E99A5723B3A223"
)

if ([string]::IsNullOrWhiteSpace($DestinationDir)) {
    $DestinationDir = Resolve-Path "$PSScriptRoot\.."
}

function Test-Hash {
    param([string]$File, [string[]]$Trusted)
    if (-not (Test-Path $File)) { return $false }
    $actual = (Get-FileHash -Path $File -Algorithm SHA256).Hash
    return ($Trusted -contains $actual)
}

if (-not (Test-Path $DestinationDir)) {
    New-Item -ItemType Directory -Path $DestinationDir -Force | Out-Null
}
$finalPath = Join-Path $DestinationDir "FW_ACC_00U.bin"

# --- Source 1: already present in destination ---
if (Test-Hash -File $finalPath -Trusted $ExpectedHashes) {
    Write-Host "[+] Firmware already present and verified: $finalPath" -ForegroundColor Green
    return $finalPath
}

# --- Source 2: local file next to this script / repo root ---
$localFw = Join-Path (Split-Path $MyInvocation.MyCommand.Path) "FW_ACC_00U.bin"
if (Test-Hash -File $localFw -Trusted $ExpectedHashes) {
    Copy-Item -Path $localFw -Destination $finalPath -Force
    Write-Host "[+] Firmware copied from local source: $localFw" -ForegroundColor Green
    return $finalPath
}

# --- Source 3: Windows DriverStore / FileRepository (offline) ---
Write-Host "[*] Searching Windows DriverStore for FW_ACC_00U.bin..." -ForegroundColor Cyan
$storeRoots = @(
    "C:\Windows\System32\DriverStore\FileRepository",
    "C:\Windows\System32\drivers",
    "C:\Windows\System32\DriverStore"
)
$foundStore = $null
foreach ($r in $storeRoots) {
    $foundStore = Get-ChildItem $r -Recurse -Filter "FW_ACC_00U.bin" -ErrorAction SilentlyContinue |
        Where-Object { Test-Hash -File $_.FullName -Trusted $ExpectedHashes } |
        Select-Object -First 1
    if ($foundStore) { break }
}
if ($foundStore) {
    Copy-Item -Path $foundStore.FullName -Destination $finalPath -Force
    Write-Host "[+] Firmware extracted from DriverStore: $($foundStore.FullName)" -ForegroundColor Green
    return $finalPath
}

# --- Source 4: official Microsoft Windows Update CDN ---
$DriverUrl = "http://download.windowsupdate.com/c/msdownload/update/driver/drvs/2017/07/1cd6a87c-623f-4407-a52d-c31be49e925c_e19f60808bdcbfbd3c3df6be3e71ffc52e43261e.cab"
$TempCab = Join-Path $env:TEMP "xbox_driver_$([System.Guid]::NewGuid().ToString('N')).cab"
$TempExtract = Join-Path $env:TEMP "xbox_fw_$([System.Guid]::NewGuid().ToString('N'))"

try {
    Write-Host "[*] Downloading official driver package from Microsoft Windows Update CDN..." -ForegroundColor Cyan
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $DriverUrl -OutFile $TempCab -UseBasicParsing -TimeoutSec 90

    Write-Host "[*] Extracting FW_ACC_00U.bin using expand.exe..." -ForegroundColor Cyan
    New-Item -ItemType Directory -Path $TempExtract -Force | Out-Null
    & expand.exe "$TempCab" -F:FW_ACC_00U.bin "$TempExtract" | Out-Null
    $extractedFw = Join-Path $TempExtract "FW_ACC_00U.bin"

    if (Test-Hash -File $extractedFw -Trusted $ExpectedHashes) {
        Copy-Item -Path $extractedFw -Destination $finalPath -Force
        Write-Host "[+] Integrity verified, firmware saved to: $finalPath" -ForegroundColor Green
        return $finalPath
    } else {
        throw "Downloaded package did not contain a valid or verified FW_ACC_00U.bin."
    }
} catch {
    Write-Host "    Download/extraction failed: $($_.Exception.Message)" -ForegroundColor Yellow
} finally {
    if (Test-Path $TempCab) { Remove-Item $TempCab -Force -ErrorAction SilentlyContinue }
    if (Test-Path $TempExtract) { Remove-Item $TempExtract -Recurse -Force -ErrorAction SilentlyContinue }
}

Write-Host "ERROR: Could not acquire official firmware (FW_ACC_00U.bin)." -ForegroundColor Red
Write-Host "Please place the official firmware at: $finalPath" -ForegroundColor Red
Write-Host "(Obtain from Microsoft's 'Xbox Wireless Adapter for Windows' driver package.)" -ForegroundColor Red
exit 1
