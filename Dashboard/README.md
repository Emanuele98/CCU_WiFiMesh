# 🐝 Bumblebee Monitoring Dashboard

## Software Stack: Mosquitto → Telegraf → InfluxDB → Node-RED

[![Docker](https://img.shields.io/badge/Docker-Compose-blue)](https://docs.docker.com/compose/)
[![MQTT](https://img.shields.io/badge/MQTT-TLS--Secured-red)](https://mosquitto.org/)
[![Version](https://img.shields.io/badge/version-v0.3.0-orange)](https://github.com/yourusername/bumblebee-mesh)

---

## 🎯 Overview

The Bumblebee Dashboard provides real-time monitoring and management of the wireless power transfer mesh network. It consists of four Docker containers working together to collect, store, and visualize telemetry data from ESP32 nodes.

### Live Dashboard

**URL:** [http://15.188.29.195:1880/dashboard/bumblebee](http://15.188.29.195:1880/dashboard/bumblebee)

---

## 🏗️ Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                         Docker Compose Stack                         │
│                                                                      │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────────────────┐  │
│  │  Mosquitto  │───>│  Telegraf   │───>│       InfluxDB          │  │
│  │   (MQTT)    │    │  (Bridge)   │    │    (Time-Series DB)     │  │
│  │  :8883 TLS  │    │             │    │        :8086            │  │
│  └──────┬──────┘    └─────────────┘    └─────────────────────────┘  │
│         │                                                            │
│         │ Subscribe                                                  │
│         ▼                                                            │
│  ┌─────────────────────────────────────────────────────────────────┐│
│  │                        Node-RED                                  ││
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐ ││
│  │  │  Dashboard  │  │     OTA     │  │    MQTT Integration     │ ││
│  │  │  Real-time  │  │  Management │  │  Subscribe & Publish    │ ││
│  │  │  Gauges     │  │  Firmware   │  │                         │ ││
│  │  └─────────────┘  └─────────────┘  └─────────────────────────┘ ││
│  │                        :1880                                     ││
│  └─────────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────────┘
                                │
                                │ MQTT/TLS
                                ▼
                    ┌─────────────────────┐
                    │   ESP32 Mesh        │
                    │   ROOT → Children   │
                    └─────────────────────┘
```

---

## 📦 Components

### Mosquitto (MQTT Broker)

Handles all MQTT communication between ESP32 devices and the dashboard.

| Port | Protocol | Purpose |
|------|----------|---------|
| 8883 | MQTTS | External secure connections (ESP32) |
| 1883 | MQTT | Internal Docker network only |
| 9001 | WSS | WebSocket over TLS |

**Features:**
- TLS 1.2 encryption
- Username/password authentication
- Persistent sessions
- Message logging

### Telegraf (Data Bridge)

Subscribes to MQTT topics and writes data to InfluxDB.

**Subscribed Topics:**
- `bumblebee/+/dynamic` - Telemetry data
- `bumblebee/+/alerts` - Safety alerts

**Output:** InfluxDB v2 with token authentication

### InfluxDB (Time-Series Database)

Stores all historical telemetry and alert data.

| Setting | Value |
|---------|-------|
| Organization | bumblebee |
| Bucket | sensor_data |
| Retention | Unlimited |

**Access:** http://15.188.29.195:8086

### Node-RED (Dashboard & OTA)

Provides the web interface for monitoring and firmware management.

**Features:**
- Real-time telemetry visualization
- Multi-unit status display
- OTA firmware upload and management
- MQTT topic monitoring
- Global control commands

---

## 📡 MQTT Topics

### Published by ESP32

| Topic | Description | Frequency |
|-------|-------------|-----------|
| `bumblebee/{unit_id}/dynamic` | Telemetry (voltage, current, temp) | 30s or on-change |
| `bumblebee/{unit_id}/alerts` | Safety alerts | Immediate |
| `bumblebee/{unit_id}/ota/status` | OTA progress updates | During OTA |

### Subscribed by ESP32

| Topic | Description | Payload |
|-------|-------------|---------|
| `bumblebee/control` | Global ON/OFF | `0` or `1` |
| `bumblebee/ota/start` | OTA trigger | `{"sha256":"..."}` |

### Example Payloads

**Dynamic Telemetry:**
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

**OTA Command:**
```json
{
  "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
}
```

---

## 🔄 OTA Firmware Updates

The dashboard includes an OTA management page for uploading and deploying firmware updates.

### OTA Flow

```
┌─────────────────────────────────────────────────────────────┐
│                    OTA Management Page                       │
│                                                              │
│  1. Upload firmware.bin (drag & drop)                       │
│  2. SHA256 calculated automatically                          │
│  3. Stored in /data/ota/firmware.bin                        │
│  4. Click "Trigger OTA"                                      │
│  5. MQTT publishes to bumblebee/ota/start                   │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    ESP32 ROOT Node                           │
│                                                              │
│  1. Receives MQTT trigger                                   │
│  2. Downloads: GET /ota/firmware.bin                        │
│  3. HTTP Basic Auth: admin:bumblebee2025                    │
│  4. Verifies SHA256                                         │
│  5. Flashes to OTA partition                                │
│  6. Reboots                                                 │
└─────────────────────────────────────────────────────────────┘
```

### Current Limitations

- **ROOT node only**: Currently, only the mesh ROOT node can receive OTA updates
- **Sequential download**: Single connection to Node-RED HTTP endpoint

### Future Enhancement

nginx integration will enable concurrent firmware downloads for all mesh nodes simultaneously.

---

## 🔐 Security

### Authentication

| Service | Username | Password |
|---------|----------|----------|
| Node-RED Editor | admin | bumblebee2025 |
| OTA HTTP Endpoint | admin | bumblebee2025 |
| MQTT Broker | bumblebee | bumblebee2025 |
| InfluxDB | admin | bumblebee2025 |

### TLS/SSL

- MQTT: Port 8883 with TLS 1.2
- Self-signed certificates (development)
- Let's Encrypt supported (production)

### Node-RED Security

The `settings.js` file configures:
- Admin authentication (bcrypt hashed passwords)
- HTTP node authentication for OTA endpoints
- Credential encryption

**Generate bcrypt hash:**
```bash
node -e "console.log(require('bcryptjs').hashSync('your_password', 8))"
```

---

## 🚀 Quick Start

### Local Development

```bash
# Clone repository
git clone https://github.com/yourusername/bumblebee-mesh.git
cd bumblebee-mesh/Dashboard

# Generate certificates
cd mosquitto/certs
openssl req -new -x509 -days 365 -extensions v3_ca \
  -keyout ca.key -out ca.crt \
  -subj "/CN=Bumblebee-CA"
openssl genrsa -out server.key 2048
openssl req -new -key server.key -out server.csr -subj "/CN=localhost"
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key \
  -CAcreateserial -out server.crt -days 365
cd ../..

# Start services
docker compose up -d

# Create MQTT users
docker compose exec mosquitto mosquitto_passwd -c /mosquitto/config/passwd bumblebee
docker compose exec mosquitto mosquitto_passwd /mosquitto/config/passwd telegraf
docker compose exec mosquitto mosquitto_passwd /mosquitto/config/passwd nodered

# Access dashboard
open http://localhost:1880/dashboard/bumblebee
```

### Production Deployment

See [AWS-DEPLOYMENT.md](AWS-DEPLOYMENT.md) for complete instructions.

---

## 🛠️ Management Commands

### Service Control

```bash
# Start all
docker compose up -d

# Stop all
docker compose down

# Restart specific service
docker compose restart nodered

# View logs
docker compose logs -f
docker compose logs -f nodered
```

### MQTT Testing

```bash
# Subscribe to all topics
docker compose exec mosquitto mosquitto_sub \
  -h localhost -p 1883 \
  -u bumblebee -P bumblebee2025 \
  -t '#' -v

# Publish test message
docker compose exec mosquitto mosquitto_pub \
  -h localhost -p 1883 \
  -u bumblebee -P bumblebee2025 \
  -t 'test/topic' -m 'Hello'

# Test OTA trigger
docker compose exec mosquitto mosquitto_pub \
  -h localhost -p 1883 \
  -u bumblebee -P bumblebee2025 \
  -t 'bumblebee/ota/start' \
  -m '{"sha256":"abc123..."}'
```

### OTA Directory Management

```bash
# Create OTA directories
docker exec -u 0 bumblebee-nodered mkdir -p /data/ota/versions
docker exec -u 0 bumblebee-nodered chown -R node-red:node-red /data/ota

# List firmware files
docker exec bumblebee-nodered ls -la /data/ota/

# Test firmware endpoint
curl -u admin:bumblebee2025 http://localhost:1880/ota/firmware.bin -o test.bin
```

---

## 📁 Files Included

| File | Description |
|------|-------------|
| `docker-compose.yml` | Service orchestration |
| `mosquitto/config/mosquitto.conf` | MQTT broker configuration |
| `mosquitto/certs/` | TLS certificates |
| `telegraf/telegraf.conf` | Data bridge configuration |
| `nodered/settings.js` | Node-RED configuration |
| `AWS-DEPLOYMENT.md` | Production deployment guide |
| `README.md` | This file |

---

## 🖥️ Service URLs

### Local Development

| Service | URL | Credentials |
|---------|-----|-------------|
| Dashboard | http://localhost:1880/dashboard/bumblebee | — |
| Node-RED | http://localhost:1880 | admin / bumblebee2025 |
| InfluxDB | http://localhost:8086 | admin / bumblebee2025 |
| OTA Firmware | http://localhost:1880/ota/firmware.bin | admin / bumblebee2025 |

### Production (AWS)

| Service | URL | Credentials |
|---------|-----|-------------|
| Dashboard | http://15.188.29.195:1880/dashboard/bumblebee | — |
| Node-RED | http://15.188.29.195:1880 | admin / bumblebee2025 |
| InfluxDB | http://15.188.29.195:8086 | admin / bumblebee2025 |
| MQTT (TLS) | mqtts://15.188.29.195:8883 | bumblebee / bumblebee2025 |

---

## 🐛 Troubleshooting

### MQTT Connection Issues

```bash
# Test TLS connection
openssl s_client -connect localhost:8883 -CAfile mosquitto/certs/ca.crt

# Check certificate dates
openssl x509 -in mosquitto/certs/server.crt -noout -dates

# View Mosquitto logs
docker compose logs -f mosquitto
```

### Node-RED Issues

```bash
# Check Node-RED logs
docker compose logs -f nodered

# Restart Node-RED
docker compose restart nodered

# Access container shell
docker exec -it bumblebee-nodered /bin/bash
```

### ESP32 Connection Issues

1. Verify CA certificate is embedded in firmware
2. Check ESP32 has ~40KB heap for TLS
3. Verify username/password matches
4. Check firewall allows ESP32 IP range

---

## 📚 Additional Resources

- [Mosquitto Documentation](https://mosquitto.org/documentation/)
- [Node-RED Documentation](https://nodered.org/docs/)
- [InfluxDB Documentation](https://docs.influxdata.com/influxdb/v2/)
- [ESP32 MQTT Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/mqtt.html)

---

**🐝 BUMBLEBEE - Wireless Power Transfer Monitoring Dashboard**  
**Version 0.3.0** | **January 2026**