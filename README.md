# xow-win

**xow-win** is an open-source, user-mode Windows driver and service for the official **Microsoft Xbox Wireless Adapter** (both the Model 1713 original bulky adapter and Model 1790 slim adapter).

It enables the Xbox Wireless Adapter to work seamlessly on **Windows on ARM64** (such as Snapdragon X Elite, Snapdragon X Plus, Surface Pro X, and Surface Laptop Copilot+ PCs) as well as standard **x64/x86 Windows**, without requiring Microsoft's missing ARM64 kernel drivers or disabling Driver Signature Enforcement.

Derived from the Linux driver [`xow`](https://github.com/medusalix/xow) by Severin von Wnuck (Medusalix).

---

## Why xow-win?

Microsoft never released an ARM64-compatible version of `mt7612us.sys` (the MediaTek MT7612U NDIS miniport driver that powers the USB dongle). While Windows 11 on ARM can emulate x86/x64 desktop applications, **the Windows NT kernel strictly rejects non-native kernel drivers**. As a result, plugging the adapter into a Windows on ARM PC results in Device Manager showing a driver error (`XBOX ACC` with Code 31/39).

### How xow-win Solves This
1. **Zero Kernel Modification:** Binds the adapter to Microsoft's in-box, WHQL-signed generic USB driver (**`WinUSB.sys`**) using a simple `.inf` file.
2. **User-Space Transceiver Driver:** Communicates with the MediaTek MT7612U wireless chip over WinUSB/libusb, boots the official firmware (`FW_ACC_00U.bin`), broadcasts Xbox Wireless 802.11 beacon frames, and handles pairing.
3. **Virtual Gamepad Integration:** Translates controller inputs into virtual Xbox 360 gamepads using **ViGEmBus**, with bidirectional force feedback (rumble) support across all PC games.

---

## Supported Hardware

| Hardware | USB ID | Description |
| :--- | :--- | :--- |
| **Model 1790** | `045E:02E6` | Xbox Wireless Adapter for Windows (Slim version, MT7612U) |
| **Model 1713** | `045E:02D1` | Xbox Wireless Adapter for Windows (Original bulky version) |
| **Alternate Revision** | `045E:02FE` | Xbox Wireless Adapter revised hardware |
| **Surface Transceiver**| `045E:091E` | Built-in Xbox Wireless module on Surface devices |

---

## Easy Installation (One-Click)

### Prerequisites
* Windows 10 or Windows 11 (ARM64 or x64).
* The official Xbox Wireless USB Adapter.

### Automated Setup
1. Open PowerShell as **Administrator**.
2. Run the installation script:
   ```powershell
   Set-ExecutionPolicy Bypass -Scope Process -Force
   .\scripts\install.ps1
   ```
   * What this does automatically:
     * Checks for and installs the official signed **ViGEmBus** driver if not already installed.
     * Registers the WinUSB driver package for the Xbox dongle via `pnputil`.
     * **Firmware Acquisition:** Because proprietary firmware cannot legally be redistributed, the script automatically downloads Microsoft's official driver cabinet from `download.windowsupdate.com`, extracts `FW_ACC_00U.bin` using Windows built-in `expand.exe`, verifies its SHA-256 hash, and saves it to `%ProgramData%\xow-win`.
     * Configures `xow-win.exe` to run in the background on startup.

3. Plug in your Xbox Wireless Adapter.
4. Press the pairing button on the adapter and on your Xbox controller to connect!

### Standalone Helper Scripts (Optional)
* **Download ViGEmBus installer:**
  ```powershell
  .\scripts\get-vigem.ps1
  ```
* **Download official adapter firmware:**
  ```powershell
  .\scripts\get-firmware.ps1
  ```

### Uninstallation
To completely remove `xow-win` and restore your system:
```powershell
.\scripts\uninstall.ps1
```

---

## Architecture: ViGEmBus vs. Microsoft Synthetic Device

Users often ask why `xow-win` uses ViGEmBus rather than Microsoft's internal `xboxgipsynthetic.dll` (`SWD\XBOXGIP_SYNTHETIC`):

* **ViGEmBus (Default):**
  * **Ease of Use:** Runs under standard user permissions without special system flags.
  * **Compatibility:** 100% compatible with all PC games, emulators, and launchers (Steam, Xbox Game Pass, Epic).
  * **Limitation:** Emulates an Xbox 360 layout (does not expose separate Impulse Trigger motors or 3.5mm headset audio).
* **Microsoft Synthetic Device:**
  * Requires **Windows Developer Mode** to be turned on in Windows Settings and requires running as Administrator on every launch (otherwise blocked by Windows security with `E_ACCESSDENIED` `0x80080005`).
  * Relies on undocumented, private Microsoft APIs that can break between Windows feature updates.

`xow-win` abstracts the virtual gamepad layer behind the `IVirtualGamepad` interface, allowing alternative backends to be selected in the future.

---

## Building from Source

### Requirements
* [Visual Studio 2022](https://visualstudio.microsoft.com/) with C++ Desktop Development tools (or standalone MSVC Build Tools).
* [CMake 3.16+](https://cmake.org/).
* Git.

### Build Steps (ARM64 or x64)
```cmd
git clone https://github.com/your-username/xow-win.git
cd xow-win

# Generate CMake build files (targets the host architecture by default)
cmake -B build -A ARM64   # For ARM64 (Snapdragon / Surface)
# or: cmake -B build -A x64   # For Intel / AMD 64-bit

# Compile in Release mode
cmake --build build --config Release
```
The compiled binary will be placed in `xow-win\bin\xow-win.exe`.

---

## Usage & Command-Line Flags

```cmd
xow-win.exe [OPTIONS]

Options:
  -p, --pair      Trigger wireless pairing mode on startup
  -h, --help      Display usage information
```

---

## Credits & License

* **xow:** Originally created by [Severin von Wnuck (Medusalix)](https://github.com/medusalix/xow) for Linux.
* **ViGEmBus:** Virtual Gamepad Emulation framework created by [Benjamin Höglinger-Stelzer (Nefarius)](https://github.com/nefarius/ViGEmBus).
* Licensed under the **GNU General Public License v2.0** (matching `xow`). See `LICENSE` for details.
