# Sơ đồ hoạt động hệ thống BLE + UWB

## 1. Sơ đồ kết nối phần cứng

```
╔═══════════════════════════╗              ╔═══════════════════════════╗
║   TAG MODULE              ║              ║   ANCHOR MODULE           ║
║   (Người dùng)            ║              ║   (Trên xe)               ║
╠═══════════════════════════╣              ╠═══════════════════════════╣
║                           ║              ║                           ║
║  ┌─────────────────┐      ║              ║      ┌─────────────────┐  ║
║  │   ESP32 Dev     │      ║              ║      │   ESP32 Dev     │  ║
║  │                 │      ║              ║      │                 │  ║
║  │  GPIO 4  ◄──────┼──────╫──────SPI─────╫──────┤► GPIO 4        │  ║
║  │  GPIO 18 ◄──────┼──────╫──────SPI─────╫──────┤► GPIO 18       │  ║
║  │  GPIO 19 ◄──────┼──────╫──────SPI─────╫──────┤► GPIO 19       │  ║
║  │  GPIO 23 ◄──────┼──────╫──────SPI─────╫──────┤► GPIO 23       │  ║
║  │  GPIO 27 ◄──────┼──────╫─────RST──────╫──────┤► GPIO 27       │  ║
║  │  GPIO 34 ◄──────┼──────╫─────IRQ──────╫──────┤► GPIO 34       │  ║
║  │                 │      ║              ║      │                 │  ║
║  │  [BLE Radio]────┼──────╫◄────BLE─────►╫──────┤─[BLE Radio]    │  ║
║  └─────┬───────────┘      ║              ║      └────────┬────────┘  ║
║        │                  ║              ║               │           ║
║  ┌─────▼───────────┐      ║              ║      ┌────────▼────────┐  ║
║  │  DWM3000 UWB    │      ║              ║      │  DWM3000 UWB    │  ║
║  │                 │      ║              ║      │                 │  ║
║  │  [UWB Radio]────┼──────╫◄────UWB─────►╫──────┤─[UWB Radio]    │  ║
║  └─────────────────┘      ║              ║      └─────────────────┘  ║
╚═══════════════════════════╝              ╚═══════════════════════════╝
```

## 2. Luồng khởi động hệ thống

```
ANCHOR (Trên xe)                            TAG (Người dùng)
═════════════════                           ═══════════════

[START]
  │
  ├─► Init Serial
  │
  ├─► Init SPI Bus
  │
  ├─► Init BLE Device
  │   └─► Name: "CarAnchor_01"
  │
  ├─► Create BLE Server
  │   ├─► Service UUID
  │   └─► Characteristic UUID
  │
  ├─► Start BLE Advertising ───────────┐
  │   └─► Status: "ANCHOR_READY"       │
  │                                     │         [START]
  │                                     │           │
  │ ┌───────────────────────────────────┼───────────┤
  │ │  BLE Advertising                  │           │
  │ │  UUID: 12345678-1234...           │           ├─► Init Serial
  │ └───────────────────────────────────┼───────────┤
  │                                     │           │
  ▼                                     │           ├─► Init SPI Bus
[WAIT FOR CONNECTION]                   │           │
                                        │           ├─► Init BLE Device
                                        │           │   └─► Name: "UserTag_01"
                                        │           │
                                        │           ├─► Start BLE Scan
                                        │           │   └─► Looking for Service UUID
                                        │           │
                                        └───────────┤
                                                    │
                                                    ├─► Found Anchor!
                                                    │
                                                    ├─► Connect to Anchor
                                                    │
                                                    ▼
```

## 3. Sequence Diagram - BLE Pairing

```
TAG                     Bluetooth LE                    ANCHOR
│                                                          │
├─────► Scan for devices ─────────────────────────────────┤
│                                                          │
│       ◄────── Advertisement (UUID: 12345678...)─────────┤
│                                                          │
├─────► Connection Request ───────────────────────────────►
│                                                          │
│       ◄────── Connection Accepted ──────────────────────┤
│                                                          │
│                   *** BLE CONNECTED ***                  │
│                                                          │
├─────► Subscribe to Characteristic ──────────────────────►
│                                                          │
│       ◄────── Notification: "ANCHOR_READY" ─────────────┤
│                                                          │
│                 [BLE Link Established]                   │
│                                                          │
```

## 4. Sequence Diagram - UWB Wake Up & Ranging

