<#
.SYNOPSIS
    One-click automated installer for xwa-arm64 (Xbox Wireless Adapter on Windows ARM64 / x64).
.DESCRIPTION
    1. Ensures administrator rights (elevates automatically).
    2. Automatically downloads and installs signed ViGEmBus driver if not present.
    3. Registers WinUSB driver for the Xbox Wireless Adapter.
    4. Downloads/extracts official firmware from Microsoft Update into %ProgramData%\xwa-arm64.
    5. Sets up automatic launch on startup.
#>

[CmdletBinding()]
param(
    [switch]$SkipViGEm,
    [switch]$NoService
)

$ErrorActionPreference = "Continue"

# 1. Require Administrator
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "[xwa-arm64] Elevating to Administrator..." -ForegroundColor Cyan
    Start-Process powershell.exe -ArgumentList ("-NoProfile -ExecutionPolicy Bypass -File `"{0}`"" -f $MyInvocation.MyCommand.Path) -Verb RunAs
    exit
}

Write-Host "==========================================================" -ForegroundColor Green
Write-Host "  xwa-arm64: Xbox Wireless Adapter Setup for Windows" -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Green
Write-Host "Detected Architecture: $env:PROCESSOR_ARCHITECTURE" -ForegroundColor Yellow

$RepoRoot = Resolve-Path "$PSScriptRoot\.."
$DriverInf = Join-Path $RepoRoot "driver\xow_dongle.inf"
$FirmwareSrc = Join-Path $RepoRoot "firmware\FW_ACC_00U.bin"
$ProgramDataDir = Join-Path $env:ProgramData "xwa-arm64"

# 2. Check and Install ViGEmBus
if (-not $SkipViGEm) {
    Write-Host "`n[*] Step 1/4: Checking ViGEmBus virtual controller driver..." -ForegroundColor Cyan
    $isInstalled = (Test-Path "$env:SystemRoot\System32\drivers\ViGEmBus.sys") -or 
                   (Get-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Services\ViGEmBus" -ErrorAction SilentlyContinue)

    if ($isInstalled) {
        Write-Host "[+] ViGEmBus driver is already installed." -ForegroundColor Green
    } else {
        Write-Host "[*] ViGEmBus not found. Acquiring signed installer via get-vigem.ps1..." -ForegroundColor Cyan
        
        $getViGemScript = Join-Path $PSScriptRoot "get-vigem.ps1"
        $installerPath = $null
        if (Test-Path $getViGemScript) {
            $installerPath = & $getViGemScript -DestinationDir $RepoRoot
        } else {
            Write-Warning "Could not find get-vigem.ps1 at $getViGemScript"
        }

        if ($installerPath -and (Test-Path $installerPath)) {
            Write-Host "[*] Running ViGEmBus driver setup..." -ForegroundColor Cyan
            $proc = Start-Process -FilePath $installerPath -ArgumentList "/passive /norestart" -Wait -PassThru
            
            Start-Sleep -Seconds 3
            $verifyInstalled = (Test-Path "$env:SystemRoot\System32\drivers\ViGEmBus.sys") -or 
                               (Get-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Services\ViGEmBus" -ErrorAction SilentlyContinue)
            
            if ($verifyInstalled) {
                Write-Host "[+] ViGEmBus installed and verified successfully!" -ForegroundColor Green
                Start-Service ViGEmBus -ErrorAction SilentlyContinue
            } else {
                Write-Warning "ViGEmBus installer finished with exit code $($proc.ExitCode). If prompt appeared, ensure installation was confirmed."
            }
        } else {
            Write-Warning "Failed to acquire ViGEmBus installer."
            Write-Host "Please download and install ViGEmBus manually from: https://github.com/nefarius/ViGEmBus/releases" -ForegroundColor Yellow
        }
    }
}

# 3. Register WinUSB driver for Xbox Wireless Adapter
Write-Host "`n[*] Step 2/4: Configuring Xbox Wireless Adapter USB driver..." -ForegroundColor Cyan
if (Test-Path $DriverInf) {
    try {
        $pnpOutput = pnputil /add-driver "$DriverInf" /install
        $pnpOutput | ForEach-Object { Write-Host "    $_" }
        Write-Host "[+] Adapter driver registration attempted." -ForegroundColor Green
    } catch {
        Write-Warning "Driver registration note: $_"
    }
    
    # Check if adapter is connected and bound
    $adapter = Get-PnpDevice -FriendlyName "*Xbox*Wireless*", "*XBOX ACC*" -ErrorAction SilentlyContinue
    if ($adapter) {
        Write-Host "[+] Found Xbox Wireless Adapter: $($adapter.FriendlyName) [Status: $($adapter.Status)]" -ForegroundColor Green
    } else {
        Write-Host "[*] Note: Plug in your Xbox Wireless Adapter if you haven't already." -ForegroundColor Yellow
    }
} else {
    Write-Warning "Could not find driver INF at $DriverInf"
}

# 4. Acquire Firmware (Automated download from Microsoft CDN if not present)
Write-Host "`n[*] Step 3/4: Setting up adapter firmware..." -ForegroundColor Cyan
if (-not (Test-Path $ProgramDataDir)) {
    New-Item -ItemType Directory -Path $ProgramDataDir -Force | Out-Null
}

$targetFw = Join-Path $ProgramDataDir "FW_ACC_00U.bin"
if (Test-Path $targetFw) {
    Write-Host "[+] Firmware is already present in $targetFw" -ForegroundColor Green
} elseif (Test-Path $FirmwareSrc) {
    Copy-Item $FirmwareSrc -Destination $targetFw -Force
    Write-Host "[+] Firmware copied to $targetFw" -ForegroundColor Green
} else {
    Write-Host "[*] Fetching official firmware from Microsoft Update CDN..." -ForegroundColor Cyan
    $getFwScript = Join-Path $PSScriptRoot "get-firmware.ps1"
    if (Test-Path $getFwScript) {
        & $getFwScript -DestinationDir $ProgramDataDir
    } else {
        Write-Warning "Could not find get-firmware.ps1 at $getFwScript"
    }
}

# 5. Startup Configuration
if (-not $NoService) {
    Write-Host "`n[*] Step 4/4: Setting up startup service..." -ForegroundColor Cyan
    $binPath = Join-Path $RepoRoot "bin\xwa-arm64.exe"
    if (Test-Path $binPath) {
        $startupDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\Startup"
        $shortcutPath = Join-Path $startupDir "xwa-arm64.lnk"
        
        $wsh = New-Object -ComObject WScript.Shell
        $shortcut = $wsh.CreateShortcut($shortcutPath)
        $shortcut.TargetPath = $binPath
        $shortcut.WorkingDirectory = (Split-Path $binPath)
        $shortcut.Description = "xwa-arm64 Background Controller Service"
        $shortcut.Save()
        Write-Host "[+] Created Startup shortcut at $shortcutPath" -ForegroundColor Green
    } else {
        Write-Host "[!] Note: Binary not found at $binPath yet. Build project first." -ForegroundColor Yellow
    }
}

Write-Host "`n==========================================================" -ForegroundColor Green
Write-Host "  Setup Complete! xwa-arm64 is ready to use." -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Green
Write-Host "1. Plug in your Xbox Wireless Adapter (if not already plugged in)."
Write-Host "2. Run xwa-arm64 or double-click your desktop / startup shortcut."
Write-Host "3. Turn on your controller and press sync to play!"
Write-Host ""
Write-Host "Press any key to exit..."
[void][System.Console]::ReadKey()
