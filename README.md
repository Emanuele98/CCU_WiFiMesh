# 🐝 Bumblebee WiFi Mesh Network - Firmware Documentation

## Table of Contents
- [Quick Start Guide](#quick-start-guide)
- [System Architecture](#system-architecture)
- [API & Payload Documentation](#api--payload-documentation)
- [Communication Procedures](#communication-procedures)
- [Configuration & Customization](#configuration--customization)
- [Troubleshooting](#troubleshooting)

---

## Quick Start Guide

### Prerequisites

**Hardware:**
- ESP32-C6 development boards
- I2C sensors (for TX/RX detection)
- WiFi router with internet access

**Software:**
- [PlatformIO](https://platformio.org/) (recommended) or ESP-IDF v5.5.1
- Python 3.7+
- Git

### Installation Steps

#### 1. Clone the Repository
```bash
git clone https://github.com/yourusername/bumblebee-mesh.git
cd bumblebee-mesh
```

#### 2. Configure Your Unit

Edit `main/include/unitID.h` to set your unit ID:
```c
#define CONFIG_UNIT_ID 1  // Change to unique ID (1-255)
```

Update WiFi credentials in `main/Kconfig.projbuild`:
```
CONFIG_MESH_ROUTER_SSID="YourWiFiSSID"
CONFIG_MESH_ROUTER_PASSWORD="YourWiFiPassword"
```

Update MQTT broker address in `main/include/mqtt_client_manager.h`:
```c
#define MQTT_BROKER_URI "mqtt://192.168.1.92:1883"  // Your MQTT broker IP
```

#### 3. Build and Flash

**Using PlatformIO:**
```bash
pio run --target upload
pio device monitor  # View serial output
```

**Using ESP-IDF:**
```bash
idf.py build
idf.py flash monitor
```

#### 4. Verify Operation

After flashing, you should see in the serial monitor:
```
I (XXX) MAIN: ========================================
I (XXX) MAIN:   Bumblebee WiFi Mesh
I (XXX) MAIN:   Firmware Version: vX.X.X
I (XXX) MAIN:   ESP-IDF: 5.5.1
I (XXX) MAIN: ========================================
I (XXX) wifiMesh: Mesh network formed
I (XXX) wifiMesh: Root node: YES/NO
```

### First-Time Setup Checklist

- [ ] Flash firmware to at least 2 devices (1 will become ROOT)
- [ ] Set unique `CONFIG_UNIT_ID` for each device
- [ ] Ensure all devices use same WiFi credentials
- [ ] Configure MQTT broker IP address
- [ ] Power up devices and verify mesh formation
- [ ] Check MQTT broker receives data on topics: `bumblebee/{unit_id}/dynamic`

---

## System Architecture

### Overview

Bumblebee implements a **WiFi Mesh-Lite network** with **ESP-NOW** for low-latency peer-to-peer communication, designed for wireless charging station monitoring and control.

```
┌─────────────────────────────────────────────────────────────┐
│                    Internet / Cloud                          │
└─────────────────────┬───────────────────────────────────────┘
                      │
              ┌───────▼────────┐
              │  WiFi Router   │
              └───────┬────────┘
                      │ WiFi (MQTT)
              ┌───────▼────────┐
              │  MASTER/ROOT   │ ◄─── Any TX unit can be ROOT
              │    (TX1)       │      (Dynamic allocation)
              └───┬────────┬───┘
                  │        │
        WiFi-Mesh │        │ WiFi-Mesh
         (Layer1) │        │ (Layer2+)
                  │        │
         ┌────────▼──┐  ┌──▼────────┐
         │  TX2      │  │  TX3      │
         └─┬──┬──┬───┘  └───┬──┬──┬─┘
           │  │  │          │  │  │
     ESP-NOW (P2P direct)   │  │  │
           │  │  │          │  │  │
         ┌─▼──▼──▼───┐  ┌───▼──▼──▼─┐
         │ RX1 RX2 RX3│  │RX4 RX5 RX6│
         └────────────┘  └───────────┘
```

### Key Components

#### 1. **Node Types**

| Type | Role | Capabilities |
|------|------|-------------|
| **TX** | Transmitter / Charging Pad | - Sensor monitoring (V, I, T)<br>- WiFi Mesh participant<br>- ESP-NOW sender/receiver<br>- Can become MASTER<br>- MQTT publishing (if MASTER) |
| **RX** | Receiver / Scooter Unit | - Sensor monitoring (V, I, T)<br>- ESP-NOW sender only<br>- No mesh participation<br>- Battery charging status |
| **MASTER** | Root Node | - TX unit elected as root<br>- MQTT broker connection<br>- Data aggregation<br>- Control distribution |

#### 2. **Communication Layers**

**Layer 1: ESP-NOW (TX ↔ RX)**
- Protocol: Peer-to-peer, connectionless
- Purpose: Low-latency sensor data & alerts
- Latency: ~5-20 ms
- Range: Up to 200m (line-of-sight)
- Use Cases:
  - RX → TX: Dynamic sensor data, alerts, localization
  - TX → RX: Control commands, requests

**Layer 2: WiFi Mesh-Lite (TX ↔ TX)**
- Protocol: Star topology with multi-hop support
- Purpose: Data aggregation to MASTER
- Latency: ~20 ms per hop
- Throughput: 20-30 Mbps
- Max Hops: Up to 10 (configurable)
- Use Cases:
  - Child TX → ROOT: Forwarding RX data
  - ROOT → Child TX: Configuration, control

**Layer 3: MQTT (MASTER ↔ Broker)**
- Protocol: MQTT v3.1.1
- Purpose: Cloud/Backend communication
- QoS: 1 (at least once delivery)
- Use Cases:
  - Publishing sensor data
  - Receiving control commands
  - Alert notifications

#### 3. **Technology Stack**

| Component | Technology | Purpose |
|-----------|-----------|---------|
| **MCU** | ESP32-C6 | RISC-V, WiFi 6 support |
| **RTOS** | FreeRTOS | Multi-tasking, priority scheduling |
| **Framework** | ESP-IDF 5.5.1 | Native Espressif SDK |
| **Build System** | PlatformIO / CMake | Cross-platform development |
| **Mesh** | ESP-WIFI-MESH-LITE | Lightweight mesh protocol |
| **P2P** | ESP-NOW | Direct peer communication |
| **Protocol** | MQTT | IoT messaging |
| **Data Format** | JSON (MQTT) / Binary (ESP-NOW) | Flexible & efficient |

### Performance Characteristics

**Measured Performance (ESP32-C6):**
```
TX: 13.18 Mbps, 1170.5 pps, 11706 packets, 16482048 bytes
RX: 13.18 Mbps, 1170.3 pps, 11708 packets, 16484864 bytes
RTT: avg=3.11 ms, min=1.60 ms, max=15.60 ms (samples=5375)
Dropped: 0 packets, Layer: 1, Connected: YES
```

**Scalability:**
- **WiFi Mesh**: Unlimited nodes (practical limit ~100)
- **ESP-NOW**: Up to 20 paired peers per TX
- **Self-healing**: Automatic mesh reconfiguration
- **Master election**: Dynamic (any TX can become ROOT)

### Data Flow Architecture

#### Sensor Reading Flow
```
RX Sensors → ESP-NOW → TX (Child) → WiFi Mesh → TX (MASTER) → MQTT → Backend
   │                       │                        │              
   └─ Voltage          └─ Aggregation         └─ JSON Payload    
   └─ Current             └─ Filtering            └─ bumblebee/{id}/dynamic
   └─ Temperature         └─ Forwarding
```

#### Alert Flow (Fast Path)
```
RX Alert Detection → ESP-NOW → TX (Immediate Action) → WiFi Mesh → MASTER → MQTT
     (< 10ms)           │            │                    │            │
                    Local     Switch OFF              Propagate    Backend
                   Action                              Alert       Notification
```

#### Control Flow (Bidirectional)
```
Backend → MQTT → MASTER → WiFi Mesh → TX (Target) → ESP-NOW → RX (Execute)
            │        │         │           │            │
        Subscribe  Process   Route     Validate     Command
        bumblebee/ Control   to Child  Target       (Switch ON/OFF)
        control    
```

---

## API & Payload Documentation

### MQTT Topics Structure

```
bumblebee/
├── {unit_id}/
│   ├── dynamic          # Sensor telemetry data
│   └── alerts           # Alert conditions
└── control              # Global control commands
```

**Topic Patterns:**
- `bumblebee/{unit_id}/dynamic` - Telemetry from specific unit
- `bumblebee/{unit_id}/alerts` - Alerts from specific unit
- `bumblebee/control` - Global control (all units subscribe)

### Payload Formats

#### 1. Dynamic Payload (Telemetry)

**MQTT Topic:** `bumblebee/{unit_id}/dynamic`  
**Publish Rate:** Every 30s OR when significant change detected  
**Format:** JSON

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

**Field Descriptions:**

| Field | Type | Unit | Description | Range |
|-------|------|------|-------------|-------|
| `unit_id` | uint8 | - | TX unit identifier | 1-255 |
| `tx.mac` | string | - | TX MAC address | - |
| `tx.id` | uint8 | - | TX ID | 1-255 |
| `tx.status` | uint8 | - | TX operational status | 0-3 (see enum) |
| `tx.voltage` | float | V | TX output voltage | 0-80 |
| `tx.current` | float | A | TX output current | 0-5 |
| `tx.temp1` | float | °C | TX coil temperature | -40 to 125 |
| `tx.temp2` | float | °C | TX board temperature | -40 to 125 |
| `rx.mac` | string | - | RX MAC address | - |
| `rx.id` | uint8 | - | RX ID | 1-255 |
| `rx.status` | uint8 | - | RX charging status | 0-3 (see enum) |
| `rx.voltage` | float | V | RX input voltage | 0-100 |
| `rx.current` | float | A | RX input current | 0-5 |
| `rx.temp1` | float | °C | RX rectifier temp | -40 to 125 |
| `rx.temp2` | float | °C | RX battery temp | -40 to 125 |

**TX Status Enum:**
```c
typedef enum {
    TX_OFF = 0,              // Pad inactive
    TX_IDLE = 1,             // Pad ready, no RX
    TX_CHARGING = 2,         // Actively charging RX
    TX_ALERT = 3             // Alert condition
} TX_status;
```

**RX Status Enum:**
```c
typedef enum {
    RX_DISCONNECTED = 0,     // Not connected to TX
    RX_CONNECTED = 1,        // Connected, not charging
    RX_CHARGING = 2,         // Charging in progress
    RX_ALERT = 3,            // Alert condition
    RX_FULLY_CHARGED = 4     // Battery full
} RX_status;
```

**Change Detection Thresholds:**
```c
#define DELTA_VOLTAGE       1.0   // V
#define DELTA_CURRENT       0.1   // A
#define DELTA_TEMPERATURE   2.0   // °C
```
*Publishing occurs when any value exceeds threshold OR 30s interval elapses*

#### 2. Alert Payload

**MQTT Topic:** `bumblebee/{unit_id}/alerts`  
**Publish Rate:** Immediately upon alert condition  
**Format:** JSON

```json
{
  "unit_id": 1,
  "tx": {
    "mac": "AA:BB:CC:DD:EE:FF",
    "id": 1,
    "overtemperature": false,
    "overcurrent": false,
    "overvoltage": false,
    "fod": true
  },
  "rx": {
    "mac": "11:22:33:44:55:66",
    "id": 101,
    "overtemperature": true,
    "overcurrent": false,
    "overvoltage": false,
    "fully_charged": false
  }
}
```

**Alert Thresholds:**

| Alert Type | TX Threshold | RX Threshold | Action |
|------------|--------------|--------------|--------|
| `overtemperature` | > 50°C | > 60°C | Switch OFF, publish alert |
| `overcurrent` | > 2.2 A | > 2.0 A | Switch OFF, publish alert |
| `overvoltage` | > 80 V | > 100 V | Switch OFF, publish alert |
| `fod` (Foreign Object Detection) | Active | N/A | Switch OFF, publish alert |
| `fully_charged` | N/A | Detected | Stop charging, notify |

**Alert Response Flow:**
1. Local sensor detects threshold violation
2. **Immediate local action** (switch OFF if safety issue)
3. ESP-NOW alert message to parent TX (< 10ms)
4. TX forwards via WiFi Mesh to MASTER
5. MASTER publishes to MQTT `alerts` topic
6. Backend/monitoring system notified

#### 3. Control Payload (Incoming)

**MQTT Topic:** `bumblebee/control` (subscribed by MASTER only)  
**Format:** JSON

```json
{
  "command": "switch_on",
  "target": {
    "type": "tx",
    "id": 5
  }
}
```

**Supported Commands:**

| Command | Description | Target | Effect |
|---------|-------------|--------|--------|
| `switch_on` | Enable charging pad | TX ID | TX powers ON coil |
| `switch_off` | Disable charging pad | TX ID | TX powers OFF coil |
| `global_off` | Emergency stop all | "all" | All TX units power OFF |
| `reset` | Reboot unit | TX ID | Software reset |

**Control Message Flow:**
```
Backend → MQTT (control) → MASTER → WiFi Mesh → Target TX → ESP-NOW → RX (if applicable)
```

### ESP-NOW Payload Structure (Internal)

**Used for TX ↔ RX direct communication**

```c
typedef struct { 
    uint8_t id;        // Peer unit ID
    uint8_t type;      // Message type (see enum)
    uint16_t crc;      // CRC16 checksum
    float field_1;     // Context-dependent
    float field_2;     // Context-dependent
    float field_3;     // Context-dependent
    float field_4;     // Context-dependent
} __attribute__((packed)) espnow_data_t;
```

**ESP-NOW Message Types:**

| Type | Name | Direction | field_1 | field_2 | field_3 | field_4 |
|------|------|-----------|---------|---------|---------|---------|
| 0 | `DATA_BROADCAST` | RX→TX | RX Voltage | - | - | - |
| 1 | `DATA_ALERT` | RX→TX | Overvoltage | Overcurrent | Overtemp | Fully Charged |
| 2 | `DATA_DYNAMIC` | RX→TX | Voltage | Current | Temp1 | Temp2 |
| 3 | `DATA_ASK_DYNAMIC` | TX→RX | Timer value | - | - | - |

### WiFi Mesh-Lite Payload Structure (Internal)

**Used for TX ↔ TX mesh communication**

#### Static Payload (Device Registration)
```c
typedef struct {
    uint8_t macAddr[6];              // Device MAC
    uint8_t id;                      // Unit ID
    peer_type type;                  // TX or RX
    float OVERVOLTAGE_limit;         // Alert threshold
    float OVERCURRENT_limit;         // Alert threshold
    float OVERTEMPERATURE_limit;     // Alert threshold
    uint8_t FOD;                     // FOD enabled/disabled
} mesh_static_payload_t;
```

**Message IDs:**
- `0x1000` - Static payload (child → root)
- `0x1001` - Static payload response (root → child)
- `0x1002` - Dynamic payload (child → root)
- `0x1003` - Dynamic payload response
- `0x1004` - Alert payload (child → root)
- `0x1005` - Alert payload response
- `0x1006` - Localization data
- `0x1007` - Localization response
- `0x1008` - Control message (root → child)
- `0x1009` - Control message response

---

## Communication Procedures

### 1. Mesh Network Formation

**Sequence:**
```
Power ON
   │
   ├─► NVS Init
   │
   ├─► I2C Scan (TX/RX detection)
   │
   ├─► WiFi Init
   │
   ├─► Mesh Formation
   │     ├─ Search for existing mesh
   │     ├─ Join as child (if mesh exists)
   │     └─ Become root (if no mesh found)
   │
   ├─► Wait for MESH_FORMEDBIT
   │
   └─► Hardware Init
         ├─ TX: Power circuits, sensors
         └─ RX: Sensors, battery management
```

**Root Election:**
- First TX powered ON becomes ROOT automatically
- If ROOT fails, mesh re-elects new ROOT
- ROOT maintains connection to WiFi router
- Only ROOT initializes MQTT client

### 2. Device Discovery & Pairing

**TX ↔ TX (Mesh):**
```
New TX joins mesh
   │
   ├─► Send Static Payload (0x1000)
   │     └─ Contains: MAC, ID, Type, Alert Limits
   │
   ├─► ROOT processes
   │     ├─ Add TX_peer structure
   │     ├─ Store configuration
   │     └─ Send response (0x1001) with limits
   │
   └─► TX stores limits in NVS
```

**TX ↔ RX (ESP-NOW):**
```
RX powers ON
   │
   ├─► Broadcast localization beacon (voltage)
   │
TX receives broadcast
   │
   ├─► Add ESP-NOW peer (RX MAC)
   │
   ├─► Send DATA_ASK_DYNAMIC
   │
   ├─► RX responds with full sensor data
   │
   └─► TX stores RX_peer structure
```

### 3. Normal Operation Data Flow

**Periodic Telemetry (every 30s OR on change):**

```
[RX Sensors] ─── ESP-NOW ───> [TX]
                               │
                               ├─ Compare to previous values
                               │
                               ├─ If changed OR timeout:
                               │    │
                               │    └─► WiFi Mesh (0x1002) ───> [MASTER]
                               │                                    │
                               │                                    ├─ Convert to JSON
                               │                                    │
                               │                                    └─► MQTT Publish
                               │                                         bumblebee/{id}/dynamic
                               │
                               └─ Store as "previous" values
```

### 4. Alert Handling (Fast Path)

**Critical Alert Response (< 100ms total):**

```
[RX Detects Alert]
   │
   ├─► LOCAL ACTION: Switch OFF (0-10ms)
   │
   ├─► ESP-NOW Alert Message ──> [TX] (5-20ms)
   │                               │
   │                               ├─► LOCAL ACTION: Switch OFF
   │                               │
   │                               ├─► WiFi Mesh (0x1004) ──> [MASTER] (20-40ms)
   │                               │                             │
   │                               │                             └─► MQTT Publish
   │                               │                                  bumblebee/{id}/alerts
   │                               │
   │                               └─► Set reconnection timeout
   │
   └─► Wait for manual reset OR timeout
```

### 5. Control Commands

**Backend → Device Control:**

```
[Backend] ─── MQTT Publish ───> bumblebee/control
                                      │
                                      ├─ MASTER subscribes
                                      │
                                      ├─ Parse JSON
                                      │
                                      ├─ Identify target TX
                                      │
                                      ├─ WiFi Mesh (0x1008) ──> [Target TX]
                                      │                             │
                                      │                             ├─ Validate command
                                      │                             │
                                      │                             ├─ Execute locally
                                      │                             │
                                      │                             └─ OR forward via ESP-NOW ──> [RX]
                                      │
                                      └─ Response (0x1009) ──────────┘
```

### 6. Error Handling & Recovery

**ESP-NOW Communication Failure:**
```c
#define MAX_COMMS_ERROR 10

if (send_status != ESP_NOW_SEND_SUCCESS) {
    comms_fail++;
    if (comms_fail >= MAX_COMMS_ERROR) {
        // Mark peer as disconnected
        // Retry with exponential backoff
        // Or alert MASTER
    }
}
```

**WiFi Mesh Disconnection:**
```
Mesh link lost
   │
   ├─► Attempt rejoin (automatic)
   │
   ├─► If rejoin fails:
   │     └─ Scan for new parent
   │
   └─► If no mesh found:
         └─ Become new ROOT candidate
```

**MQTT Connection Loss:**
```
MQTT disconnect event
   │
   ├─► Log warning
   │
   ├─► Automatic reconnection (5s interval)
   │
   ├─► Buffer up to 20 messages locally
   │
   └─► Publish buffered data on reconnect
```

### 7. Performance Optimization

**Compile Optimizations (sdkconfig.defaults):**
```ini
CONFIG_COMPILER_OPTIMIZATION_PERF=y
CONFIG_FREERTOS_HZ=1000
CONFIG_ESP32_WIFI_STATIC_RX_BUFFER_NUM=16
CONFIG_ESP32_WIFI_DYNAMIC_RX_BUFFER_NUM=64
CONFIG_LWIP_TCP_SND_BUF_DEFAULT=65536
```

**Key Performance Settings:**
- FreeRTOS tick rate: 1000 Hz (1ms precision)
- WiFi RX/TX buffers: 64 dynamic + 16 static
- TCP send/receive window: 65536 bytes
- LWIP TCP MSS: 1460 bytes
- Flash mode: QIO @ 80MHz

---

## Configuration & Customization

### 1. Unit Configuration

**menuconfig Options:**
```bash
idf.py menuconfig

# Navigate to:
# Component config → Bumblebee Configuration

├─ Unit ID (1-255)
├─ Router SSID
├─ Router Password
├─ MQTT Broker URI
├─ Mesh Channel (1-11)
└─ Alert Thresholds
     ├─ TX Overcurrent
     ├─ TX Overvoltage
     ├─ TX Overtemperature
     ├─ RX Overcurrent
     ├─ RX Overvoltage
     └─ RX Overtemperature
```

### 2. Alert Threshold Configuration

Edit `main/include/util.h`:

```c
/* TX ALERT LIMITS */
#define OVERCURRENT_TX          2.2   // Amperes
#define OVERVOLTAGE_TX         80.0   // Volts
#define OVERTEMPERATURE_TX     50.0   // Celsius
#define FOD_ACTIVE                1   // 1=enabled, 0=disabled

/* RX ALERT LIMITS */
#define OVERCURRENT_RX          2.0   // Amperes
#define OVERVOLTAGE_RX        100.0   // Volts
#define OVERTEMPERATURE_RX     60.0   // Celsius
#define MIN_RX_VOLTAGE         50.0   // Volts
```

### 3. MQTT Configuration

Edit `main/include/mqtt_client_manager.h`:

```c
/* MQTT Broker URI */
#define MQTT_BROKER_URI "mqtt://192.168.1.92:1883"

/* For TLS/SSL */
// #define MQTT_BROKER_URI "mqtts://broker.example.com:8883"

/* Publishing Intervals */
#define MQTT_MIN_PUBLISH_INTERVAL_MS 30000  // 30 seconds

/* Authentication (if needed) */
// .credentials.username = "your_username"
// .credentials.authentication.password = "your_password"
```

### 4. Network Configuration

**WiFi Mesh Settings:**
```c
// Max mesh layers (hops from root)
#define ESP_MESH_MAX_LAYER 10

// Mesh channel
#define ESP_MESH_CHANNEL 1  // 1-11

// Max connections per node
#define ESP_MESH_AP_CONNECTIONS 10
```

**ESP-NOW Settings:**
```c
// Queue size for ESP-NOW messages
#define ESPNOW_QUEUE_SIZE 20

// Max communication errors before disconnect
#define MAX_COMMS_ERROR 10

// ESP-NOW send timeout
#define ESPNOW_QUEUE_MAXDELAY 10000  // 10s
```

### 5. Sensor Configuration

**I2C Configuration:**
```c
#define I2C_MASTER_SCL_IO    22
#define I2C_MASTER_SDA_IO    21
#define I2C_MASTER_FREQ_HZ   100000
```

**Sensor Addresses:**
```c
#define T1_SENSOR_ADDR       0x48
#define T2_SENSOR_ADDR       0x49
```

### 6. Logging Configuration

**Set log levels in menuconfig or sdkconfig:**
```ini
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
# CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y
# CONFIG_LOG_DEFAULT_LEVEL_VERBOSE=y
```

**Per-component logging:**
```c
esp_log_level_set("wifiMesh", ESP_LOG_INFO);
esp_log_level_set("MQTT_CLIENT", ESP_LOG_DEBUG);
esp_log_level_set("PEER", ESP_LOG_WARN);
```

---

## Troubleshooting

### Common Issues

#### 1. Mesh Network Not Forming

**Symptoms:**
- Units don't connect to each other
- "Mesh formation timeout" error

**Solutions:**
```bash
# Check WiFi credentials match
idf.py menuconfig → Bumblebee Config → WiFi Settings

# Verify all units on same channel
CONFIG_MESH_CHANNEL=1

# Check router is accessible
ping 192.168.1.1

# Increase mesh formation timeout
# In main/main.c:
xEventGroupWaitBits(eventGroupHandle, MESH_FORMEDBIT, 
                    pdTRUE, pdFALSE, pdMS_TO_TICKS(30000));  // 30s
```

#### 2. MQTT Connection Fails

**Symptoms:**
- "MQTT_EVENT_DISCONNECTED" repeatedly
- No data on MQTT broker

**Solutions:**
```bash
# 1. Verify broker is running
mosquitto -v

# 2. Test connectivity from device network
ping 192.168.1.92

# 3. Check broker allows connections
# In mosquitto.conf:
listener 1883
allow_anonymous true

# 4. Verify URI format
#define MQTT_BROKER_URI "mqtt://192.168.1.92:1883"  # Correct
# Not: "192.168.1.92:1883"  # Wrong (missing mqtt://)

# 5. Check firewall
sudo ufw allow 1883/tcp
```

#### 3. ESP-NOW Communication Issues

**Symptoms:**
- RX data not received by TX
- "ESP-NOW send failed" errors

**Solutions:**
```c
// 1. Verify peer is added
if (!esp_now_is_peer_exist(peer_addr)) {
    ESP_LOGE(TAG, "Peer not found!");
}

// 2. Check channel matches mesh
esp_wifi_get_channel(&primary, &secondary);

// 3. Increase queue size if overflowing
#define ESPNOW_QUEUE_SIZE 20  // → 40

// 4. Check CRC errors
uint8_t espnow_data_crc_control(uint8_t *data, uint16_t data_len);
```

#### 4. High Latency / Packet Loss

**Symptoms:**
- Slow response times
- Data gaps in MQTT stream

**Solutions:**
```bash
# 1. Check WiFi interference
iw dev wlan0 scan | grep -E "SSID|Channel"

# 2. Optimize buffer sizes
CONFIG_ESP32_WIFI_DYNAMIC_RX_BUFFER_NUM=64
CONFIG_LWIP_TCP_SND_BUF_DEFAULT=65536

# 3. Reduce log verbosity
CONFIG_LOG_DEFAULT_LEVEL_INFO=y  # Not DEBUG

# 4. Check CPU load
# Monitor task statistics in serial output

# 5. Verify power supply is adequate
# Brownout detector triggered = insufficient power
```

#### 5. Unit Not Detected as TX/RX

**Symptoms:**
- Wrong unit type assignment
- I2C scan fails

**Solutions:**
```c
// 1. Uncomment I2C scan in main.c
i2c_scan_bus();
if (i2c_device_present(T1_SENSOR_ADDR) || 
    i2c_device_present(T2_SENSOR_ADDR)) {
    UNIT_ROLE = RX;
} else {
    UNIT_ROLE = TX;
}

// 2. Manually override if needed
UNIT_ROLE = TX;  // Force TX mode

// 3. Verify sensor addresses
#define T1_SENSOR_ADDR 0x48
#define T2_SENSOR_ADDR 0x49
```

### Debug Commands

**Serial Monitor Commands:**
```bash
# View logs with timestamps
idf.py monitor --timestamps

# Filter by component
idf.py monitor | grep "wifiMesh"

# Increase baud rate
idf.py monitor -b 921600

# View core dump (if crash)
idf.py monitor --decode
```

**Useful ESP-IDF Commands:**
```bash
# Full erase (clear NVS)
idf.py erase_flash

# Monitor heap usage
idf.py monitor | grep "Free heap"

# Check partition table
idf.py partition-table

# View current configuration
idf.py show-efuse

# Run built-in tests
idf.py test
```

### Serial Log Analysis

**Healthy Boot Sequence:**
```
I (xxx) MAIN: Firmware Version: v1.0.0
I (xxx) MAIN: ESP-IDF: 5.5.1
I (xxx) wifiMesh: WiFi Mesh initialization
I (xxx) wifiMesh: Mesh network formed
I (xxx) wifiMesh: Root node: YES
I (xxx) wifiMesh: Mesh level: 1
I (xxx) MQTT_CLIENT: MQTT_EVENT_CONNECTED
I (xxx) PEER: TX Peer structure added! ID: 1
```

**Problematic Boot:**
```
E (xxx) wifiMesh: Mesh formation timeout       ← Check WiFi config
E (xxx) MQTT_CLIENT: Connection refused         ← Check broker URI
E (xxx) PEER: Failed to allocate memory         ← Heap exhaustion
W (xxx) wifiMesh: ESP-NOW send failed          ← Check peer pairing
```

### Performance Monitoring

**Benchmark Your Deployment:**
```bash
# On MASTER node serial output, look for:
"PERFORMANCE STATS (last 10001 ms)"
TX: 13.18 Mbps, 1170.5 pps
RTT: avg=3.11 ms, min=1.60 ms, max=15.60 ms
Dropped: 0 packets

# Expected values:
Throughput: > 10 Mbps
RTT (1 hop): < 5 ms
RTT (2 hops): < 10 ms
Packet loss: < 1%
```

---

## Support & Contributing

**Documentation:**
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/)
- [ESP-WIFI-MESH-LITE](https://github.com/espressif/esp-wifi-mesh-lite)
- [ESP-NOW](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html)

**Issues & Questions:**
- GitHub Issues: [Your Repository Issues]
- Community Forum: [Your Forum Link]

**License:**
- Firmware: [Your License]
- Dependencies: See `LICENSE` file

---

## Appendix

### Firmware Version History

**v1.0.0** - Initial Release
- WiFi Mesh-Lite implementation
- ESP-NOW TX-RX communication
- MQTT publishing (dynamic & alerts)
- Basic control commands

### Hardware Compatibility

**Tested Boards:**
- ESP32-C6-DevKitC-1
- ESP32-C6-MINI-1

**Required Sensors:**
- Voltage sensor: 0-100V range
- Current sensor: 0-5A range (Hall effect or shunt)
- Temperature sensors: I2C (LM75, DS18B20, or similar)

### Network Security Considerations

**Current Implementation:**
- WiFi: WPA2-PSK
- MQTT: Unencrypted (MQTT over TCP)
- ESP-NOW: Unencrypted

**Recommendations for Production:**
- Enable MQTT over TLS (mqtts://)
- Implement device authentication
- Use certificate-based MQTT auth
- Enable ESP-NOW encryption
- Regular firmware updates via OTA

---

**Document Version:** 1.0  
**Last Updated:** October 2025  
**Maintainer:** [Your Name/Team]