<#
.SYNOPSIS
    Downloads and extracts the official Xbox Wireless Adapter firmware from Microsoft Windows Update.
.DESCRIPTION
    Proprietary firmware cannot be redistributed directly. This script downloads the official
    Microsoft driver package from Microsoft's CDN, extracts FW_ACC_00U.bin, verifies its
    SHA-256 hash, and installs it locally.
#>

[CmdletBinding()]
param(
    [string]$DestinationDir = ""
)

$ErrorActionPreference = "Stop"

$DriverUrl = "http://download.windowsupdate.com/c/msdownload/update/driver/drvs/2017/07/1cd6a87c-623f-4407-a52d-c31be49e925c_e19f60808bdcbfbd3c3df6be3e71ffc52e43261e.cab"
$ExpectedHash = "48084D9FA53B9BB04358F3BB127B7495DC8F7BB0B3CA1437BD24EF2B6EABDF66"

if ([string]::IsNullOrWhiteSpace($DestinationDir)) {
    $DestinationDir = Resolve-Path "$PSScriptRoot\..\firmware"
}

$TempCab = Join-Path $env:TEMP "xbox_driver_$([System.Guid]::NewGuid().ToString('N')).cab"
$TempExtract = Join-Path $env:TEMP "xbox_fw_$([System.Guid]::NewGuid().ToString('N'))"

try {
    Write-Host "[*] Downloading official driver package from Microsoft Update CDN..." -ForegroundColor Cyan
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $DriverUrl -OutFile $TempCab -UseBasicParsing

    Write-Host "[*] Extracting FW_ACC_00U.bin using expand.exe..." -ForegroundColor Cyan
    New-Item -ItemType Directory -Path $TempExtract -Force | Out-Null
    
    $expandResult = & expand.exe "$TempCab" -F:FW_ACC_00U.bin "$TempExtract"
    $extractedFw = Join-Path $TempExtract "FW_ACC_00U.bin"

    if (-not (Test-Path $extractedFw)) {
        throw "Failed to extract FW_ACC_00U.bin from the downloaded cabinet."
    }

    Write-Host "[*] Verifying SHA-256 checksum..." -ForegroundColor Cyan
    $actualHash = (Get-FileHash -Path $extractedFw -Algorithm SHA256).Hash
    if ($actualHash -ne $ExpectedHash) {
        throw "SHA-256 verification failed! Expected: $ExpectedHash, Got: $actualHash"
    }

    Write-Host "[+] Integrity verified: $actualHash" -ForegroundColor Green

    # Ensure target directory exists
    if (-not (Test-Path $DestinationDir)) {
        New-Item -ItemType Directory -Path $DestinationDir -Force | Out-Null
    }

    $finalPath = Join-Path $DestinationDir "FW_ACC_00U.bin"
    Copy-Item -Path $extractedFw -Destination $finalPath -Force
    Write-Host "[+] Firmware saved to: $finalPath" -ForegroundColor Green

    return $finalPath
}
finally {
    if (Test-Path $TempCab) { Remove-Item $TempCab -Force -ErrorAction SilentlyContinue }
    if (Test-Path $TempExtract) { Remove-Item $TempExtract -Recurse -Force -ErrorAction SilentlyContinue }
}
