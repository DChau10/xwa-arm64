# xwa-arm64

**xwa-arm64** is an open-source, user-mode Windows driver and background service for the official **Microsoft Xbox Wireless USB Adapter** (both the Model 1713 original bulky adapter and Model 1790 slim adapter).

It enables the Xbox Wireless Adapter to work seamlessly on **Windows on ARM64** (including Snapdragon X Elite, Snapdragon X Plus, Surface Pro, and Copilot+ PCs) using native Windows APIs and Microsoft's in-box WinUSB driver.

Derived from the Linux driver [`xow`](https://github.com/medusalix/xow) by Severin von Wnuck (Medusalix).

---

## Why xwa-arm64?

Microsoft's official kernel driver for the Xbox Wireless Adapter (`mt7612us.sys`) was only built for x86/x64 architectures. Because the Windows NT kernel strictly requires native ARM64 drivers, plugging the adapter into a Windows on ARM PC leaves it without a functional driver (`XBOX ACC` with Code 28).

### How It Works

1. **Standard WinUSB Interface:**
   Binds the adapter to Microsoft's built-in `WinUSB.sys` driver via Windows Device Manager, exposing standard USB bulk and control transfer pipes directly to user mode.

2. **User-Mode Transceiver Engine:**
   Initializes the MediaTek MT7612U hardware, manages 802.11 wireless beacon frames, orchestrates controller pairing, and handles encrypted Game Input Protocol (GIP) packets entirely in user space.

3. **Automated Firmware Delivery:**
   Retrieves official firmware directly from Microsoft's Windows Update CDN, verifies its cryptographic SHA-256 signature, and boots it onto the adapter hardware.

4. **Virtual Gamepad Emulation:**
   Feeds controller inputs into the Windows gaming ecosystem via [ViGEmBus](https://github.com/nefarius/ViGEmBus), providing plug-and-play Xbox 360 controller compatibility and bidirectional rumble across all PC games.

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
  * Real-time dongle status (`Active`, `Listening`, `Waiting / Unplugged`).
  * Connected controller battery status (`full`, `medium`, `low`, `critical`).
  * One-click "Pair New Controller" action.
  * "Start with Windows" autostart toggle.
* **Full Share Button Customization:**
  * Choose action for Single Tap, Tier 1 Hold (1.0s - 2.0s), and Tier 2 Hold (2.0s - 3.0s).
  * Capture Providers: Steam Recording, Windows Game Bar (`Win+Alt+PrtScn`, `Win+Alt+G`), or Custom Keystrokes.
  * Detent / Ramp Haptic Feedback: Distinct vibration detent notches and ascending ramps indicate hold milestones.
* **Automatic Onboarding:**
  * Seamlessly verifies prerequisites ([ViGEmBus](https://github.com/nefarius/ViGEmBus) and WinUSB driver binding) on launch.

---

## Quick Start

1. Download the latest `xwa-arm64-ARM64.zip` from [Releases](https://github.com/DChau10/xwa-arm64/releases).
2. Extract the archive and launch `xwa-arm64.exe`.
3. Plug in your Xbox Wireless Adapter.
4. Press the pairing button on your adapter and on your Xbox controller.
5. Game on!

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

# Generate CMake build files for ARM64
cmake -B build -A ARM64 -DCMAKE_BUILD_TYPE=Release

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

## Credits & License

* **[xow](https://github.com/medusalix/xow):** Originally created by [Severin von Wnuck (Medusalix)](https://github.com/medusalix) for Linux.
* **[ViGEmBus](https://github.com/nefarius/ViGEmBus) & [ViGEmClient](https://github.com/nefarius/ViGEmClient):** Virtual Gamepad Emulation framework created by [Benjamin Höglinger-Stelzer (Nefarius)](https://github.com/nefarius).
* **[libusb](https://libusb.info/):** Cross-platform library for USB device access.
* Licensed under the **GNU General Public License v2.0** (matching `xow`). See `LICENSE` for details.


### AI Transparency & Development

Portions of this codebase, documentation, and CI/CD automation were developed with the assistance of AI tools (including Google Antigravity / Gemini). All changes—especially low-level WinUSB transfers, cryptographic packet handling, and driver configurations—are manually audited, compiled, and tested on physical Windows on ARM64 hardware by the maintainers.
