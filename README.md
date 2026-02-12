# Smart Car Access (SCA)
**UWB-Based Secure Vehicle Access System**

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-1.0.0-green.svg)](version.txt)

> Digital Key system using Ultra-Wideband (UWB) and Bluetooth Low Energy (BLE) for secure, relay-attack resistant vehicle access control.

**Authors:** Lê Minh Hiếu, Nguyễn Đăng Khoa  
**Advisor:** Th.S Nguyễn Thành Tuyên  
**Institution:** HCMUTE - Faculty of Mechanical Engineering

---

## Quick Start

```bash
# Clone repository
git clone https://github.com/your-username/SCA.git

# Flash Anchor firmware
arduino-cli compile --fqbn esp32:esp32:esp32s3 Src/anchor/SmartCarAnchor/
arduino-cli upload -p COM3 --fqbn esp32:esp32:esp32s3 Src/anchor/SmartCarAnchor/

# Start backend server
cd Server && python -m venv venv && venv\Scripts\activate
pip install -r requirements.txt && python main.py
```

---

## Overview

### Problem Statement

Traditional keyless entry systems (LF/RF) are vulnerable to:
- **Relay Attacks**: Signal amplification from 2+ devices defeats proximity checks
- **No Distance Verification**: Cannot measure actual distance, only signal strength
- **High Theft Rate**: Thousands of vehicles stolen annually via relay attacks

### Solution

| Technology | Purpose | Performance |
|------------|---------|-------------|
| **UWB (DW3000)** | Secure ranging via ToF | ±10cm accuracy, relay-attack proof |
| **BLE 5.0** | Device discovery & auth | 10-30m range, low power |
| **ECC-256 + AES-256** | End-to-end encryption | Industry standard security |
| **CAN Bus** | Vehicle control | ISO 11898 compliant |

UWB ranging uses Time-of-Flight measurement with ±10cm accuracy, capable of detecting relay attack delays as small as 1 nanosecond.

---

## System Architecture

![System Architecture](Diagram/z7525095032747_010be537974b6ea477c51593dc253996.jpg)

The system consists of three main components: Cloud Server for user management and key distribution, Mobile App as the Digital Key, and Vehicle Gateway for authentication and control via CAN Bus.

---

## Technical Specifications

### Hardware

| Component | Model | Key Specs |
|-----------|-------|-----------|
| MCU | ESP32-S3-WROOM-1 | Dual-core 240MHz, 512KB RAM, 8MB Flash |
| UWB | Qorvo DW3000 | Ch5/9, 6.8Mbps, ±10cm accuracy, 200m range |
| BLE | ESP32-S3 Built-in | BT 5.0 LE, -97dBm sensitivity |
| CAN | MCP2515 + TJA1050 | CAN 2.0B, 1Mbps, 5V |

### Performance Metrics

| Metric | Target | Achieved |
|--------|--------|----------|
| UWB Accuracy | ±10cm | ±8cm (avg) |
| Response Time | <3s | 1.27s (unlock), 1.45s (lock) |
| Power (Standby) | <50mA | 35mA |
| Power (Active) | <200mA | 165mA |
| Success Rate (0-1m) | >95% | 98.5% |
| Relay Attack Block | 100% | 100% |

### Security Stack

| Layer | Implementation |
|-------|----------------|
| **Crypto** | AES-256-GCM, ECC-256 (P-256), HMAC-SHA256 |
| **Auth** | ECDSA signatures, Challenge-Response, Nonce |
| **Key Storage** | Secure Enclave (iOS), TEE (Android) |
| **Anti-Replay** | Sequence numbers, time windows |
| **UWB Security** | STS (Scrambled Timestamp Sequence) |

---

## Features

### 1. Owner Pairing
- Initial vehicle registration via dealer device
- ECC-256 keypair generation & secure distribution
- Public key exchange over BLE, private key → Secure Enclave

### 2. Friend Sharing (Max 5 Users)

| Access Level | Permissions | Use Case |
|--------------|-------------|----------|
| Full | Unlock + Start Engine | Family members |
| Limited | Unlock only | Valet, maintenance |
| Time-based | Temporary access | Car rental |
| One-time | Single use | Delivery services |

### 3. Passive Entry
- Auto-unlock at 1m proximity (UWB verified)
- Position detection (front/rear/left/right)
- Welcome Light activation

