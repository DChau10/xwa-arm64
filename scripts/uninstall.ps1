<#
.SYNOPSIS
    Uninstaller for xow-win (Xbox Wireless Adapter on Windows / ARM64).
#>

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

# Require Administrator
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "[xow-win] Elevating to Administrator..." -ForegroundColor Cyan
    Start-Process powershell.exe -ArgumentList ("-NoProfile -ExecutionPolicy Bypass -File `"{0}`"" -f $MyInvocation.MyCommand.Path) -Verb RunAs
    exit
}

Write-Host "==========================================================" -ForegroundColor Red
Write-Host "  xow-win: Uninstalling Xbox Wireless Adapter Setup" -ForegroundColor Red
Write-Host "==========================================================" -ForegroundColor Red

# 1. Stop any running xow-win process
$proc = Get-Process -Name "xow-win" -ErrorAction SilentlyContinue
if ($proc) {
    Write-Host "[*] Stopping running xow-win process..." -ForegroundColor Cyan
    Stop-Process -Name "xow-win" -Force
}

# 2. Remove Startup shortcut
$startupDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\Startup"
$shortcutPath = Join-Path $startupDir "xow-win.lnk"
if (Test-Path $shortcutPath) {
    Remove-Item $shortcutPath -Force
    Write-Host "[+] Removed Startup shortcut." -ForegroundColor Green
}

# 3. Find and remove xow_dongle.inf OEM driver
Write-Host "[*] Searching for installed xow-win driver packages in DriverStore..." -ForegroundColor Cyan
$drivers = pnputil /enum-drivers
$oemInf = $null
for ($i = 0; $i -lt $drivers.Count; $i++) {
    if ($drivers[$i] -match "xow_dongle.inf") {
        # Previous line usually contains Published Name: oemXX.inf
        for ($j = $i - 1; $j -ge 0; $j--) {
            if ($drivers[$j] -match "oem(\d+)\.inf") {
                $oemInf = $matches[0]
                break
            }
        }
        break
    }
}

if ($oemInf) {
    Write-Host "[*] Uninstalling driver package $oemInf..." -ForegroundColor Cyan
    pnputil /delete-driver $oemInf /uninstall /force
    Write-Host "[+] Removed $oemInf from DriverStore." -ForegroundColor Green
} else {
    Write-Host "[+] No registered xow_dongle OEM driver package found." -ForegroundColor Yellow
}

# 4. Clean program data
$ProgramDataDir = Join-Path $env:ProgramData "xow-win"
if (Test-Path $ProgramDataDir) {
    Remove-Item -Path $ProgramDataDir -Recurse -Force
    Write-Host "[+] Removed $ProgramDataDir" -ForegroundColor Green
}

Write-Host "`nUninstallation complete." -ForegroundColor Green
Write-Host "Press any key to exit..."
[void][System.Console]::ReadKey()
