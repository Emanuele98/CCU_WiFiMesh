# 🐝 BUMBLEBEE - Wireless Power Transfer Monitoring System

## 📖 Overview

**Bumblebee** is a comprehensive real-time monitoring system for wireless power transfer infrastructure. It provides live monitoring of TX (transmitter) pads and RX (receiver) units, tracking power metrics, efficiency, temperature, and alert conditions.

### System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    ESP32 DEVICES                            │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐                   │
│  │  TX/RX   │  │  TX/RX   │  │  TX/RX   │  (WiFi Mesh Lite  │ 
│  │  Unit 1  │  │  Unit 2  │  │  Unit N  │   + ESP-NOW)      │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘                   │
│       └─────────────┴─────────────┘                         │
│                     │                                       │
│              ┌──────▼──────┐                                │
│              │   MASTER    │ (Aggregates all data)          │
│              │   ESP32     │                                │
│              └──────┬──────┘                                │
└─────────────────────┼───────────────────────────────────────┘
                      │ MQTT/WiFi
                      ▼
┌─────────────────────────────────────────────────────────────┐
│                  SOFTWARE STACK (Docker)                    │
│                                                             │
│  ┌────────────────┐          ┌─────────────────┐            │
│  │   MOSQUITTO    │────────▶│    TELEGRAF     │             │
│  │  MQTT Broker   │          │  Data Bridge    │            │
│  └────────┬───────┘          └────────┬────────┘            │
│           │                           │                     │
│           │                           ▼                     │
│           │                 ┌─────────────────┐             │
│           │                 │   INFLUXDB      │             │
│           │                 │  Time-Series DB │             │
│           │                 └─────────────────┘             │
│           │                                                 │
│           ▼                                                 │
│  ┌─────────────────┐                                        │
│  │    NODE-RED     │                                        │
│  │    Dashboard    │                                        │
│  └─────────────────┘                                        │
└─────────────────────────────────────────────────────────────┘
```

## ✨ Features

- **Real-Time Monitoring**: Live TX/RX data with 1-second updates
- **Efficiency Calculation**: Automatic power transfer efficiency
- **Master Control**: Single switch to control all TX units
- **Alert System**: Visual indicators for faults and alerts
- **Auto-Discovery**: Units appear/disappear based on activity
- **2-Minute Timeout**: Inactive units automatically hidden
- **Professional UI**: Bumblebee-branded black/yellow theme
- **Horizontal Scrolling**: View many units simultaneously

## 🚀 Quick Start

### Windows Installation

1. **Install Docker Desktop** from https://www.docker.com/products/docker-desktop
2. **Download files** to `C:\bumblebee-monitoring`
3. **Run** `start-bumblebee.bat` and choose option 1
4. **Access dashboard** at http://localhost:1880/ui

📘 Detailed guide: [SETUP-GUIDE-WINDOWS.md](SETUP-GUIDE-WINDOWS.md)

### AWS Lightsail Deployment

📘 Production deployment: [AWS-DEPLOYMENT.md](AWS-DEPLOYMENT.md)

## 📋 Files Included

- `docker-compose.yml` - Service orchestration
- `mosquitto/config/mosquitto.conf` - MQTT broker config
- `telegraf/telegraf.conf` - Data processing config
- `bumblebee-flowfuse-dashboard.json` - Dashboard flow
- `start-bumblebee.bat` - Windows management script
- `SETUP-GUIDE-WINDOWS.md` - Detailed Windows setup for local host
- `AWS-DEPLOYMENT.md` - AWS deployment guide

## 📡 MQTT Topics

### Published by ESP32
- `bumblebee/{unit_id}/dynamic` - Real-time sensor data
- `bumblebee/{unit_id}/alerts` - Alert conditions

### Subscribed by ESP32
- `bumblebee/control` - Master ON/OFF control (0 or 1)

## 🖥️ Service URLs

| Service | URL | Credentials |
|---------|-----|-------------|
| Dashboard | http://15.188.29.195:1880/:1880/dashboard/bumblebee | None |
| Node-RED | http://15.188.29.195:1880/:1880 | None |
| InfluxDB | http://15.188.29.195:1880/:8086 | admin / bumblebee2025 |
| MQTT | localhost:1883 | None |

## 🛠️ Management

### Start Services
```bash
docker compose up -d
```

### Stop Services
```bash
docker compose down
```

### View Logs
```bash
docker compose logs -f
```

## 🎯 Next Steps

1. ✅ Start local stack
2. ✅ Configure ESP32 with your IP
3. ✅ Import Node-RED flow (install flow fuse dashboard dependency using manage palette)
4. ✅ Test with real devices
5. 📱 Deploy to AWS Lightsail

---

**🐝 BUMBLEBEE - Wireless Power Transfer Monitoring**
