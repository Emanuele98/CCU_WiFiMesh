# Bumblebee WiFi Mesh Lite - AI Agent Instructions

## Project Overview
ESP32/ESP32-C6 firmware for distributed wireless charging stations using dual-protocol architecture: WiFi Mesh-Lite for TX↔TX communication and ESP-NOW for ultra-low latency TX↔RX safety alerts. Built with ESP-IDF v5.5.1 and FreeRTOS.

## Architecture

### Dual Unit System
- **TX (Transmitter)**: Charging pad units that form mesh network, coordinate charging, send telemetry
- **RX (Receiver)**: Scooter units that communicate via ESP-NOW for low-latency safety alerts
- Unit role auto-detected via I2C scan at boot ([main.c](main/main.c#L68-L79)) - checks for temperature sensors at `T1_SENSOR_ADDR`/`T2_SENSOR_ADDR`

### Communication Architecture
```
TX Units: WiFi Mesh-Lite (root→router) + ESP-NOW (TX↔RX)
RX Units: ESP-NOW only (low-power, safety-critical)
Root TX: MQTT bridge to cloud dashboard (TLS port 8883)
```

### Key Components
- [main/wifiMesh.c](main/wifiMesh.c) - Mesh-Lite & ESP-NOW message handlers, localization logic
- [main/peer.c](main/peer.c) - Peer management (TX/RX lists), hardware abstraction, payload structures
- [main/mqtt_client_manager.c](main/mqtt_client_manager.c) - TLS MQTT client, telemetry publishing
- [main/aux_ctu_hw.c](main/aux_ctu_hw.c) - TX hardware (STM32 UART, sensors, LED control)
- [main/cru_hw.c](main/cru_hw.c) - RX hardware (I2C sensors, voltage monitoring)

## Developer Workflows

### Initial Setup (Critical First Step)
**Every device MUST be provisioned with unique UNIT_ID before deployment:**
1. Configure your ESP-IDF path in `flash_unit_id.py` (line 36: update `'C:\\Users\\degan\\esp\\v5.5.1\\esp-idf'`)
2. Run VSCode task: `Ctrl+Shift+P` → "Tasks: Run Task" → "Flash Unit ID"
3. Enter unit ID, serial port (COM14 default), chip type (esp32/esp32c6)
4. Script flashes NVS partition with persistent ID ([flash_unit_id.py](flash_unit_id.py#L59-L115))

### Build & Flash
```bash
# VSCode ESP-IDF extension handles target automatically
idf.py build                    # Generates versioned binary: BumblebeeWiFiMeshLite_v0.2.0.bin
idf.py -p COM14 flash monitor   # Flash and view logs
```

### Configuration via Kconfig
Use `idf.py menuconfig` → "Example Configuration":
- `MESH_ROUTER_SSID` / `MESH_ROUTER_PASSWD` - WiFi credentials
- `FW_VERSION_MAJOR/MINOR/PATCH` - Semantic versioning (printed at boot)

### Multi-Board Support
- **ESP32-C6**: WiFi 6, optimized latency, recommended production
- **ESP32 Classic**: WiFi 4, known stability issues (random disconnections)
- Target auto-detected via `idf.py set-target`, binaries incompatible between chips

## Code Patterns & Conventions

### Global State via Peer System
- `UNIT_ROLE` (TX/RX) - Determines hardware init & communication behavior
- `UNIT_ID` - Unique identifier (1-255), loaded from NVS at boot
- Payloads stored in global structs: `self_dynamic_payload`, `self_alert_payload`, etc.
- Peer lists: `RX_peers`, `TX_peers` - Mutex-protected SLIST queues

### FreeRTOS Event-Driven Flow
```c
// Critical event bits used for synchronization:
MESH_FORMEDBIT   // Set when mesh connected, main waits before init ([main.c](main/main.c#L88))
LOCALIZEDBIT     // Set when RX localized to TX ([cru_hw.c](main/cru_hw.c#L181))
```

### ESP-NOW Security (Bi-directional Encryption)
```c
// BOTH sides must encrypt peers after adding:
add_peer_if_needed(mac_addr);
esp_now_encrypt_peer(mac_addr);  // Required on TX AND RX ([DOCUMENTATION_UPDATE_SUMMARY.md](DOCUMENTATION_UPDATE_SUMMARY.md#L38-L48))
```
- Master Session Key (MSK): `ESPNOW_PMK` in [wifiMesh.h](main/include/wifiMesh.h#L28)
- Local Master Key (LMK): `ESPNOW_LMK` (applied via `esp_now_encrypt_peer()`)

### Message Type Patterns
**Mesh-Lite Messages** (TX↔TX, root→cloud):
- `TO_ROOT_STATIC_MSG_ID` (0x100) - One-time config (MAC, fw version)
- `TO_ROOT_DYNAMIC_MSG_ID` (0x102) - On-change telemetry (voltage, current, temp)
- `TO_ROOT_ALERT_MSG_ID` (0x104) - Safety alerts to cloud

**ESP-NOW Messages** (TX↔RX):
- `DATA_BROADCAST` - RX voltage for localization
- `DATA_ALERT` - Critical safety events (overcurrent, overvoltage)
- `DATA_ASK_DYNAMIC` - TX requests RX telemetry
- `DATA_RX_LEFT` - RX voltage < `MIN_RX_VOLTAGE` (40V) triggers removal

### Hardware Abstraction
- TX units: STM32 coprocessor via UART ([aux_ctu_hw.c](main/aux_ctu_hw.c)), commands like `TX_OFF`, `TX_LOCALIZATION`
- RX units: Direct I2C sensors ([cru_hw.c](main/cru_hw.c))
- Alert thresholds: `OVERCURRENT_TX`, `OVERVOLTAGE_RX` etc. in [util.h](main/include/util.h#L41-L51)

## Critical Gotchas

### ESP32 vs ESP32-C6 Differences
- ESP32 has random disconnections and beacon validation issues (use ESP32-C6 for production)
- WiFi power saving disabled for both (`esp_wifi_set_ps(WIFI_PS_NONE)`) due to latency requirements

### Security Credentials Hardcoded (Development Only!)
- MQTT: [mqtt_client_manager.h](main/include/mqtt_client_manager.h) contains broker IP, username, password
- ESP-NOW: Keys defined in [wifiMesh.h](main/include/wifiMesh.h#L28-L29)
- Production: Use environment variables, vault systems, proper CA certificates

### Commented NVS Code
[main.c](main/main.c#L23-L37) has NVS unit ID reading commented out - ESP32 specific issue, hardcoded to `UNIT_ID = 1` for testing. Use `flash_unit_id.py` for proper provisioning.

## Dashboard Integration
- Cloud dashboard: http://15.188.29.195:1880/dashboard/bumblebee
- MQTT topics: `bumblebee/{unit_id}/dynamic`, `bumblebee/{unit_id}/static`, `bumblebee/{unit_id}/alert`
- Local Docker stack in [Dashboard/](Dashboard/) - Mosquitto → Telegraf → InfluxDB → Node-RED
- TLS certificates required, see [Dashboard/README.md](Dashboard/README.md#L22-L43)

## Testing & Debugging
```bash
# Monitor logs with proper filtering
idf.py monitor | grep -E "PEER|MESH|ESPNOW"

# Check mesh formation status
# Look for: "MESH_FORMEDBIT set" and "Added self TX peer"

# Verify ESP-NOW encryption
# Look for: "Peer encrypted" after peer addition
```

## Known Limitations (v0.2.0)
- Fully charged scenario incomplete
- WiFi power saving disabled (latency priority)
- OTA updates not implemented
- ESP32 stability issues (prefer ESP32-C6)
- Dashboard input controls limited (individual switches, alert thresholds)

## Useful References
- [README.md](README.md) - Architecture overview, quick start, performance metrics
- [README-FWextensive.md](README-FWextensive.md) - Detailed API, payloads, communication procedures
- [DOCUMENTATION_UPDATE_SUMMARY.md](DOCUMENTATION_UPDATE_SUMMARY.md) - v0.2.0 security implementation, resolved issues
- ESP-IDF docs: https://docs.espressif.com/projects/esp-idf/en/v5.5.1/