```
TAG                          UWB Radio                    ANCHOR
│                                                          │
│  [After BLE Connected]                                   │
│                                                          │
├─► Wait 1 second                                          │
│                                                          │
│                                               [BLE Connected Event]
│                                                          │
│                                                          ├─► Init UWB Module
│                                                          │   ├─ Reset DW3000
│                                                          │   ├─ Configure
│                                                          │   └─ Set Antenna Delay
│                                                          │
│                                                          ├─► Enable RX Mode
│                                                          │
├─► Init UWB Module                                        ▼
│   ├─ Reset DW3000                            [WAIT FOR POLL MSG]
│   ├─ Configure                                           
│   └─ Set Antenna Delay                                   
│                                                          
├─► Start Ranging                                          
│                                                          
│  ┌─────────── RANGING LOOP ─────────────┐               
│  │                                       │               
│  ├─► Send Poll Message ──────────────────┼──────────────►
│  │   [Timestamp: T1]                     │               │
│  │                                       │               │
│  │                                       │               ├─► Receive Poll
│  │                                       │               │   [Timestamp: T2]
│  │                                       │               │
│  │                                       │               ├─► Prepare Response
│  │                                       │               │   - Include T2
│  │                                       │               │   - Calculate T3
│  │                                       │               │
│  │   ◄───────── Response Message ────────┼───────────────┤
│  │              [T2, T3]                 │               │   [Timestamp: T3]
│  │                                       │               │
│  ├─► Receive Response                    │               ▼
│  │   [Timestamp: T4]                     │   [WAIT FOR NEXT POLL]
│  │                                       │               
│  ├─► Calculate Distance:                 │               
│  │   TOF = [(T4-T1) - (T3-T2)] / 2      │               
│  │   Distance = TOF × Speed_of_Light    │               
│  │                                       │               
│  ├─► Display: "Distance: 2.45 m"        │               
│  │                                       │               
│  ├─► Send Distance via BLE ──────────────┼──────────────►
│  │   "DIST:2.45m"                        │               │
│  │                                       │               │
│  │   ◄─────────── BLE ACK ───────────────┼───────────────┤
│  │                                       │               │
│  ├─► Wait 1 second                       │               │
│  │                                       │               │
│  └───────────► Loop back ────────────────┘               │
│                                                          │
```

## 5. State Machine - TAG Module

```
┌─────────────────────────────────────────────────────────────────┐
│                         TAG STATE MACHINE                        │
└─────────────────────────────────────────────────────────────────┘

     START
       │
       ▼
  ┌─────────┐
  │  INIT   │  - Initialize Serial
  │         │  - Initialize SPI
  └────┬────┘  - Initialize BLE
       │
       ▼
  ┌──────────┐
  │ SCANNING │  - Scan for Anchor
  │   BLE    │  - Check Service UUID
  └────┬─────┘
       │
       │ Found Anchor
       ▼
  ┌───────────┐
  │CONNECTING │  - Connect to Anchor
  │    BLE    │  - Subscribe to notifications
  └─────┬─────┘
        │
        │ Connected
        ▼
  ┌────────────┐
  │   WAIT     │  - Wait 1 second
  │  FOR UWB   │  - Let Anchor init UWB first
  └─────┬──────┘
        │
        ▼
  ┌────────────┐
  │   INIT     │  - Initialize DW3000
  │    UWB     │  - Configure ranging
  └─────┬──────┘
        │
        ▼
  ┌────────────┐
  │  RANGING   │◄────┐ - Send Poll
  │   ACTIVE   │     │ - Receive Response
  └─────┬──────┘     │ - Calculate Distance
        │            │ - Display Distance
        │            │ - Send via BLE
        │            │ - Wait 1s
        └────────────┘
        │
        │ Disconnected
        ▼
  ┌──────────┐
  │ SCANNING │  (Loop back)
  │   BLE    │
  └──────────┘
```

## 6. State Machine - ANCHOR Module

```
┌─────────────────────────────────────────────────────────────────┐
│                       ANCHOR STATE MACHINE                       │
└─────────────────────────────────────────────────────────────────┘

     START
       │
       ▼
  ┌─────────┐
  │  INIT   │  - Initialize Serial
  │         │  - Initialize SPI
  └────┬────┘  - Initialize BLE
       │
       ▼
  ┌────────────┐
  │ADVERTISING │  - Start BLE Server
  │    BLE     │  - Advertise Service UUID
  └─────┬──────┘  - Status: "ANCHOR_READY"
        │
        │ Tag Connected
        ▼
  ┌────────────┐
  │   INIT     │  - Initialize DW3000
  │    UWB     │  - Configure ranging
  └─────┬──────┘  - Enable RX mode
        │
        ▼
  ┌────────────┐
  │RESPONDER   │◄────┐ - Wait for Poll
  │   ACTIVE   │     │ - Record RX timestamp
  └─────┬──────┘     │ - Send Response
        │            │ - Include timestamps
        │            │
        └────────────┘
        │
        │ Tag Disconnected
        ▼
  ┌────────────┐
  │ADVERTISING │  (Loop back)
  │    BLE     │
  └────────────┘
```

## 7. Timing Diagram - UWB Two-Way Ranging

```
Time
 │
 │   TAG                                         ANCHOR
 │
T1 ──┤► Poll TX ──────────┐
 │   │                    │
 │   │                    │  (Flight Time)
 │   │                    │
T2 ──┤                    └─────────────────►  Poll RX
 │   │                                           │
 │   │                                           │ (Processing)
 │   │                                           │
T3 ──┤                                           ├► Response TX ──┐
 │   │                                                            │
 │   │                                                            │
 │   │                           (Flight Time)                    │
 │   │                                                            │
T4 ──┤◄ Response RX ───────────────────────────────────────────┘
 │   │
 │   ├─► Calculate:
 │   │   Round Trip Time (Tag)  = T4 - T1
 │   │   Round Trip Time (Anchor) = T3 - T2
 │   │   Time of Flight = [(T4-T1) - (T3-T2)] / 2
 │   │   Distance = TOF × Speed of Light
 │   │
 ▼   ▼
```

