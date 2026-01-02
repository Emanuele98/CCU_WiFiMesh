# 🐝 Bumblebee Firmware - Extensive Documentation

[![Version](https://img.shields.io/badge/version-v0.3.0-orange)](https://github.com/yourusername/bumblebee-mesh)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.1-green)](https://docs.espressif.com/projects/esp-idf/en/latest/)

## Table of Contents

- [Quick Start Guide](#quick-start-guide)
- [System Architecture](#system-architecture)
- [Firmware Modules](#firmware-modules)
- [OTA Update System](#ota-update-system)
- [Communication Protocols](#communication-protocols)
- [Security Implementation](#security-implementation)
- [API & Payload Documentation](#api--payload-documentation)
- [Configuration Reference](#configuration-reference)
- [Troubleshooting](#troubleshooting)

---

## Quick Start Guide

### Prerequisites

**Hardware:**
- ESP32-C6 development boards (recommended for WiFi 6)
- ESP32 classic boards (fully supported)
- I2C sensors for TX/RX detection
- WiFi router with internet access

**Software:**
- ESP-IDF v5.5.1
- Python 3.7+
- VSCode with ESP-IDF extension

### Board Compatibility

| Board | WiFi Standard | Performance | Recommended Use |
|-------|--------------|-------------|-----------------|
| **ESP32-C6** | WiFi 6 (802.11ax) | Best latency, power efficiency | Production |
| **ESP32** | WiFi 4 (802.11n) | Good performance, proven stability | Development |

### Installation Steps

#### 1. Clone Repository

```bash
git clone https://github.com/yourusername/bumblebee-mesh.git
cd bumblebee-mesh
```

#### 2. Configure Unit ID

Each device requires a unique UNIT_ID stored in NVS:

```bash
# Edit flash_unit_id.py with your ESP-IDF path
# VSCode: Ctrl+Shift+P → Tasks: Run Task → Flash Unit ID
```

#### 3. Configure WiFi

```bash
idf.py menuconfig
# Navigate to: Component config → Bumblebee Configuration
# Set Router SSID and Password
```

#### 4. Configure MQTT Security

Update `main/include/mqtt_client_manager.h`:

```c
#define MQTT_BROKER_HOST "15.188.29.195"
#define MQTT_BROKER_PORT 8883
#define MQTT_USERNAME "bumblebee"
#define MQTT_PASSWORD "bumblebee2025"
```

Embed CA certificate in `main/mqtt_client_manager.c`:

```c
static const char *mqtt_ca_cert = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIDszCCApugAwIBAgIUQtkzgohE...\n" \
"-----END CERTIFICATE-----\n";
```

#### 5. Build and Flash

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## System Architecture

### Three-Layer Communication Stack

```
┌─────────────────────────────────────────────────────────────┐
│                    LAYER 3: MQTT/TLS                        │
│         Cloud connectivity, telemetry, OTA triggers         │
│                   (ROOT node only)                          │
└─────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────────────────────────────────────┐
│                LAYER 2: WiFi Mesh-Lite                      │
│        TX↔TX communication, self-healing, multi-hop         │
│              (All TX nodes participate)                     │
└─────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────────────────────────────────────┐
│                  LAYER 1: ESP-NOW                           │
│         TX↔RX low-latency, encrypted, peer-to-peer          │
│                    (<10ms response)                         │
└─────────────────────────────────────────────────────────────┘
```

### Technology Stack

| Component | Technology | Purpose |
|-----------|-----------|---------|
| **MCU** | ESP32-C6 / ESP32 | RISC-V/Xtensa, WiFi 6/4 |
| **RTOS** | FreeRTOS | Multi-tasking, priority scheduling |
| **Framework** | ESP-IDF 5.5.1 | Native Espressif SDK |
| **Mesh** | ESP-WIFI-MESH-LITE | Lightweight mesh protocol |
| **P2P** | ESP-NOW | Direct peer communication |
| **Protocol** | MQTT over TLS | Secure IoT messaging |
| **OTA** | HTTP + SHA256 | Firmware updates |

---

## Firmware Modules

### Module Overview

```
main/
├── main.c                    # Application entry point
├── ota_manager.c             # OTA firmware updates
├── mqtt_client_manager.c     # MQTT client & publishing
├── wifiMesh.c                # Mesh-Lite & ESP-NOW
├── peer.c                    # Peer list management
├── aux_ctu_hw.c              # TX hardware interface
├── cru_hw.c                  # RX hardware interface
├── leds.c                    # Status LED indicators
└── include/
    ├── ota_manager.h         # OTA API definitions
    ├── mqtt_client_manager.h # MQTT configuration
    ├── wifiMesh.h            # Mesh message definitions
    ├── peer.h                # Peer data structures
    └── util.h                # Common utilities & config
```

### main.c - Application Entry Point

**Purpose:** System initialization, task creation, event handling.

**Key Functions:**

| Function | Description |
|----------|-------------|
| `app_main()` | Entry point - initializes NVS, WiFi, tasks |
| `init_hardware()` | Configures GPIO, ADC, I2C peripherals |
| `detect_unit_role()` | Determines TX/RX via I2C scan |

**Initialization Sequence:**
1. NVS Flash initialization
2. Unit ID retrieval from NVS
3. Hardware detection (TX/RX role)
4. WiFi Mesh initialization
5. MQTT client start (ROOT only)
6. OTA manager initialization
7. Sensor/LED task creation

---

### ota_manager.c - OTA Firmware Updates

**Purpose:** Download, verify, and install firmware updates over HTTP.

**Key Functions:**

| Function | Signature | Description |
|----------|-----------|-------------|
| `ota_manager_init` | `esp_err_t ota_manager_init(void)` | Initialize OTA subsystem, check rollback |
| `ota_start_update` | `esp_err_t ota_start_update(const char *sha256)` | Start non-blocking OTA download |
| `ota_get_progress` | `esp_err_t ota_get_progress(ota_progress_t *progress)` | Get current update status |
| `ota_is_running` | `bool ota_is_running(void)` | Check if OTA in progress |
| `ota_mark_valid` | `esp_err_t ota_mark_valid(void)` | Confirm firmware validity |

**OTA Status Codes:**

```c
typedef enum {
    OTA_STATUS_IDLE = 0,        // No OTA in progress
    OTA_STATUS_STARTING,        // Initializing download
    OTA_STATUS_DOWNLOADING,     // Downloading firmware
    OTA_STATUS_VERIFYING,       // SHA256 verification
    OTA_STATUS_FLASHING,        // Writing to flash
    OTA_STATUS_SUCCESS,         // Update successful
    OTA_STATUS_FAILED,          // Update failed
    OTA_STATUS_ROLLBACK         // Rolled back to previous
} ota_status_t;
```

**Error Codes:**

```c
typedef enum {
    OTA_ERR_NONE = 0,
    OTA_ERR_ALREADY_RUNNING,    // OTA already in progress
    OTA_ERR_HTTP_CONNECT,       // HTTP connection failed
    OTA_ERR_HTTP_RESPONSE,      // Invalid HTTP response
    OTA_ERR_DOWNLOAD,           // Download error
    OTA_ERR_SHA256_MISMATCH,    // Verification failed
    OTA_ERR_FLASH_WRITE,        // Flash write error
    OTA_ERR_PARTITION,          // Partition error
    OTA_ERR_IMAGE_INVALID,      // Invalid firmware image
    OTA_ERR_NO_MEMORY,          // Memory allocation failed
    OTA_ERR_TIMEOUT             // Operation timeout
} ota_error_t;
```

**Configuration:**

```c
// main/include/ota_manager.h
#define OTA_FIRMWARE_URL    "http://15.188.29.195:1880/ota/firmware.bin"
#define OTA_HTTP_USERNAME   "admin"
#define OTA_HTTP_PASSWORD   "bumblebee2025"
#define OTA_MQTT_TOPIC      "bumblebee/ota/start"
#define OTA_BUF_SIZE        4096
#define OTA_HTTP_TIMEOUT_MS 60000
#define OTA_REBOOT_DELAY_MS 3000
```

---

### mqtt_client_manager.c - MQTT Client

**Purpose:** Secure MQTT connection, telemetry publishing, command handling.

**Key Functions:**

| Function | Description |
|----------|-------------|
| `mqtt_client_init()` | Initialize MQTT client with TLS |
| `mqtt_publish_dynamic()` | Publish telemetry data |
| `mqtt_publish_alert()` | Publish safety alerts |
| `mqtt_publish_ota_status()` | Publish OTA progress |

**MQTT Topics Handled:**

| Topic | Direction | Handler |
|-------|-----------|---------|
| `bumblebee/control` | Subscribe | Global ON/OFF |
| `bumblebee/ota/start` | Subscribe | OTA trigger |
| `bumblebee/{id}/dynamic` | Publish | Telemetry |
| `bumblebee/{id}/alerts` | Publish | Alerts |
| `bumblebee/{id}/ota/status` | Publish | OTA status |

**OTA Command Handler:**

```c
// Triggered when MQTT receives: bumblebee/ota/start
// Expected payload: {"sha256":"64-hex-characters"}
static void handle_ota_command(const char *payload) {
    cJSON *json = cJSON_Parse(payload);
    const char *sha256 = cJSON_GetObjectItem(json, "sha256")->valuestring;
    ota_start_update(sha256);
    cJSON_Delete(json);
}
```

---

### wifiMesh.c - Mesh Networking & ESP-NOW

**Purpose:** WiFi Mesh-Lite management, ESP-NOW peer communication, message routing.

**Message IDs (Mesh-Lite):**

```c
// main/include/wifiMesh.h
#define TO_ROOT_STATIC_MSG_ID           0x100
#define TO_ROOT_STATIC_MSG_ID_RESP      0x101
#define TO_ROOT_DYNAMIC_MSG_ID          0x102
#define TO_ROOT_DYNAMIC_MSG_ID_RESP     0x103
#define TO_ROOT_ALERT_MSG_ID            0x104
#define TO_ROOT_ALERT_MSG_ID_RESP       0x105
#define TO_ROOT_LOCALIZATION_ID         0x106
#define TO_ROOT_LOCALIZATION_ID_RESP    0x107
#define TO_CHILD_CONTROL_MSG_ID         0x108
#define TO_CHILD_CONTROL_MSG_ID_RESP    0x109
```

**Message Handlers (raw_actions array):**

| Message ID | Handler | Direction |
|------------|---------|-----------|
| `TO_ROOT_STATIC_MSG_ID` | `static_to_root_raw_msg_process` | Child → Root |
| `TO_ROOT_DYNAMIC_MSG_ID` | `dynamic_to_root_raw_msg_process` | Child → Root |
| `TO_ROOT_ALERT_MSG_ID` | `alert_to_root_raw_msg_process` | Child → Root |
| `TO_ROOT_LOCALIZATION_ID` | `localization_to_root_raw_msg_process` | Child → Root |
| `TO_CHILD_CONTROL_MSG_ID` | `control_to_child_raw_msg_process` | Root → Child |

**ESP-NOW Message Types:**

```c
typedef enum {
    DATA_BROADCAST,     // Localization broadcast (RX voltage)
    DATA_ALERT,         // Critical alert from RX
    DATA_DYNAMIC,       // Dynamic payload from RX
    DATA_ASK_DYNAMIC,   // Request dynamic data from RX
    DATA_RX_LEFT        // RX departure notification
} espnow_message_type;
```

**Key Functions:**

| Function | Description |
|----------|-------------|
| `wifi_mesh_init()` | Initialize mesh network |
| `send_dynamic_message_to_root()` | Send telemetry to ROOT |
| `send_control_message_to_child()` | Broadcast control to children |
| `pass_the_baton()` | Sequential TX localization |
| `reset_the_baton()` | Turn off all TXs for localization |

---

### peer.c - Peer Management

**Purpose:** Track connected TX and RX nodes, manage peer lists.

**Data Structures:**

```c
struct TX_peer {
    uint8_t id;
    uint8_t position;
    uint8_t MACaddress[6];
    TX_status status;
    mesh_static_payload_t *static_payload;
    mesh_dynamic_payload_t *dynamic_payload;
    mesh_alert_payload_t *alert_payload;
    struct TX_peer *next;
};

struct RX_peer {
    uint8_t id;
    uint8_t position;
    uint8_t MACaddress[6];
    RX_status RX_status;
    struct RX_peer *next;
};
```

**Key Functions:**

| Function | Description |
|----------|-------------|
| `TX_peer_add()` | Add TX to peer list |
| `TX_peer_find_by_mac()` | Find TX by MAC address |
| `RX_peer_add()` | Add RX to peer list |
| `peer_delete()` | Remove peer from list |

---

## OTA Update System

### Current Implementation (v0.3.0)

The OTA system currently supports firmware updates for the **ROOT node only**. Node-RED serves the firmware file via HTTP with Basic Authentication.

### OTA Flow Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Node-RED Dashboard                       │
├─────────────────────────────────────────────────────────────┤
│  1. Upload firmware.bin                                     │
│     └─> Auto-calculate SHA256                               │
│         └─> Store in /data/ota/firmware.bin                 │
│                                                              │
│  2. Trigger OTA                                             │
│     └─> Publish MQTT: bumblebee/ota/start                   │
│         └─> Payload: {"sha256":"64-hex-chars"}              │
│                                                              │
│  3. Serve Firmware                                          │
│     └─> GET /ota/firmware.bin                               │
│         └─> HTTP Basic Auth: admin:bumblebee2025            │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                     ESP32 ROOT Node                          │
├─────────────────────────────────────────────────────────────┤
│  1. Receive MQTT trigger                                    │
│  2. Extract SHA256 from payload                             │
│  3. Start OTA download task                                 │
│  4. HTTP GET with Basic Auth                                │
│  5. Stream to OTA partition                                 │
│  6. Verify SHA256                                           │
│  7. Set boot partition                                      │
│  8. Reboot                                                  │
└─────────────────────────────────────────────────────────────┘
```

### Triggering OTA via MQTT

```bash
# Using mosquitto_pub
mosquitto_pub -h 15.188.29.195 -p 8883 \
  --cafile ca.crt \
  -u bumblebee -P bumblebee2025 \
  -t "bumblebee/ota/start" \
  -m '{"sha256":"abc123def456..."}'
```

### OTA Progress Monitoring

The ESP32 publishes status updates to `bumblebee/{unit_id}/ota/status`:

```json
{
  "status": "downloading",
  "progress": 45,
  "bytes_downloaded": 512000,
  "total_bytes": 1138000
}
```

### Partition Table

The firmware uses `two_ota_large` partition scheme:

| Partition | Type | Size | Description |
|-----------|------|------|-------------|
| `ota_0` | app | 1.5MB | Primary firmware |
| `ota_1` | app | 1.5MB | Secondary firmware |
| `otadata` | data | 8KB | OTA state tracking |
| `nvs` | data | 24KB | Non-volatile storage |

### Future Enhancement: Mesh OTA

In a future version, nginx will replace the Node-RED HTTP endpoint to enable concurrent downloads. This will allow all mesh nodes to download firmware simultaneously:

```
┌─────────────────────────────────────────────────────────────┐
│  Future: nginx static file server                           │
│  - Concurrent downloads for all nodes                       │
│  - Optimized sendfile() for efficiency                      │
│  - ROOT broadcasts OTA command to all children              │
│  - Each node downloads independently                        │
└─────────────────────────────────────────────────────────────┘
```

---

## Communication Protocols

### Mesh-Lite Message Flow

```
Child TX                    ROOT TX                    Cloud
    │                          │                          │
    │──── Static Payload ─────>│                          │
    │<─── Static Response ─────│                          │
    │                          │                          │
    │──── Dynamic Payload ────>│──── MQTT Publish ───────>│
    │<─── Dynamic Response ────│                          │
    │                          │                          │
    │──── Alert Payload ──────>│──── MQTT Publish ───────>│
    │                          │                          │
    │                          │<─── MQTT Command ────────│
    │<─── Control Broadcast ───│                          │
```

### ESP-NOW Message Flow (TX↔RX)

```
RX Unit                     TX Unit
    │                          │
    │── Broadcast (voltage) ──>│  (Localization)
    │                          │
    │<── ASK_DYNAMIC ──────────│  (TX locks onto RX)
    │                          │
    │── DYNAMIC Payload ──────>│  (Periodic updates)
    │                          │
    │── ALERT Payload ────────>│  (Critical events)
    │                          │
    │── RX_LEFT ──────────────>│  (Departure)
```

---

## Security Implementation

### MQTT TLS Configuration

```c
esp_mqtt_client_config_t mqtt_cfg = {
    .broker = {
        .address = {
            .hostname = MQTT_BROKER_HOST,
            .port = 8883,
            .transport = MQTT_TRANSPORT_OVER_SSL,
        },
        .verification = {
            .certificate = mqtt_ca_cert,
            .skip_cert_common_name_check = true,  // Self-signed
        },
    },
    .credentials = {
        .username = MQTT_USERNAME,
        .authentication.password = MQTT_PASSWORD,
    },
};
```

### ESP-NOW Encryption

Dual-layer encryption is implemented:

1. **MSK (Master Session Key)**: Network-wide encryption
2. **LSK (Local Session Key)**: Per-peer encryption

```c
// PMK and LMK configuration
#define ESPNOW_PMK "pmk1234567890999"
#define ESPNOW_LMK "lmk1234567890999"

// Enable encryption after peer discovery
esp_now_encrypt_peer(peer_mac_addr);
```

### Security Checklist

- [x] MQTT TLS 1.2 encryption
- [x] MQTT username/password authentication
- [x] ESP-NOW MSK encryption
- [x] ESP-NOW LSK encryption
- [x] HTTP Basic Auth for OTA
- [x] SHA256 firmware verification
- [x] Certificate validation
- [ ] Certificate pinning (optional)
- [ ] Mutual TLS (optional)

---

## API & Payload Documentation

### Dynamic Payload (MQTT)

```json
{
  "unit_id": 1,
  "tx": {
    "mac": "AA:BB:CC:DD:EE:FF",
    "id": 1,
    "status": 2,
    "voltage": 48.5,
    "current": 1.85,
    "temp1": 35.2,
    "temp2": 33.8
  },
  "rx": {
    "mac": "11:22:33:44:55:66",
    "id": 101,
    "status": 1,
    "voltage": 52.3,
    "current": 1.75,
    "temp1": 38.5,
    "temp2": 37.2
  }
}
```

### Alert Payload

```json
{
  "unit_id": 1,
  "tx_alerts": {
    "overvoltage": false,
    "overcurrent": true,
    "overtemperature": false,
    "fod": false
  },
  "rx_alerts": {
    "overvoltage": false,
    "overcurrent": false,
    "overtemperature": false,
    "fully_charged": false
  }
}
```

### OTA Command Payload

```json
{
  "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
}
```

---

## Configuration Reference

### util.h - Main Configuration

```c
// Alert Thresholds - TX
#define OVERCURRENT_TX      2.2    // Amperes
#define OVERVOLTAGE_TX      80.0   // Volts
#define OVERTEMPERATURE_TX  50.0   // Celsius
#define FOD_ACTIVE          true   // Foreign Object Detection

// Alert Thresholds - RX
#define OVERCURRENT_RX      2.0    // Amperes
#define OVERVOLTAGE_RX      100.0  // Volts
#define OVERTEMPERATURE_RX  60.0   // Celsius

// Timing
#define DYNAMIC_PUBLISH_INTERVAL_S  30    // Telemetry interval
#define ALERT_TIMEOUT              60000  // Alert timeout (ms)
#define LOCALIZATION_TIME_MS       500    // TX switching delay

// Voltage Detection
#define MIN_RX_VOLTAGE     40.0   // RX departure threshold
```

### sdkconfig.defaults - ESP-IDF Configuration

Key settings for mesh performance:

```ini
# Mesh-Lite
CONFIG_MESH_LITE_MAXIMUM_LEVEL_ALLOWED=2
CONFIG_MESH_LITE_MAXIMUM_NODE_NUMBER=20
CONFIG_MESH_LITE_REPORT_INTERVAL=20

# WiFi Optimization
CONFIG_ESP32_WIFI_STATIC_RX_BUFFER_NUM=16
CONFIG_ESP32_WIFI_DYNAMIC_RX_BUFFER_NUM=64

# TCP/IP
CONFIG_LWIP_TCP_SND_BUF_DEFAULT=65536
CONFIG_LWIP_TCP_WND_DEFAULT=65536
```

---

## Troubleshooting

### Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| Mesh not forming | WiFi credentials mismatch | Verify SSID/password in menuconfig |
| MQTT TLS failed | Certificate issue | Check CA cert embedded correctly |
| ESP-NOW no response | Channel mismatch | Ensure mesh and ESP-NOW on same channel |
| OTA SHA256 mismatch | Corrupted download | Retry, check network stability |
| OTA HTTP 401 | Auth failure | Verify username/password |
| Node stuck in rollback | Boot failure | Flash known-good firmware |

### Debug Commands

```bash
# Monitor with filtering
idf.py monitor | grep -E "(OTA|MQTT|wifiMesh)"

# Check heap memory
idf.py monitor | grep "heap"

# Full flash erase
idf.py erase_flash

# View partition table
idf.py partition_table
```

### OTA-Specific Debugging

```bash
# Test HTTP endpoint manually
curl -u admin:bumblebee2025 http://15.188.29.195:1880/ota/firmware.bin -o test.bin

# Verify SHA256
sha256sum test.bin

# Check firmware size
ls -la test.bin
```

---

## Version History

| Version | Date | Changes |
|---------|------|---------|
| **v0.3.0** | Jan 2026 | OTA updates (ROOT only), Node-RED integration, status reporting |
| v0.2.0 | Nov 2025 | TLS security, ESP-NOW encryption, RX departure detection |
| v0.1.0 | Oct 2025 | Initial mesh implementation, basic telemetry |

---

## Hardware Compatibility

| Feature | ESP32-C6 | ESP32 Classic |
|---------|----------|---------------|
| WiFi Standard | WiFi 6 (802.11ax) | WiFi 4 (802.11n) |
| CPU | RISC-V 160MHz | Xtensa LX6 240MHz |
| ADC Calibration | Curve Fitting | Line Fitting |
| Power Consumption | Lower | Higher |
| ESP-NOW Range | Extended | Standard |
| OTA Support | ✅ Full | ✅ Full |
| Production Ready | ✅ Recommended | ✅ Supported |

---

**Document Version:** 3.0  
**Last Updated:** January 2026  
**Firmware Version:** v0.3.0  
**Maintainer:** Bumblebee Development Team