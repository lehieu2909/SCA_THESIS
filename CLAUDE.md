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

Simulate device flows without hardware:
```bash
python Server/anchor_client.py   # Simulate Anchor key provisioning
python Server/tag_client.py      # Simulate Tag HMAC verification
```

## Tools

```bash
# Erase ESP32 NVS partition (reset stored pairing key before re-pairing)
python Tools/nvs_clear.py --port COM3
```

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

- `BLE_UWB_Anchor.ino` — monolithic firmware (994 lines): BLE server, UWB responder, CAN transmitter
- `can_commands.h` — high-level CAN command API (unlock/lock)
- `can_frames.h` — raw CAN frame definitions (15 unlock frames, 16 lock frames; calibrated for test vehicle)

**Alternative modular variant:** `Src/latest/SmartCar_Anchor/` — same functionality split into `ble_handler`, `uwb_handler`, `can_handler`, and `crypto_utils` modules. Uses WiFi + mDNS for key provisioning instead of hardcoded values.

### Tag (`Src/latest/BLE_UWB_Tag/`)
Acts as the **user key fob**. Scans for and connects to Anchor BLE, authenticates, then initiates UWB ranging. Locks/unlocks based on measured distance: unlock at ≤3m, lock at >3.5m (hysteresis).

- `BLE_UWB_Tag.ino` — orchestrator (~139 lines); logic split into:
  - `tag_ble.h/cpp` — BLE scan, client callbacks, notification handler
  - `tag_crypto.h/cpp` — HMAC-SHA256, hex utilities
  - `tag_uwb.h/cpp` — SS-TWR initiator loop, distance filter
  - `tag_config.h` — all tunable constants (pairing key **must match server DB**)

### Shared UWB Config
`Src/latest/uwb_config.h` — UWB RF parameters shared by both devices:
- Channel 5 (6.5 GHz), **1024-symbol preamble** (DWT_PLEN_1024), 6.8 Mbps data rate
- TX/RX antenna delay: 16385 (calibrated for ±8cm accuracy)
- Config option: `CONFIG_OPTION_19`

### Server (`Server/`)
FastAPI + SQLite (`car_access.db`, table `vehicles`) backend for device pairing and key management.

**Secure endpoints** (ECDH + AES-256-GCM encrypted, used by firmware):
- `POST /owner-pairing` — generate & store pairing key; returns it encrypted to caller's public key
- `POST /secure-check-pairing` — verify pairing exists

**Plaintext endpoints** (testing/debug only, not for production):
- `GET /check-pairing/{vehicle_id}`, `GET /vehicle/{vehicle_id}`, `GET /vehicles`, `DELETE /vehicle/{vehicle_id}`

**Key provisioning flow:**
1. Anchor loads pairing key from NVS; if absent, calls `/owner-pairing` with its SECP256R1 public key
2. Server performs ECDH, derives KEK via HKDF (`"owner-pairing-kek"`), generates 16-byte pairing key, encrypts with AES-256-GCM, stores in DB
3. Anchor decrypts, saves to NVS `Preferences`
4. Tag holds a matching key hardcoded in `tag_config.h`

mDNS service (`smartcar._http._tcp.local`) lets devices discover the server without a hardcoded IP.

### Android App (`AndroidApp/`)
Kotlin + Jetpack Compose (partial). Handles owner-side pairing flow: generates EC keypair, calls `/owner-pairing`, performs ECDH + HKDF + AES-GCM to extract the pairing key. UI fragments (`WelcomeFragment`, `EnterVinFragment`, `VerifyingVinFragment`, etc.) are partially stubbed. BLE integration is in `Bluetooth/BluetoothFragment.kt`.

## FreeRTOS Variants (`Src/testFreeRTOS/`)

Multi-task versions of the firmware under active testing:
- `FreeRTOS_Anchor/` — Anchor with FreeRTOS task scheduling
- `FreeRTOS_Tag/` — Tag with FreeRTOS task scheduling
- `FreeRTOS_Anchor_TestSimFetchKey/` — Anchor variant using a SIM module (cellular, Viettel APN) for key provisioning instead of WiFi

FreeRTOS task layout: BLE task (Core 0, prio 3), UWB task (Core 1, prio 4), CAN task (Core 1, prio 2). An `spiMutex` serializes access to the shared SPI2 bus (DW3000 + MCP2515). IPC uses `EventGroupHandle_t` (`EVT_CONNECTED`, `EVT_AUTHED`, `EVT_UWB_ACTIVE`) and per-task queues.

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
- DWM3000: RST=5, IRQ=4, CS=10, SCK=12, MISO=13, MOSI=11
- MCP2515: CS=9, shares SPI2 bus

## Libraries

- **`lib/Dw3000/`** — Qorvo DW3000 UWB driver (Apache 2.0). Key files: `dw3000_device_api`, `dw3000_config_options`, `dw3000_port` (platform abstraction), `dw3000_shared_functions`.
- **`lib/autowp-mcp2515/`** — MCP2515 CAN controller driver v1.3.1 (MIT). Provides `MCP2515` class for CAN 2.0B at up to 1 Mbps over SPI.

## Active Code vs. Examples

- **`Src/latest/`** — production firmware (use this for development)
- **`Src/testFreeRTOS/`** — FreeRTOS multi-task variants under testing
- **`example/latest/`** — mirrors `Src/latest/` with markdown documentation (README, INSTALL, WIRING_GUIDE, TESTING, etc.)
- **`example/`** subdirectories — earlier prototypes and standalone tests (OTA, SniffCAN, Module_Sim, etc.); useful as reference but not production
