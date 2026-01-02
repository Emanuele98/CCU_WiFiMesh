# 🐝 Bumblebee - Wireless Power Transfer Mesh Network

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.1-green)](https://docs.espressif.com/projects/esp-idf/en/latest/)
[![Platform](https://img.shields.io/badge/platform-ESP32--C6-blue)](https://www.espressif.com/en/products/socs/esp32-c6)
[![Platform](https://img.shields.io/badge/platform-ESP32-blue)](https://www.espressif.com/en/products/socs/esp32)
[![Version](https://img.shields.io/badge/version-v0.3.0-orange)](https://github.com/yourusername/bumblebee-mesh)
[![RTOS](https://img.shields.io/badge/RTOS-FreeRTOS-purple)](https://www.freertos.org/)

## 🎯 Overview

Bumblebee is a distributed wireless charging network firmware for electric scooter charging stations. Built on ESP32-C6 microcontrollers (with ESP32 classic support), it creates a self-organizing mesh network combining **WiFi Mesh-Lite** for inter-station communication and **ESP-NOW** for ultra-low latency charging pad communication.

### 🌐 Live Dashboard

**Real-time monitoring:** [http://15.188.29.195:1880/dashboard/bumblebee](http://15.188.29.195:1880/dashboard/bumblebee)

---

## ⚡ Key Features

| Feature | Description |
|---------|-------------|
| **Self-organizing Mesh** | Automatic root election, multi-hop routing, self-healing topology |
| **Dual-protocol Architecture** | WiFi Mesh-Lite (TX↔TX) + ESP-NOW (TX↔RX) |
| **Real-time Telemetry** | MQTT over TLS to cloud dashboard |
| **OTA Firmware Updates** | Over-the-air updates with SHA256 verification |
| **Automatic Localization** | Scooter detection and position tracking |
| **Safety-first Design** | Hardware alerts, emergency shutoff, <10ms response |
| **Dual-layer Encryption** | MQTT TLS + ESP-NOW MSK/LSK encryption |

---

## 📊 Performance Metrics

| Protocol | Use Case | Throughput | Latency | Security |
|----------|----------|------------|---------|----------|
| **WiFi Mesh-Lite** | TX↔TX Communication | 13+ Mbps | 3-5 ms | WPA2 |
| **ESP-NOW** | TX↔RX Low Latency | 40 Kbps | 7 ms | MSK+LSK |
| **MQTT/TLS** | Cloud Connectivity | N/A | ~50 ms | TLS 1.2 |

---

## 🏗️ System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                AWS Lightsail Cloud Infrastructure           │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐        │
│  │Mosquitto│  │InfluxDB │  │Telegraf │  │Node-RED │        │
│  │ (MQTT)  │  │  (DB)   │  │(Bridge) │  │(Dashboard│        │
│  │ :8883   │  │ :8086   │  │         │  │  :1880) │        │
│  └────┬────┘  └─────────┘  └─────────┘  └────┬────┘        │
└───────┼──────────────────────────────────────┼─────────────┘
        │ MQTT/TLS                             │ HTTP (OTA)
        │                                      │
┌───────▼──────────────────────────────────────▼─────────────┐
│                      WiFi Router                            │
└───────┬─────────────────────────────────────────────────────┘
        │
┌───────▼───────┐
│  ROOT/MASTER  │◄─── Any TX can become root (automatic election)
│     (TX1)     │
└───┬───────┬───┘
    │       │
    │  ESP-NOW (<10ms)
    │       │
    │  ┌────▼────┐
    │  │   RX1   │◄─── ROOT's paired receiver
    │  └─────────┘
    │
    │ WiFi Mesh-Lite
    │
┌───▼────┐     ┌─────────┐
│  TX2   │     │   TX3   │
└───┬────┘     └────┬────┘
    │               │
    │ ESP-NOW       │ ESP-NOW
    │               │
┌───▼────┐     ┌────▼────┐
│  RX2   │     │   RX3   │
└────────┘     └─────────┘
```

---

## 🚦 Current Status (v0.3.0)

### ✅ Implemented Features

- **Core Functionality**
  - Hardware sensor readings (voltage, current, temperature)
  - Mesh network formation with self-healing
  - ESP-NOW peer-to-peer encrypted communication
  - Automatic scooter localization (sequential TX switching)
  - Dynamic telemetry (on-change + periodic publishing)

- **Safety & Alerts**
  - Immediate local response (<10ms)
  - Alert propagation through mesh
  - RX departure detection
  - Configurable alert thresholds

- **Security**
  - MQTT over TLS (port 8883)
  - ESP-NOW dual encryption (MSK + LSK)
  - HTTP Basic Authentication for OTA
  - Certificate validation

- **OTA Updates**
  - ROOT node firmware download via HTTP
  - SHA256 integrity verification
  - Automatic partition switching
  - Rollback support on boot failure

### 🔄 Planned Features

- **Mesh OTA Coordination**: Sequential/parallel firmware updates for all nodes (requires nginx integration)
- **Fully Charged Detection**: Complete charging cycle management
- **Dashboard Enhancements**: Individual TX switches, threshold configuration, waveform display

---

## 🚀 Quick Start

### Prerequisites

- **Hardware**: ESP32-C6 (recommended) or ESP32 classic
- **Software**: ESP-IDF v5.5.1+, Python 3.7+
- **Tools**: VSCode with ESP-IDF extension

### Installation

```bash
# Clone repository
git clone https://github.com/yourusername/bumblebee-mesh.git
cd bumblebee-mesh

# Configure Unit ID (one-time setup per device)
# Edit flash_unit_id.py with your ESP-IDF path, then:
# VSCode: Ctrl+Shift+P → Tasks: Run Task → Flash Unit ID

# Configure WiFi
idf.py menuconfig
# Navigate to: Component config → Bumblebee Configuration
# Set SSID and Password

# Build and flash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Verify Operation

```
I (XXX) MAIN: ========================================
I (XXX) MAIN:   Bumblebee WiFi Mesh
I (XXX) MAIN:   Firmware Version: v0.3.0
I (XXX) MAIN:   Build Date: YYYY-MM-DD HH:MM:SS
I (XXX) MAIN: ========================================
I (XXX) wifiMesh: Mesh network formed
I (XXX) MQTT_CLIENT: Connected to broker (TLS)
```

---

## 📡 MQTT Topics

| Topic | Direction | Description |
|-------|-----------|-------------|
| `bumblebee/{unit_id}/dynamic` | ESP32 → Cloud | Real-time telemetry |
| `bumblebee/{unit_id}/alerts` | ESP32 → Cloud | Safety alerts |
| `bumblebee/{unit_id}/ota/status` | ESP32 → Cloud | OTA progress updates |
| `bumblebee/control` | Cloud → ESP32 | Global ON/OFF control |
| `bumblebee/ota/start` | Cloud → ESP32 | OTA trigger command |

---

## 🔧 Configuration

### Alert Thresholds

```c
// main/include/util.h
#define OVERCURRENT_TX      2.2   // Amperes
#define OVERVOLTAGE_TX     80.0   // Volts
#define OVERTEMPERATURE_TX 50.0   // Celsius
```

### MQTT Broker

```c
// main/include/mqtt_client_manager.h
#define MQTT_BROKER_HOST "15.188.29.195"
#define MQTT_BROKER_PORT 8883
#define MQTT_USERNAME "bumblebee"
#define MQTT_PASSWORD "bumblebee2025"
```

---

## 📦 Project Structure

```
bumblebee-mesh/
├── main/
│   ├── include/              # Header files
│   │   ├── ota_manager.h     # OTA update management
│   │   ├── mqtt_client_manager.h
│   │   ├── wifiMesh.h        # Mesh networking
│   │   ├── peer.h            # Peer management
│   │   └── util.h            # Configuration & utilities
│   ├── main.c                # Entry point
│   ├── ota_manager.c         # OTA implementation
│   ├── mqtt_client_manager.c # MQTT publishing
│   ├── wifiMesh.c            # Mesh-Lite & ESP-NOW
│   ├── peer.c                # Peer list management
│   └── ...
├── Dashboard/                # Cloud infrastructure
│   ├── docker-compose.yml    # Service orchestration
│   ├── AWS-DEPLOYMENT.md     # Deployment guide
│   └── README.md             # Dashboard documentation
├── README.md                 # This file
├── README-FWextensive.md     # Detailed firmware docs
└── version.txt               # Current version
```

---

## 📚 Documentation

| Document | Description |
|----------|-------------|
| [README-FWextensive.md](README-FWextensive.md) | Detailed firmware architecture, APIs, and procedures |
| [Dashboard/README.md](Dashboard/README.md) | Dashboard setup and MQTT configuration |
| [Dashboard/AWS-DEPLOYMENT.md](Dashboard/AWS-DEPLOYMENT.md) | Production deployment guide |

---

## 🔐 Access Credentials

### Production Services

| Service | URL | Credentials |
|---------|-----|-------------|
| Dashboard | http://15.188.29.195:1880/dashboard/bumblebee | — |
| Node-RED Editor | http://15.188.29.195:1880 | admin / bumblebee2025 |
| InfluxDB | http://15.188.29.195:8086 | admin / bumblebee2025 |
| MQTT (TLS) | mqtts://15.188.29.195:8883 | bumblebee / bumblebee2025 |

⚠️ **Security Note**: Change default credentials in production environments.

---

## 🐛 Troubleshooting

| Issue | Solution |
|-------|----------|
| Mesh not forming | Verify WiFi credentials, check router accessibility, ensure unique UNIT_ID |
| MQTT connection failed | Check TLS certificate, verify port 8883 is open, test with `ping 15.188.29.195` |
| ESP-NOW issues | Confirm peer channel alignment, check encryption keys match |
| OTA fails | Verify SHA256 hash, check HTTP auth credentials, ensure sufficient heap memory |

```bash
# Debug commands
idf.py monitor --timestamps
idf.py monitor | grep "OTA"
idf.py erase_flash  # Full NVS reset
```

---

## 📈 Version History

| Version | Date | Highlights |
|---------|------|------------|
| **v0.3.0** | Jan 2026 | OTA firmware updates, Node-RED dashboard integration |
| v0.2.0 | Nov 2025 | Security hardening (TLS, ESP-NOW encryption) |
| v0.1.0 | Oct 2025 | Initial release with mesh networking |

---

## 📞 Support

- **Issues**: [GitHub Issues](https://github.com/yourusername/bumblebee-mesh/issues)
- **Documentation**: See linked guides above

---

**🐝 BUMBLEBEE - Wireless Power Transfer Mesh Network**  
**Version 0.3.0** | **ESP-IDF 5.5.1** | **January 2026**