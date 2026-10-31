# xwa-arm64

**xwa-arm64** is an open-source, user-mode Windows driver and background service for the official **Microsoft Xbox Wireless USB Adapter** (both the Model 1713 original bulky adapter and Model 1790 slim adapter).

It enables the Xbox Wireless Adapter to work seamlessly on **Windows on ARM64** (including Snapdragon X Elite, Snapdragon X Plus, Surface Pro, and Copilot+ PCs) as well as **x64/x86 Windows**, without requiring Microsoft's missing ARM64 kernel drivers, without installing third-party driver-swapping utilities, and without disabling Driver Signature Enforcement or Core Isolation.

Derived from the Linux driver [`xow`](https://github.com/medusalix/xow) by Severin von Wnuck (Medusalix).

---

## Why xwa-arm64?

Microsoft never released an ARM64-compatible version of `mt7612us.sys` (the MediaTek MT7612U NDIS miniport driver that powers the USB dongle). While Windows 11 on ARM natively emulates x86/x64 desktop user applications, **the Windows NT kernel strictly rejects non-native kernel drivers**. As a result, plugging the adapter into a Windows on ARM PC results in Device Manager showing a missing driver error (`XBOX ACC` with Code 28).

Third-party driver tools (like Zadig) frequently fail on Windows 11 ARM64 due to 32-bit x86 emulation restrictions and kernel Memory Integrity / Core Isolation checks.

### How xwa-arm64 Solves This

1. **100% Native, In-Box Microsoft WinUSB Driver:**
   Binds the adapter to Microsoft's WHQL-signed in-box generic USB driver (**`WinUSB.sys`**) using native Windows Device Manager. No third-party driver utilities or unsigned packages needed.

2. **Built-In Modeless WinUSB Setup Assistant:**
   If `xwa-arm64` detects that the adapter needs the WinUSB driver, it opens a floating, stay-on-top Setup Assistant window side-by-side with Device Manager. It guides you through the quick 7-step process with live `SetupAPI` polling that automatically detects when configuration is complete.

3. **User-Space Transceiver & Controller Engine:**
   Communicates directly with the MediaTek wireless chip over WinUSB, verifies firmware integrity, broadcasts Xbox Wireless 802.11 beacon frames, manages encryption, and handles controller pairing.

4. **Zero-Configuration Firmware Security:**
   Firmware (`FW_ACC_00U.bin`) is never bundled in the repository or releases (respecting copyright). Instead, `xwa-arm64` fetches it on-demand from Microsoft's official Windows Update CDN (`download.windowsupdate.com`) and cryptographically validates its SHA-256 checksum before uploading to hardware.

5. **Virtual Gamepad Integration (ViGEmBus):**
   Translates wireless controller inputs into virtual Xbox 360 gamepads using **ViGEmBus**, with bidirectional force feedback (rumble) support across all PC games and emulators.

---

## Supported Hardware

| Hardware | USB ID | Description |
| :--- | :--- | :--- |
| **Model 1790** | `045E:02FE` / `045E:02E6` | Xbox Wireless Adapter for Windows (Slim version, MT7612U) |
| **Model 1713** | `045E:02D1` | Xbox Wireless Adapter for Windows (Original bulky version) |
| **Surface Transceiver** | `045E:091E` | Built-in Xbox Wireless module on Microsoft Surface devices |

---

## Features

* **System Tray Dashboard:**
  * Real-time dongle status (`Active`, `Listening`, `WinUSB Driver Required`, `Waiting / Unplugged`).
  * Connected controller battery status (`full`, `medium`, `low`, `critical`).
  * One-click "Pair New Controller" action.
  * "Start with Windows" autostart toggle.
* **Full Share Button Customization:**
  * Choose action for Single Tap, Tier 1 Hold (1.0s - 2.0s), and Tier 2 Hold (2.0s - 3.0s).
  * Capture Providers: Steam Recording, Windows Game Bar (`Win+Alt+PrtScn`, `Win+Alt+G`), or Custom Keystrokes.
  * Detent / Ramp Haptic Feedback: Distinct vibration detent notches and ascending ramps indicate hold milestones.
* **First-Party WinUSB Setup Assistant:**
  * Modeless floating window stays visible alongside Device Manager with clear instructions.
  * Real-time polling dynamically detects driver binding and auto-activates.
* **ViGEmBus Automatic Detection:**
  * Checks for ViGEmBus on startup. If missing, offers one-click automatic installation with cryptographic hash validation.

---

## Quick Start

1. Download the latest `xwa-arm64-ARM64.zip` (or `xwa-arm64-x64.zip`) from [Releases](https://github.com/DChau10/xwa-arm64/releases).
2. Extract the archive and launch `xwa-arm64.exe`.
3. If ViGEmBus is not yet installed on your system, click **Install ViGEmBus Automatically** on the prompt.
4. Plug in your Xbox Wireless Adapter:
   * If Windows has not yet assigned WinUSB to the adapter, the **WinUSB Setup Assistant** will open alongside Device Manager to guide you through the one-time driver selection.
5. Press the pairing button on your adapter and on your Xbox controller.
6. Game on!

---

## Building from Source

### Requirements
* [Visual Studio 2022](https://visualstudio.microsoft.com/) with C++ Desktop Development tools (or standalone MSVC Build Tools).
* [CMake 3.16+](https://cmake.org/).
* Git.

### Build Steps

```cmd
git clone https://github.com/DChau10/xwa-arm64.git
cd xwa-arm64

# Generate CMake build files for ARM64 (or x64)
cmake -B build -A ARM64 -DCMAKE_BUILD_TYPE=Release
# or: cmake -B build -A x64 -DCMAKE_BUILD_TYPE=Release

# Compile release binary
cmake --build build --config Release
```

The compiled binary will be located at `bin\xwa-arm64.exe`.

---

## Command-Line Options

```cmd
xwa-arm64.exe [OPTIONS]

Options:
  -c, --console   Show real-time diagnostic console and log output
  -s, --settings  Open settings window on startup
  -p, --pair      Trigger wireless pairing mode on startup
  -h, --help      Display usage information
```

---

## Architecture: ViGEmBus vs. Microsoft Synthetic Device

Users often ask why `xwa-arm64` uses ViGEmBus rather than Microsoft's internal `xboxgipsynthetic.dll` (`SWD\XBOXGIP_SYNTHETIC`):

* **ViGEmBus (Default):**
  * **Ease of Use:** Runs under standard user permissions without special system flags.
  * **Compatibility:** 100% compatible with all PC games, emulators, and launchers (Steam, Xbox Game Pass, Epic).
  * **Limitation:** Emulates an Xbox 360 layout (does not expose separate Impulse Trigger motors or 3.5mm headset audio).
* **Microsoft Synthetic Device:**
  * Requires **Windows Developer Mode** to be turned on in Windows Settings and requires running as Administrator on every launch (otherwise blocked by Windows security with `E_ACCESSDENIED` `0x80080005`).
  * Relies on undocumented, private Microsoft APIs that can break between Windows feature updates.

`xwa-arm64` abstracts the virtual gamepad layer behind the `IVirtualGamepad` interface, allowing alternative backends to be selected in the future.

---

## Credits & License

* **xow:** Originally created by [Severin von Wnuck (Medusalix)](https://github.com/medusalix/xow) for Linux.
* **ViGEmBus:** Virtual Gamepad Emulation framework created by [Benjamin Höglinger-Stelzer (Nefarius)](https://github.com/nefarius/ViGEmBus).
* Licensed under the **GNU General Public License v2.0** (matching `xow`). See `LICENSE` for details.
