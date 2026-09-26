# IMEI & Radio Backup Tool [Qualcomm & MTK] - Windows App

[![Download Latest Release](https://img.shields.io/badge/📥_Download-Latest_Release-2ea44f?style=for-the-badge&logo=windows)](https://github.com/Shakib-BD/Imei-Backup-Tool/releases/latest)

[![Platform](https://img.shields.io/badge/Platform-Windows%207%20%7C%208%20%7C%2010%20%7C%2011-0078D6?logo=windows)](https://github.com/Shakib-BD/Imei-Backup-Tool)
[![Language](https://img.shields.io/badge/Language-C%2B%2B17-00599C?logo=c%2B%2B)](https://github.com/Shakib-BD/Imei-Backup-Tool)
[![Tool-Type](https://img.shields.io/badge/Utility-Firmware%20%26%20NVRAM%20Forensics-orange)](#supported-partitions)
[![Telegram](https://img.shields.io/badge/Developer-%40Shakib__BD%20%5BTG%5D-26A5E4?logo=telegram)](https://t.me/Shakib_BD)

A lightweight, standalone Windows utility designed to preserve critical baseband, NVRAM, and radio frequency (RF) calibration partitions from modern and legacy Android devices powered by **Qualcomm Snapdragon** and **MediaTek (MTK)** processors.

---

## 📑 Table of Contents
- [Overview](#overview)
- [Why IMEI & Radio Backups Are Critical](#why-imei--radio-backups-are-critical)
- [Key Features](#key-features)
- [Target Partitions](#target-partitions)
- [Prerequisites](#prerequisites)
- [Step-by-Step Usage](#step-by-step-usage)
- [Restoration Instructions](#restoration-instructions)
- [Building from Source](#building-from-source)
- [Security & Automated Cleanup](#security--automated-cleanup)
- [Disclaimer](#disclaimer)
- [Author & Support](#author--support)

---

## Overview

Flashing custom ROMs, kernels, baseband configurations, or Engineering (ENG) firmware frequently leads to corrupted NV data blocks, resulting in `Unknown Baseband`, wiped hardware serials, and `Null IMEI`.

**IMEI & Radio Backup Tool** automates the partition extraction process directly into an organized, timestamped `.zip` archive on your desktop, resolving physical block node paths across both legacy structures and modern dynamic partition layouts.

---

## Why IMEI & Radio Backups Are Critical

1. **Storage Read/Write Exposure**: Root and recovery environments grant low-level read/write access to internal physical storage blocks, making non-volatile memory vulnerable to accidental erasure.
2. **Motherboard-Unique RF Tuning**: Partitions like `modemst1/2` and `nvram` store device-specific calibration data keyed to the physical motherboard and RF transceivers. 
3. **Prevention of Null/Unknown Baseband**: Once wiped, devices lose all cellular, LTE/5G network connectivity, and SIM communication.
4. **Instant Fastboot Recovery**: Backing up raw partition images (`.img`) enables rapid manual recovery using stock fastboot commands without specialized diagnostic box equipment.

---

## Key Features

- **Universal Multi-Tier Partition Locator**:
  - Dynamically probes modern Google Android partition structures (`/dev/block/by-name/`) as well as legacy Qualcomm (`/dev/block/bootdevice/by-name/`), MediaTek (`/dev/block/platform/bootdevice/by-name/`), modern UFS/eMMC SoC trees (`/dev/block/platform/soc/*/by-name/`), and dynamic mapper devices.
  - Automatically resolves underlying hardware nodes via canonical symlink inspection (`readlink -f`).
- **Direct-to-Archive Compression**:
  - Automatically queries device codename via `getprop` and timestamps output into:
    ```text
    Desktop\IMEI-Backup-<Device_Codename>-<YYYYMMDD_HHMMSS>.zip
    ```
- **Encrypted Payload Engine**:
  - Carries its own internal, rolling-cipher encrypted ADB suite embedded into the binary.
  - Deploys safely to `C:\temp\Imei-Backup` and terminates running processes upon exit to prevent file locks.
- **Modern Minimalist UI**:
  - Windows 11/10 DWM Immersive Dark Frame integration.
  - Custom owner-drawn dual chipset pill switches and high-contrast terminal logging.
  - Built-in guidance notes and safety disclosures prior to execution.
- **Zero-Dependency Binary**:
  - Statically compiled against GCC/libstdc++ runtime to run across all client Windows environments without missing DLL errors.

---

## Target Partitions

### 🔹 Qualcomm (Snapdragon) Architecture
| Partition | Description | Function |
| :--- | :--- | :--- |
| `modemst1` | Modem NV Data 1 | Primary EFS non-volatile memory (IMEI 1 & network settings) |
| `modemst2` | Modem NV Data 2 | Secondary EFS non-volatile memory (IMEI 2 & configuration) |
| `fsg` | Golden Copy NV | Golden backup copy of factory modem NV configuration |
| `fsc` | NV Configuration | Modem NV configuration descriptor |
| `persist` | Persistent Calibration | Motherboard sensor calibration, Wi-Fi MAC, Bluetooth address |

### 🔸 MediaTek (MTK) Architecture
| Partition | Alternate Name | Description |
| :--- | :--- | :--- |
| `nvram` | — | Core hardware IMEI & RF antenna calibration |
| `nvdata` | — | Dynamic carrier configuration & runtime IMEI allocation |
| `nvcfg` | — | NVRAM partition mapping and system config |
| `protect1` | `protect_f` | Factory network settings & carrier SIM lock parameters |
| `protect2` | `protect_s` | Extended SIM profile & regional security data |
| `proinfo` | `pro_info` | Serial number, model info, and manufacturing barcode |
| `persist` | — | Persistent subsystem sensor calibration values |
| `seccfg` | `sec1` | Security partition flags and unlock validation data |

---

## Prerequisites

- **Device Mode**: The target phone **must be booted into Recovery Mode** with root shell permissions:
  - Custom Recovery (TWRP, OrangeFox, PBRP, etc.)
  - AOSP/Custom ROM environment with rooted debugging enabled
  - Engineering ROM (ENG) Recovery
- **Stock Recovery Limitation**: Stock manufacturer recovery (such as stock Xiaomi Mi-Recovery) blocks unauthorized shell execution and partition dumping. Use a compatible custom recovery or rooted engineering environment.
- **USB Cable**: Reliable USB-A to USB-C connection to avoid transfer drops during raw block image read operations.

---

## Step-by-Step Usage

1. Launch `ImeiBackupTool.exe`.
2. Review the startup advisory modal carefully and click **"OK, I Understand"**.
3. Boot your Android device into **Recovery Mode** and connect it to your PC.
4. Select your processor family:
   - **Qualcomm Snapdragon**
   - **MediaTek (MTK)**
5. Click **"Detect Device"** to verify that ADB detects your unit.
6. Click **"⚡ Backup Partitions"**.
7. The application will:
   - Request root ADB access (`adb root`).
   - Query the device codename.
   - Dump each radio partition into raw `.img` files.
   - Pack the partition images into `IMEI-Backup-<Codename>-<Timestamp>.zip` on your Desktop.
   - Reveal the generated archive inside Windows File Explorer.
8. Click **"Clean & Exit"** when finished to safely terminate background ADB daemons and remove temporary bridge files.

---

## Restoration Instructions

> **CRITICAL WARNING**:
> Never restore or flash another device's IMEI/EFS backup to your phone. Baseband calibration is uniquely paired to your motherboard's physical transceivers. Restoring alien radio files can cause permanent radio failure.

Stock vendor firmwares restrict fastboot write operations on security and modem blocks. To flash backed-up images back to the phone:

1. **Flash an Engineering (ENG) Firmware**:
   - When preparing your flash script or tool (such as MiFlash or SP Flash Tool), **uncheck or exclude** the following security partitions from the flash list to prevent overwriting:
     ```text
     fsc, fsg, modemst1, modemst2, persist, nvram, nvdata, protect1, protect2, proinfo
     ```
2. **Extract Your Backup**:
   - Extract the `.img` files from your `IMEI-Backup-<Codename>-<Timestamp>.zip` archive.
3. **Boot Device into Fastboot Mode**:
   - Run `adb reboot bootloader` or use hardware key combinations.
4. **Flash Partitions Manually**:
   - Flash each partition image individually via fastboot:
   ```bash
   fastboot flash modemst1 modemst1.img
   fastboot flash modemst2 modemst2.img
   fastboot flash fsg fsg.img
   fastboot flash persist persist.img
   ```
   *(For MTK, flash `nvram`, `nvdata`, `proinfo`, etc., according to your backed-up images).*
5. Reboot the device:
   ```bash
   fastboot reboot
   ```

---

## Building from Source

### Requirements
- **Compiler**: MinGW-w64 (GCC 9.0+ recommended with C++17 support)
- **Windows SDK / MinGW Headers**: `comctl32`, `shlwapi`, `shell32`, `dwmapi`

### Automated Build
Run the provided automated build script:
```cmd
build.bat
```

### Manual Compilation
To compile a completely standalone executable without dependencies on external MinGW DLLs:

```cmd
# 1. Compile Windows resource script
windres -i "resource.rc" -O coff -o "resource.res"

# 2. Compile standalone binary with static linking
g++ -std=c++17 main.cpp resource.res -o ImeiBackupTool.exe -mwindows -static -static-libgcc -static-libstdc++ -lcomctl32 -lshlwapi -lshell32
```

---

## Security & Automated Cleanup

- The embedded engine unpacks temporary bridge binaries into `C:\temp\Imei-Backup` on startup.
- Using the **"Clean & Exit"** button or exiting the application triggers an automated cleanup sequence (`KillAdbAndCleanup`) that:
  - Terminates running background ADB instances (`taskkill.exe /F /IM adb.exe /T`).
  - Purges temporary extraction directories from the filesystem.

---

## Disclaimer

This software interacts with low-level storage partitions. It is intended for hardware maintenance, firmware development, and repair operations. Ensure you read all device-specific recovery guidelines before modifying, wiping, or restoring radio and NVRAM partitions. The author is not responsible for improper usage or damaged hardware.

---

## Author & Support

- **Developer**: [@Shakib_BD](https://t.me/Shakib_BD)
- **Telegram Channel / Support**: [t.me/Shakib_BD](https://t.me/Shakib_BD)
- **Project Repository**: [GitHub - Shakib-BD/Imei-Backup-Tool](https://github.com/Shakib-BD/Imei-Backup-Tool)
