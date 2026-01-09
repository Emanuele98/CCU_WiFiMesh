# 🐝 BUMBLEBEE MONITORING DASHBOARD

## Complete Software Stack: Mosquitto → Telegraf → InfluxDB → Node-RED → nginx

[![Docker](https://img.shields.io/badge/Docker-Compose-blue)](https://docs.docker.com/compose/)
[![MQTT](https://img.shields.io/badge/MQTT-TLS--Secured-red)](https://mosquitto.org/)
[![OTA](https://img.shields.io/badge/OTA-nginx--Served-green)](https://nginx.org/)

## 🏗️ System Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              CLOUD (AWS Lightsail)                          │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │                        Docker Compose Stack                           │  │
│  │                                                                       │  │
│  │   ┌─────────────┐    ┌─────────────┐    ┌─────────────┐               │  │
│  │   │  Mosquitto  │───▶│  Telegraf   │───▶│  InfluxDB   │              │  │
│  │   │  (MQTT)     │    │  (Bridge)   │    │  (Database) │               │  │
│  │   │  :8883 TLS  │    │             │    │  :8086      │               │  │
│  │   └──────┬──────┘    └─────────────┘    └─────────────┘               │  │
│  │          │                                                            │  │
│  │          │ MQTT                                                       │  │
│  │          ▼                                                            │  │
│  │   ┌─────────────┐         ┌─────────────┐                             │  │
│  │   │  Node-RED   │────────▶│   nginx     │                            │  │
│  │   │ (Dashboard) │ upload  │ (OTA Files) │                             │  │
│  │   │  :1880      │         │  :8080      │                             │  │
│  │   └─────────────┘         └──────┬──────┘                             │  │
│  │                                  │                                    │  │
│  │                    [ota_firmware volume]                              │  │
│  │                    (shared between services)                          │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      │ MQTT :8883 (TLS)
                                      │ HTTP :8080 (OTA)
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              LOCAL NETWORK                                  │
│                                                                             │
│   ┌─────────┐                                                               │
│   │ Router  │◀─── WiFi ───────────────────────────────────────────┐        │
│   └────┬────┘                                                     │         │
│        │                                                          │         │
│        │ WiFi-Mesh-LITE                                           │         │
│        ▼                                                          │         │
│   ┌─────────┐      ┌─────────┐      ┌─────────┐                   │         │ 
│   │ MASTER  │◀────▶│   TX2   │◀────▶│   TX3   │ ...             │         │
│   │  TX1    │      │         │      │         │                   │         │
│   └────┬────┘      └────┬────┘      └────┬────┘                   │         │
│        │                │                │                        │         │
│        │ ESP-NOW        │ ESP-NOW        │ ESP-NOW                │         │
│        ▼                ▼                ▼                        │         │
│   ┌─────────┐      ┌─────────┐      ┌─────────┐              ┌────┴────┐    │
│   │   RX1   │      │   RX2   │      │   RX3   │              │ OTA DL  │    │
│   └─────────┘      └─────────┘      └─────────┘              │ (all TX)│    │
│                                                              └─────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
```

## 🔄 OTA Update Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                    PARALLEL OTA UPDATE FLOW                     │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌──────────────┐     ┌──────────────┐     ┌──────────────┐     │
│  │   UPLOAD     │     │   TRIGGER    │     │   DOWNLOAD   │     │
│  │              │     │              │     │   (Parallel) │     │
│  │  Dashboard   │────▶│  MQTT Pub    │────▶│  All nodes   │    │
│  │  /ota/upload │     │  ota/start   │     │  from nginx  │     │
│  └──────────────┘     └──────────────┘     └──────────────┘     │
│         │                    │                    │             │
│         ▼                    ▼                    ▼             │
│  ┌──────────────┐     ┌──────────────┐     ┌──────────────┐     │
│  │  Node-RED    │     │  ESP32 Nodes │     │    nginx     │     │
│  │  saves to    │     │  receive     │     │   serves     │     │
│  │  /data/ota/  │     │  trigger     │     │  firmware    │     │
│  └──────────────┘     └──────────────┘     └──────────────┘     │
│         │                    │                    │             │
│         └────────────────────┼────────────────────┘             │
│                              │                                  │
│                              ▼                                  │
│                    ┌──────────────────┐                         │
│                    │  STATUS REPORT   │                         │
│                    │                  │                         │
│                    │  Each node pubs  │                         │
│                    │  to MQTT topic:  │                         │
│                    │  bumblebee/{MAC} │                         │
│                    │  /ota/status     │                         │
│                    └──────────────────┘                         │
└─────────────────────────────────────────────────────────────────┘
```

## 🔒 Security Features

- **MQTT over TLS/SSL** (Port 8883)
- **Username/Password Authentication** for MQTT
- **Basic Auth** for nginx OTA endpoint
- **Self-signed certificates** (Development) / Let's Encrypt (Production)
- **Internal Docker network** isolation

## 📋 Quick Start

### AWS Lightsail Deployment

📘 Production deployment with full security: [AWS-DEPLOYMENT.md](AWS-DEPLOYMENT.md)

## 🐳 Docker Services

| Service | Container | Port | IP Address | Purpose |
|---------|-----------|------|------------|---------|
| Mosquitto | bumblebee-mosquitto | 8883 (TLS) | 172.20.0.2 | MQTT Broker |
| InfluxDB | bumblebee-influxdb | 8086 | 172.20.0.3 | Time-series DB |
| Telegraf | bumblebee-telegraf | - | 172.20.0.4 | MQTT→InfluxDB bridge |
| Node-RED | bumblebee-nodered | 1880 | 172.20.0.5 | Dashboard + OTA Upload |
| nginx | bumblebee-nginx | 8080 | 172.20.0.6 | OTA Firmware Server |

## 📡 MQTT Topics

### Published by ESP32
| Topic | Purpose |
|-------|---------|
| `bumblebee/{unit_id}/dynamic` | Real-time sensor data |
| `bumblebee/{unit_id}/alerts` | Alert conditions |
| `bumblebee/{MAC}/ota/status` | OTA update progress |

### Subscribed by ESP32
| Topic | Purpose |
|-------|---------|
| `bumblebee/control` | Master ON/OFF control (0 or 1) |
| `bumblebee/ota/start` | OTA update trigger |

### OTA Trigger Payload
```json
{
  "sha256": "abc123...",
  "version": "v0.0.4",
  "size": 1234567,
  "url": "http://15.188.29.195:8080/ota/firmware.bin"
}
```

### OTA Status Payload
```json
{
  "status": "downloading",
  "progress": 45,
  "version": "v0.0.4"
}
```

## 🖥️ Service URLs

### Production (AWS)

| Service | URL | Credentials |
|---------|-----|-------------|
| Dashboard | http://15.188.29.195:1880/dashboard/bumblebee | admin / bumblebee2025 |
| Node-RED | http://15.188.29.195:1880 | admin / bumblebee2025 |
| OTA Page | http://15.188.29.195:1880/dashboard/ota | admin / bumblebee2025 |
| OTA Firmware | http://15.188.29.195:8080/ota/firmware.bin | admin / bumblebee2025 |
| InfluxDB | http://15.188.29.195:8086 | admin / bumblebee2025 |
| MQTTS | mqtts://15.188.29.195:8883 | bumblebee / bumblebee2025 |

## 📋 Files Included

| File | Purpose |
|------|---------|
| `docker-compose.yml` | Service orchestration (5 services) |
| `mosquitto/config/mosquitto.conf` | MQTT broker configuration |
| `mosquitto/config/passwd` | MQTT user credentials |
| `mosquitto/certs/` | SSL/TLS certificates |
| `telegraf/telegraf.conf` | Data bridge configuration |
| `nginx/nginx.conf` | OTA server configuration |
| `nginx/.htpasswd` | nginx Basic Auth credentials |
| `nodered/settings.js` | Node-RED settings |
| `bumblebee-flowfuse-dashboard.json` | Dashboard flow |
| `start-bumblebee.bat` | Windows management script |

## 🛠️ Management Commands

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
# All services
docker compose logs -f

# Specific service
docker compose logs -f mosquitto
docker compose logs -f nginx
docker compose logs -f nodered
```

### Fix Node-RED OTA Permissions
⚠️ Run this after starting the stack for the first time:
```bash
docker exec -u 0 bumblebee-nodered sh -c "mkdir -p /data/ota/versions && chown -R node-red:node-red /data/ota && chmod -R 755 /data/ota"
```

### Test OTA Server
```bash
# Health check (no auth)
curl http://localhost:8080/health

# Download firmware (with auth)
curl -u admin:bumblebee2025 http://localhost:8080/ota/firmware.bin -o test.bin
```

### Test MQTT Connection
```bash
# With authentication
docker compose exec mosquitto mosquitto_pub \
  -h localhost -p 1883 \
  -u bumblebee -P bumblebee2025 \
  -t test/topic -m "Hello Secure MQTT"
```

## 🔐 Security Configuration

### Generate Self-Signed Certificates

```bash
mkdir -p mosquitto/certs
cd mosquitto/certs

# Generate CA certificate
openssl req -new -x509 -days 365 -extensions v3_ca -keyout ca.key -out ca.crt \
  -subj "/C=PT/ST=Braga/L=Barcelos/O=Bumblebee/OU=IoT/CN=Bumblebee-CA"

# Generate server certificate
openssl genrsa -out server.key 2048
openssl req -new -key server.key -out server.csr \
  -subj "/C=PT/ST=Braga/L=Barcelos/O=Bumblebee/OU=IoT/CN=localhost"
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -out server.crt -days 365
rm server.csr
```

### Create nginx Password File

```bash
# Install htpasswd utility
sudo apt install -y apache2-utils  # Linux
# or: brew install httpd           # macOS

# Create password file
htpasswd -cb nginx/.htpasswd admin bumblebee2025
```

### Create MQTT Password File

```bash
docker compose up -d mosquitto

docker compose exec mosquitto mosquitto_passwd -c /mosquitto/config/passwd bumblebee
docker compose exec mosquitto mosquitto_passwd /mosquitto/config/passwd telegraf
docker compose exec mosquitto mosquitto_passwd /mosquitto/config/passwd nodered
```

## 🐛 Troubleshooting

### Node-RED: Permission Denied on /data/ota

```bash
docker exec -u 0 bumblebee-nodered sh -c "mkdir -p /data/ota/versions && chown -R node-red:node-red /data/ota && chmod -R 755 /data/ota"
```

### nginx: 403 Forbidden

```bash
# Check password file exists
cat nginx/.htpasswd

# Recreate if needed
htpasswd -cb nginx/.htpasswd admin bumblebee2025
docker compose restart nginx
```

### ESP32: Can't Download Firmware

1. Check nginx is running: `docker compose ps`
2. Test URL: `curl -u admin:bumblebee2025 http://YOUR_IP:8080/ota/firmware.bin`
3. Check firewall allows port 8080
4. Verify ESP32 has correct URL and Basic Auth credentials

### TLS Handshake Failed

```bash
# Test certificate
openssl s_client -connect localhost:8883 -CAfile mosquitto/certs/ca.crt

# Check certificate dates
openssl x509 -in mosquitto/certs/server.crt -noout -dates
```

## 🎯 Production Checklist

- [ ] Changed all default passwords
- [ ] Configured firewall (ports 22, 1880, 8080, 8086, 8883)
- [ ] Enabled TLS on MQTT
- [ ] Set up Basic Auth on nginx
- [ ] Fixed Node-RED OTA permissions
- [ ] Tested firmware upload and download
- [ ] Verified ESP32 OTA update works
- [ ] Set up automated backups
- [ ] Configured log rotation

## 📞 Related Documentation

- [AWS-DEPLOYMENT.md](AWS-DEPLOYMENT.md) - Full AWS deployment guide
- [../README-FWextensive.md](../README-FWextensive.md) - Firmware documentation

---

**🐝 BUMBLEBEE - Wireless Power Transfer Monitoring System**  
**Dashboard Version 3.0 - With OTA Firmware Updates via nginx**