### 4. Secure Boot Authorization
- Requires user inside vehicle (<2m UWB range)
- Full Access permission check
- CAN command to enable ignition

### 5. Vehicle Integration
- **Control:** Door locks, lights, ignition enable
- **Monitoring:** Speed, temp, fuel, door/engine status
- **Protocol:** CAN 2.0B, 500kbps baud rate

---

## Project Structure

```
SCA/
├── Src/
│   ├── anchor/SmartCarAnchor/      # Vehicle gateway firmware
│   │   ├── ble_server.cpp/.h       # BLE GATT Server
│   │   ├── uwb_ranging.cpp/.h      # UWB TWR protocol
│   │   ├── crypto.cpp/.h            # AES/ECC crypto
│   │   └── can_interface.cpp/.h     # CAN Bus driver
│   └── tag/SmartCarTag/            # Tag firmware (ESP32 dev)
├── AndroidApp/                      # Mobile app (Kotlin/Jetpack)
├── Server/                          # Backend (Python/Flask)
│   ├── main.py                      # API server
│   ├── anchor_client.py             # Anchor mgmt
│   └── tag_client.py                # Tag mgmt
├── lib/
│   ├── DWM3000/                     # UWB driver library
│   └── autowp-mcp2515/              # CAN controller library
└── example/
    ├── BLE_Distance/                # BLE+UWB examples
    ├── SmartCarControl/             # CAN control demo
    └── OTA/                         # OTA firmware update
```

---

## Development

### Prerequisites

- **Hardware:** ESP32-S3, DW3000 module, MCP2515+TJA1050, 12V supply
- **Software:** Arduino IDE 2.x, ESP32 core 2.0+, Python 3.8+, Android Studio

### Build & Flash

```bash
# Setup ESP32 platform
arduino-cli core install esp32:esp32@2.0.14

# Compile Anchor firmware
arduino-cli compile --fqbn esp32:esp32:esp32s3 Src/anchor/SmartCarAnchor/

# Upload (replace COM3 with actual port)
arduino-cli upload -p COM3 --fqbn esp32:esp32:esp32s3 Src/anchor/SmartCarAnchor/

# Monitor serial
arduino-cli monitor -p COM3 -c baudrate=115200
```

### Backend Server

```bash
cd Server
python -m venv venv && venv\Scripts\activate  # Windows
pip install -r requirements.txt
python main.py  # Runs on http://localhost:5000
```

### Android App

```bash
cd AndroidApp
./gradlew assembleDebug  # Build APK
adb install app/build/outputs/apk/debug/app-debug.apk
```

---

## Security Architecture

### Relay Attack Mitigation

UWB Time-of-Flight measurement detects any relay device delays:
- Maximum acceptable ToF for 1m range: 6.67 nanoseconds
- Any additional delay triggers security alert
- Combined with encrypted BLE authentication

### Key Management
- ECC-256 keypair per user, stored in Secure Enclave/TEE
- Session keys regenerated per connection
- Long-term keys valid for 1 year with auto-renewal
- Remote key revocation via server API

---

## Test Results

**Test Environment:** Hyundai i30 2017, university parking lot

| Test Case | Result |
|-----------|--------|
| Replay Attack | Rejected (sequence mismatch) |
| Relay Attack | Detected (ToF anomaly) |
| MITM Attack | Detected (HMAC fail) |
| Brute Force | Locked after 5 attempts |
| Key Cloning | Prevented (Secure Enclave) |

---

## Documentation

- **[Installation Guide](example/BLE_UWB_Combined/INSTALL.md)** - Setup instructions
- **[API Reference](Server/README.md)** - Backend API docs
- **[Deployment Strategy](example/BLE_Distance/DEPLOYMENT_STRATEGY.md)** - Production deployment
- **[STS Security Guide](example/BLE_Distance/STS_GUIDE.md)** - UWB STS implementation

### Key References

- **Standards:** ISO/SAE 21434, UNECE R155, CCC Digital Key 3.0, IEEE 802.15.4z
- **Datasheets:** [DW3000](https://www.qorvo.com/products/p/DW3000), [ESP32-S3](https://www.espressif.com/en/products/socs/esp32-s3)
- **Research:** Joo et al. (2023), Kalyanaraman et al. (2020) - UWB keyless systems

---

## Contributing

This project is part of a university thesis. For collaboration inquiries, contact the authors.

## License

MIT License - Copyright (c) 2026 HCMUTE

---

*Last updated: February 2026*

