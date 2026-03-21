# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Smart Car Access (SCA)** — A UWB-based relay-attack-resistant vehicle access control system. The system uses UWB Time-of-Flight for precise distance measurement (±8cm) combined with BLE for authentication to unlock/lock vehicles via CAN bus commands.

**Hardware:** ESP32-S3 + Qorvo DW3000 UWB + MCP2515 CAN controller

## Build & Flash (Arduino CLI)

```bash
# Compile Anchor (vehicle-side firmware)
arduino-cli compile --fqbn esp32:esp32:esp32s3 Src/latest/BLE_UWB_Anchor/

# Compile Tag (user-side firmware)
arduino-cli compile --fqbn esp32:esp32:esp32s3 Src/latest/BLE_UWB_Tag/

# Upload to device (replace COM3 with actual port)
arduino-cli upload -p COM3 --fqbn esp32:esp32:esp32s3 Src/latest/BLE_UWB_Anchor/
arduino-cli upload -p COM3 --fqbn esp32:esp32:esp32s3 Src/latest/BLE_UWB_Tag/

# Serial monitor at 115200 baud
arduino-cli monitor -p COM3 -c baudrate=115200
```

## Server

```bash
cd Server
python -m venv venv
venv\Scripts\activate
pip install -r requirements.txt
python main.py   # FastAPI server at http://localhost:8000
```

Windows shortcuts: `run_server.bat` / `stop_server.bat`

## Automated Release

```bash
cd "Automation Release"
pip install -r requirements.txt
cp config.json.example config.json   # Fill in GitHub token & Arduino CLI path
python auto_release.py               # Compiles, versions, and uploads to GitHub Releases
```

## Architecture

The system has three hardware roles:

### Anchor (`Src/latest/BLE_UWB_Anchor/`)
Acts as the **vehicle gateway**. Runs a BLE server, waits for Tag connections, performs challenge-response authentication (HMAC-SHA256), then participates in UWB Two-Way Ranging as the responder. On successful proximity verification (≤3m), sends CAN frames to unlock the vehicle. Sleeps when no BLE connection is active.

- `BLE_UWB_Anchor.ino` — main firmware combining BLE server, UWB responder, CAN transmitter
- `can_commands.h` — high-level CAN command API (unlock/lock)
- `can_frames.h` — raw CAN frame definitions (15 frame types)

### Tag (`Src/latest/BLE_UWB_Tag/`)
Acts as the **user key fob**. Scans for and connects to Anchor BLE, authenticates, then initiates UWB ranging. Locks/unlocks based on measured distance: unlock at ≤3m, lock at >3.5m (hysteresis).

- `BLE_UWB_Tag.ino` — main firmware combining BLE client, UWB initiator, distance logic

### Shared UWB Config
`Src/latest/uwb_config.h` — UWB RF parameters shared by both devices:
- Channel 5 (6.5 GHz), 128-symbol preamble, 6.8 Mbps data rate
- TX/RX antenna delay: 16385 (calibrated for ±8cm accuracy)
- Config option: `CONFIG_OPTION_19`

### Server (`Server/`)
FastAPI + SQLite backend for device pairing and key management. Provides:
- `/secure-check-pairing` — encrypted pairing check (AES-GCM + HKDF)
- `/owner-pairing` — register a new owner-vehicle pairing
- `/vehicle/{vehicle_id}` — vehicle info and key retrieval

mDNS service registration allows devices to discover the server on the local network without hardcoded IP.

### Android App (`AndroidApp/`)
Kotlin + Jetpack Compose. Handles user-facing BLE interactions and communicates with the server for pairing flows.

## Key Constants

**BLE UUIDs:**
- Service: `12345678-1234-5678-1234-56789abcdef0`
- Data characteristic: `abcdef12-3456-7890-abcd-ef1234567890`
- Auth characteristic: `beb5483e-36e1-4688-b7f5-ea07361b26a8`
- Challenge characteristic: `ceb5483e-36e1-4688-b7f5-ea07361b26a9`

**Distance thresholds (Tag firmware):**
- Unlock: ≤ 3.0 m
- Lock (hysteresis): > 3.5 m

**Hardware SPI pins (ESP32-S3):**
- DWM3000: RST=5, IRQ=4, CS=10, SCK=18, MISO=19, MOSI=23
- MCP2515: CS=9, shares SPI2 bus

## Libraries

- **`lib/Dw3000/`** — Qorvo DW3000 UWB driver (Apache 2.0). Key files: `dw3000_device_api`, `dw3000_config_options`, `dw3000_port` (platform abstraction), `dw3000_shared_functions`.
- **`lib/autowp-mcp2515/`** — MCP2515 CAN controller driver v1.3.1 (MIT). Provides `MCP2515` class for CAN 2.0B at up to 1 Mbps over SPI.

## Active Code vs. Examples

- **`Src/latest/`** — production firmware (use this for development)
- **`example/latest/`** — mirrors `Src/latest/` with additional markdown documentation (README, INSTALL, WIRING_GUIDE, TESTING, etc.)
- **`example/`** subdirectories — earlier prototypes and standalone tests; useful as reference but not production