## 8. Protocol Stack

```
┌─────────────────────────────────────────────────────────┐
│                   APPLICATION LAYER                      │
│  - Distance Calculation                                  │
│  - Display Management                                    │
│  - Decision Logic                                        │
└─────────────────┬───────────────────────────────────────┘
                  │
┌─────────────────┴───────────────────────────────────────┐
│                 COMMUNICATION LAYER                      │
│  ┌──────────────────────┐  ┌──────────────────────┐    │
│  │   BLE Protocol       │  │   UWB Protocol       │    │
│  │  - Pairing           │  │  - TWR Ranging       │    │
│  │  - Authentication    │  │  - Timestamps        │    │
│  │  - Data Exchange     │  │  - TOF Calculation   │    │
│  └──────────┬───────────┘  └──────────┬───────────┘    │
└─────────────┼────────────────────────────┼──────────────┘
              │                            │
┌─────────────┴────────────────────────────┴──────────────┐
│                    HARDWARE LAYER                        │
│  ┌──────────────────────┐  ┌──────────────────────┐    │
│  │  BLE Radio (ESP32)   │  │  UWB Radio (DW3000)  │    │
│  │  - 2.4 GHz           │  │  - 6.5 GHz           │    │
│  │  - Range: ~50m       │  │  - Range: ~100m      │    │
│  │  - Low Power         │  │  - High Precision    │    │
│  └──────────────────────┘  └──────────────────────┘    │
└─────────────────────────────────────────────────────────┘
```

## 9. Data Flow

```
┌─────────────────────────────────────────────────────────────┐
│                      DATA FLOW DIAGRAM                       │
└─────────────────────────────────────────────────────────────┘

USER ACTION
    │
    ▼
┌─────────────┐      BLE Pairing      ┌─────────────┐
│   TAG       │◄─────────────────────►│   ANCHOR    │
│             │                        │             │
│ ┌─────────┐ │                        │ ┌─────────┐ │
│ │   BLE   │ │  Connection Status     │ │   BLE   │ │
│ │ Client  │─┼───────────────────────►│ │ Server  │ │
│ └────┬────┘ │                        │ └─────────┘ │
│      │      │                        │             │
│      │ Trigger                       │             │
│      ▼      │                        │             │
│ ┌─────────┐ │                        │ ┌─────────┐ │
│ │   UWB   │ │    Poll Message        │ │   UWB   │ │
│ │Initiator│─┼───────────────────────►│ │Responder│ │
│ │         │ │                        │ │         │ │
│ │         │ │   Response + Timestamps │ │         │ │
│ │         │◄┼────────────────────────┤ │         │ │
│ └────┬────┘ │                        │ └─────────┘ │
│      │      │                        │             │
│      │ Calculate                     │             │
│      ▼      │                        │             │
│ ┌─────────┐ │                        │             │
│ │Distance │ │      Distance Data     │             │
│ │ Engine  │─┼───────────────────────►│             │
│ └────┬────┘ │       (via BLE)        │             │
│      │      │                        │             │
│      ▼      │                        │             │
│  [Display]  │                        │             │
│  2.45 m     │                        │             │
└─────────────┘                        └─────────────┘
```

## 10. Security & Power Management

```
┌─────────────────────────────────────────────────────────────┐
│                   SECURITY FEATURES                          │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌─────────────────┐         ┌─────────────────┐           │
│  │  BLE Pairing    │         │  UWB Ranging    │           │
│  │  ─────────────  │         │  ─────────────  │           │
│  │  • Authentication│        │  • Physical     │           │
│  │  • Encryption   │         │    Distance     │           │
│  │  • Bonding      │         │  • Anti-Relay   │           │
│  └─────────────────┘         └─────────────────┘           │
│           │                           │                     │
│           └───────────┬───────────────┘                     │
│                       ▼                                     │
│              ┌──────────────────┐                           │
│              │  2-Factor Auth   │                           │
│              │  BLE + Distance  │                           │
│              └──────────────────┘                           │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                   POWER MANAGEMENT                           │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  STATE          │  BLE        │  UWB        │  Power        │
│  ──────────────────────────────────────────────────────────┤
│  Advertising    │  Active     │  Sleep      │  Low (10mA)  │
│  Connected      │  Active     │  Sleep      │  Med (20mA)  │
│  Ranging        │  Active     │  Active     │  High (80mA) │
│  Disconnected   │  Sleep      │  Sleep      │  Very Low    │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

---
**Note:** Các sơ đồ này minh họa hoạt động của hệ thống BLE+UWB Combined. 
Để biết thêm chi tiết, xem README.md và INSTALL.md
