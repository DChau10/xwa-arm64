<#
.SYNOPSIS
    One-click installer for xow-win (Xbox Wireless Adapter on Windows / ARM64).
.DESCRIPTION
    1. Ensures administrator rights.
    2. Installs ViGEmBus driver if not already present.
    3. Binds Microsoft WinUSB to the Xbox Wireless Adapter using pnputil.
    4. Downloads/extracts official firmware from Microsoft Update into %ProgramData%\xow-win.
    5. Sets up automatic launch on startup.
#>

[CmdletBinding()]
param(
    [switch]$SkipViGEm,
    [switch]$NoService
)

$ErrorActionPreference = "Stop"

# 1. Require Administrator
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "[xow-win] Elevating to Administrator..." -ForegroundColor Cyan
    Start-Process powershell.exe -ArgumentList ("-NoProfile -ExecutionPolicy Bypass -File `"{0}`"" -f $MyInvocation.MyCommand.Path) -Verb RunAs
    exit
}

Write-Host "==========================================================" -ForegroundColor Green
Write-Host "  xow-win: Xbox Wireless Adapter Setup for Windows" -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Green
Write-Host "Detected Architecture: $env:PROCESSOR_ARCHITECTURE" -ForegroundColor Yellow

$RepoRoot = Resolve-Path "$PSScriptRoot\.."
$DriverInf = Join-Path $RepoRoot "driver\xow_dongle.inf"
$FirmwareSrc = Join-Path $RepoRoot "firmware\FW_ACC_00U.bin"
$ProgramDataDir = Join-Path $env:ProgramData "xow-win"

# 2. Check and Install ViGEmBus
if (-not $SkipViGEm) {
    $viGemService = Get-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Services\ViGEmBus" -ErrorAction SilentlyContinue
    if ($viGemService) {
        Write-Host "[+] ViGEmBus driver is already installed." -ForegroundColor Green
    } else {
        Write-Host "[*] ViGEmBus not found. Downloading signed ViGEmBus installer..." -ForegroundColor Cyan
        $viGemUrl = "https://github.com/nefarius/ViGEmBus/releases/download/v1.22.0/ViGEmBus_Setup_1.22.0.exe"
        $installerPath = Join-Path $env:TEMP "ViGEmBus_Setup.exe"
        
        try {
            [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
            Invoke-WebRequest -Uri $viGemUrl -OutFile $installerPath -UseBasicParsing
            Write-Host "[*] Running ViGEmBus installer..." -ForegroundColor Cyan
            Start-Process -FilePath $installerPath -ArgumentList "/passive" -Wait
            Write-Host "[+] ViGEmBus installation finished." -ForegroundColor Green
        } catch {
            Write-Warning "Failed to download ViGEmBus automatically: $_"
            Write-Host "Please download and install ViGEmBus manually from: https://github.com/nefarius/ViGEmBus/releases" -ForegroundColor Yellow
        }
    }
}

# 3. Install WinUSB driver for Xbox Wireless Adapter via pnputil
Write-Host "[*] Installing WinUSB driver for Xbox Wireless Adapter..." -ForegroundColor Cyan
if (Test-Path $DriverInf) {
    $pnpOutput = pnputil /add-driver "$DriverInf" /install
    $pnpOutput | ForEach-Object { Write-Host "    $_" }
    Write-Host "[+] Adapter WinUSB driver package registered." -ForegroundColor Green
} else {
    Write-Error "Could not find driver INF at $DriverInf"
}

# 4. Acquire Firmware (Automated download from Microsoft if not present)
Write-Host "[*] Setting up firmware..." -ForegroundColor Cyan
if (-not (Test-Path $ProgramDataDir)) {
    New-Item -ItemType Directory -Path $ProgramDataDir -Force | Out-Null
}

$targetFw = Join-Path $ProgramDataDir "FW_ACC_00U.bin"
if (Test-Path $targetFw) {
    Write-Host "[+] Firmware is already present in $targetFw" -ForegroundColor Green
} elseif (Test-Path $FirmwareSrc) {
    Copy-Item $FirmwareSrc -Destination $targetFw -Force
    Write-Host "[+] Firmware copied from local folder to $targetFw" -ForegroundColor Green
} else {
    Write-Host "[*] Local firmware not found. Fetching official firmware from Microsoft Update CDN..." -ForegroundColor Cyan
    $getFwScript = Join-Path $PSScriptRoot "get-firmware.ps1"
    & $getFwScript -DestinationDir $ProgramDataDir
}

# 5. Startup Configuration
if (-not $NoService) {
    $binPath = Join-Path $RepoRoot "bin\xow-win.exe"
    if (Test-Path $binPath) {
        $startupDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\Startup"
        $shortcutPath = Join-Path $startupDir "xow-win.lnk"
        
        $wsh = New-Object -ComObject WScript.Shell
        $shortcut = $wsh.CreateShortcut($shortcutPath)
        $shortcut.TargetPath = $binPath
        $shortcut.WorkingDirectory = (Split-Path $binPath)
        $shortcut.Description = "xow-win Background Controller Service"
        $shortcut.Save()
        Write-Host "[+] Created Startup shortcut at $shortcutPath" -ForegroundColor Green
    } else {
        Write-Host "[!] Note: Binary not found at $binPath yet. Run cmake build first." -ForegroundColor Yellow
    }
}

Write-Host "`nSetup complete! You can now plug in your Xbox Wireless Adapter." -ForegroundColor Green
Write-Host "Press any key to exit..."
[void][System.Console]::ReadKey()
