# 🐝 Bumblebee WiFi Mesh Lite - Wireless Charging Network

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.1-green)](https://docs.espressif.com/projects/esp-idf/en/latest/)
[![Platform](https://img.shields.io/badge/platform-ESP32--C6-blue)](https://www.espressif.com/en/products/socs/esp32-c6)
[![Platform](https://img.shields.io/badge/platform-ESP32-blue)](https://www.espressif.com/en/products/socs/esp32)
[![RTOS](https://img.shields.io/badge/RTOS-FreeRTOS-purple)](https://www.freertos.org/)


## 🎯 Overview

Bumblebee is a distributed wireless charging network firmware that enables intelligent power management for electric scooter charging stations. Built on ESP32-C6 microcontrollers (but also works on traditional ESP32), it creates a self-organizing mesh network combining WiFi Mesh-Lite for inter-station communication and ESP-NOW for ultra-low latency charging pad to scooter communication.

### 🌐 Live Dashboard
**Real-time monitoring available at: http://15.188.29.195:1880/dashboard/bumblebee**

### ⚡ Key Features
- **Self-organizing mesh network** with automatic root election
- **Dual-protocol architecture**: WiFi Mesh-Lite (TX↔TX) + ESP-NOW (TX↔RX)  
- **Real-time telemetry** via MQTT to cloud dashboard
- **Automatic scooter detection and localization**
- **Safety-first design** with hardware alerts and emergency shutoff
- **Sub-10ms alert response time** for critical safety events

### 📊 Performance Metrics

| Protocol | Use Case | Throughput | Latency | Packet Loss |
|----------|----------|------------|---------|-------------|
| **WiFi Mesh-Lite** | TX↔TX Communication | 13+ Mbps | 3-5 ms | <0% |
| **ESP-NOW** | TX↔RX Low Latency | 40 Kbps | 7 ms | 0% |
| **Native TCP** | Alternative | 224 Kbps | 3.7 ms | 0% |

*ESP-NOW provides deterministic low-latency for safety-critical alerts while Mesh-Lite handles high-throughput telemetry*

---

## 🚦 Current Status (v0.2.0)

### ✅ Implemented Features
- Hardware sensor readings (voltage, current, temperature)
- Mesh network formation and self-healing
- ESP-NOW peer-to-peer communication
- Automatic scooter localization
- Dynamic telemetry publishing (on-change + periodic)
- Alert system with immediate local response
- MQTT integration for cloud visualization
- Real-time dashboard monitoring
- RX leaving charging pad detection
- Alert reconnection timeout
- Security features (ESP-NOW encryption, MQTT TLS)

### ⚠️ Known Limitations
- Fully charged scenario incomplete
- WiFi power saving features disabled for performance
- OTA updates not implemented

### 🔄 Testing Required
Edge cases and real-world scenarios need validation before production deployment.

---

## 🚀 Quick Start

### Prerequisites
- **Hardware**: ESP32 development boards
- **Software**: ESP-IDF v5.5.1+
- **Tools**: VSCode with ESP-IDF extension

### Installation

1. **Setup ESP-IDF Environment**
   - [ESP-IDF Installation Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html)
   - [VSCode Extension Setup](https://github.com/espressif/vscode-esp-idf-extension/blob/master/docs/tutorial/install.md)

2. **Clone Repository**
   ```bash
   git clone https://github.com/yourusername/bumblebee-mesh.git
   cd bumblebee-mesh
   ```

3. **Configure Unit**
- Configure flash_unit_id.py with your ESP-IDF path
- Use Ctrl+, and look for esp-idf
- Change 'C:\\Users\\degan\\esp\\v5.5.1\\esp-idf' to your path
- Use Ctrl+Shift+P and run Task > Flash Unit ID
- Set unique UNIT_ID for each device (must be done once!)

4. **Build & Flash**
   ```bash
   idf.py build
   idf.py -p /dev/ttyUSB0 flash monitor
   ```

---

## 🏗️ Architecture

### System Overview
```
┌─────────────────────────────────────────────────────────────┐
│                    Cloud Dashboard (MQTT)                    │
└─────────────────────┬───────────────────────────────────────┘
                      │
              ┌───────▼────────┐
              │  WiFi Router   │
              └───────┬────────┘
                      │
              ┌───────▼────────┐
              │  ROOT/MASTER   │ ◄─── Any TX can be root
              │    (TX1)       │      (Automatic election)
              └───┬────────┬───┘
                  │        │
        Mesh-Lite │        │ ESP-NOW
                  │        │ (<10ms)
                  │   ┌────▼────┐
                  │   │   RX1   │ ◄─── ROOT's own RX
                  │   └─────────┘
                  │
         ┌────────▼──────────┐
         │                   │
    ┌────▼────┐         ┌────▼────┐
    │   TX2   │         │   TX3   │
    └────┬────┘         └────┬────┘
         │                   │  
  ESP-NOW│                   │ESP-NOW  
   (<10ms)                   │(<10ms)
         │                   │  
    ┌────▼────┐         ┌────▼────┐
    │   RX2   │         │   RX3   │
    └─────────┘         └─────────┘
```

### Communication Protocols

#### WiFi Mesh-Lite (TX ↔ TX)
- **Purpose**: Inter-station communication & data aggregation
- **Performance**: 13+ Mbps throughput, 3-5ms latency
- **Features**: Self-healing, multi-hop support, automatic root election
- **Documentation**: [ESP Mesh-Lite Guide](https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/solution-introduction/mesh/mesh-lite-solution.html)

#### ESP-NOW (TX ↔ RX)
- **Purpose**: Low-latency scooter communication & safety alerts
- **Performance**: 40 Kbps throughput, ~7ms latency, 0% packet loss
- **Features**: Connectionless, peer-to-peer, <10ms alert response
- **Documentation**: [ESP-NOW API Reference](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_now.html)

### Data Flow

1. **Sensor Reading** (100Hz)
   ```
   RX Sensors → ESP-NOW → TX → Mesh-Lite → ROOT → MQTT → Dashboard
   ```

2. **Alert Response** (<10ms)
   ```
   Alert Detection → Local Shutoff → ESP-NOW Alert → TX Action → Mesh Propagation
   ```

3. **Control Commands**
   ```
   Dashboard → MQTT → ROOT → Mesh → Target TX → ESP-NOW → RX
   ```

---

## 📦 Project Structure

```
BumblebeeWiFiMeshLite/
├── main/
│   ├── include/           # Header files
│   │   ├── peer.h        # Peer management
│   │   ├── wifiMesh.h    # Mesh networking
│   │   └── mqtt_client_manager.h
│   ├── main.c            # Entry point & initialization
│   ├── wifiMesh.c        # Mesh-Lite & ESP-NOW
│   ├── peer.c            # Peer list management
│   ├── mqtt_client_manager.c # MQTT publishing
│   ├── aux_ctu_hw.c      # TX hardware interface
│   ├── cru_hw.c          # RX hardware interface
│   └── leds.c            # Status indicators
├── sdkconfig.defaults     # Optimized ESP32 configuration
├── dependencies.lock      # Component versions
└── CMakeLists.txt        # Build configuration
```

---

## 📡 MQTT Topics & Payloads

### Topic Structure
```
bumblebee/
├── {unit_id}/
│   ├── dynamic          # Telemetry (30s intervals or on-change)
│   └── alerts           # Safety alerts (immediate)
└── control              # Global commands
```

### Dynamic Payload Example
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

### Alert Thresholds

| Alert Type | TX Threshold | RX Threshold | Action |
|------------|--------------|--------------|--------|
| Overtemperature | >50°C | >60°C | Immediate shutoff |
| Overcurrent | >2.2A | >2.0A | Immediate shutoff |
| Overvoltage | >80V | >100V | Immediate shutoff |
| Foreign Object | Active | N/A | Immediate shutoff |

---

## 🔧 Configuration

### WiFi Credentials
```bash
idf.py menuconfig
# Navigate to: Example Configuration
# Set Router SSID and Password
```

### Alert Limits
```c
// Edit main/include/util.h
#define OVERCURRENT_TX      2.2   // Amperes
#define OVERVOLTAGE_TX     80.0   // Volts  
#define OVERTEMPERATURE_TX 50.0   // Celsius
```

### MQTT Broker
```c
// Edit main/include/mqtt_client_manager.h
#define MQTT_BROKER_URI "mqtts://15.188.29.195:8883"
```

---

## 🐛 Troubleshooting

### Common Issues

**Mesh Not Forming**
- Verify all units use same WiFi credentials
- Check router is accessible: `ping 192.168.1.1`
- Ensure unique `UNIT_ID` per device

**MQTT Connection Failed**
- Test broker connectivity: `ping 15.188.29.195`
- Verify URI format includes `mqtt://` prefix
- Check firewall allows port 1883

**ESP-NOW Issues**
- Confirm peers added with matching channels
- Check WiFi channel alignment with mesh
- Monitor CRC errors in debug logs

### Debug Commands
```bash
# Monitor with timestamps
idf.py monitor --timestamps

# Full flash erase (clears NVS)
idf.py erase_flash

# Filter specific component logs
idf.py monitor | grep "wifiMesh"
```

---

## 📚 References

- [ESP Mesh-Lite Solution](https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/solution-introduction/mesh/mesh-lite-solution.html)
- [ESP Mesh-Lite User Guide](https://github.com/espressif/esp-mesh-lite/blob/release/v1.0/components/mesh_lite/User_Guide.md)
- [ESP-NOW Documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_now.html)
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/)

---

## 🔐 Database Access

For historical data access:
- **URL**: http://15.188.29.195:8086
- **Username**: `admin`
- **Password**: `bumblebee2025`

---

## 📝 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---


## 📞 Support

For questions and support:
- Open an issue on [GitHub Issues](https://github.com/yourusername/bumblebee-mesh/issues)
- Contact the development team

---

**Document Version:** 2.0  
**Last Updated:** November 2025  
**Firmware Version:** v0.2.0