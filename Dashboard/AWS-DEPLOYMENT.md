# 🚀 AWS Lightsail Deployment Guide

## Bumblebee Monitoring System - Production Setup v0.3.0

Complete deployment guide for the Bumblebee wireless power transfer monitoring system on AWS Lightsail with TLS/SSL security and OTA firmware update support.

---

## 📋 Table of Contents

- [Infrastructure Overview](#infrastructure-overview)
- [Prerequisites](#prerequisites)
- [Step-by-Step Deployment](#step-by-step-deployment)
- [OTA System Configuration](#ota-system-configuration)
- [Security Hardening](#security-hardening)
- [Maintenance & Backup](#maintenance--backup)
- [Troubleshooting](#troubleshooting)
- [Production Checklist](#production-checklist)

---

## 🏗️ Infrastructure Overview

```
┌────────────────────────────────────────────────────────────────┐
│                   AWS Lightsail Instance                       │
│                   Ubuntu 22.04 LTS                             │
│                   $10/month (2GB RAM, 60GB SSD)                │
│                                                                │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                 Docker Compose Stack                      │  │
│  │                                                           │  │
│  │  ┌───────────┐  ┌───────────┐  ┌───────────┐            │  │
│  │  │ Mosquitto │  │ InfluxDB  │  │ Telegraf  │            │  │
│  │  │   MQTT    │  │    DB     │  │  Bridge   │            │  │
│  │  │   :8883   │  │   :8086   │  │           │            │  │
│  │  └───────────┘  └───────────┘  └───────────┘            │  │
│  │                                                           │  │
│  │  ┌───────────────────────────────────────────────────┐   │  │
│  │  │              Node-RED Dashboard                    │   │  │
│  │  │  - Real-time monitoring      (:1880)              │   │  │
│  │  │  - OTA firmware hosting      (/ota/firmware.bin)  │   │  │
│  │  │  - OTA management UI         (/dashboard/ota)     │   │  │
│  │  └───────────────────────────────────────────────────┘   │  │
│  │                                                           │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                │
│  Firewall Rules:                                               │
│  - 22 (SSH)                                                    │
│  - 1880 (Node-RED Dashboard + OTA)                            │
│  - 8086 (InfluxDB)                                            │
│  - 8883 (MQTT/TLS)                                            │
└────────────────────────────────────────────────────────────────┘
```

### Service Endpoints

| Service | Port | Protocol | Purpose |
|---------|------|----------|---------|
| Mosquitto | 8883 | MQTTS | Secure MQTT broker |
| Mosquitto | 1883 | MQTT | Internal Docker only |
| InfluxDB | 8086 | HTTP | Time-series database |
| Node-RED | 1880 | HTTP | Dashboard & OTA server |
| Telegraf | — | Internal | MQTT → InfluxDB bridge |

---

## 📝 Prerequisites

- AWS Account with billing enabled
- Basic Linux/SSH knowledge
- Local development environment tested
- Domain name (optional, for proper SSL)

---

## 🔧 Step-by-Step Deployment

### Step 1: Create Lightsail Instance

1. Log into [AWS Lightsail Console](https://lightsail.aws.amazon.com/)
2. Click **"Create Instance"**
3. Select:
   - **Platform**: Linux/Unix
   - **Blueprint**: Ubuntu 22.04 LTS
   - **Instance Plan**: $10/month (2GB RAM, 60GB SSD) minimum
   - **Instance Name**: `bumblebee-monitor`
4. Click **"Create Instance"**

### Step 2: Configure Firewall

Navigate to your instance → **Networking** → Add rules:

| Application | Protocol | Port | Restrict to |
|-------------|----------|------|-------------|
| SSH | TCP | 22 | Your IP (recommended) |
| Custom | TCP | 1880 | Any |
| Custom | TCP | 8086 | Any |
| Custom | TCP | 8883 | Any |

### Step 3: Assign Static IP

1. Go to **Networking** tab
2. Click **"Create static IP"**
3. Attach to your instance
4. Note the IP: `15.188.29.195`

### Step 4: Connect via SSH

```bash
# Using Lightsail browser console, or:
ssh -i /path/to/key.pem ubuntu@15.188.29.195
```

### Step 5: Install Docker

```bash
# Update system
sudo apt update && sudo apt upgrade -y

# Install prerequisites
sudo apt install -y ca-certificates curl gnupg lsb-release

# Add Docker's GPG key
sudo mkdir -p /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | \
  sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg

# Set up repository
echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] \
  https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable" | \
  sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

# Install Docker
sudo apt update
sudo apt install -y docker-ce docker-ce-cli containerd.io docker-compose-plugin

# Add user to docker group
sudo usermod -aG docker ubuntu
newgrp docker

# Verify
docker --version
docker compose version
```

### Step 6: Create Project Structure

```bash
mkdir -p ~/bumblebee-monitoring
cd ~/bumblebee-monitoring
mkdir -p mosquitto/config mosquitto/certs telegraf nodered
```

### Step 7: Generate TLS Certificates

#### Option A: Self-Signed (Development/Testing)

```bash
cd ~/bumblebee-monitoring/mosquitto/certs

# Generate CA certificate
openssl req -new -x509 -days 365 -extensions v3_ca \
  -keyout ca.key -out ca.crt \
  -subj "/C=US/ST=State/L=City/O=Bumblebee/OU=IoT/CN=Bumblebee-CA"

# Generate server key
openssl genrsa -out server.key 2048

# Generate server certificate request
openssl req -new -key server.key -out server.csr \
  -subj "/C=US/ST=State/L=City/O=Bumblebee/OU=IoT/CN=15.188.29.195"

# Sign the certificate
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key \
  -CAcreateserial -out server.crt -days 365

# Set permissions
chmod 644 ca.crt server.crt
chmod 600 server.key ca.key
rm server.csr

cd ~/bumblebee-monitoring
```

#### Option B: Let's Encrypt (Production with Domain)

```bash
sudo apt install -y certbot
sudo certbot certonly --standalone -d your-domain.com

sudo cp /etc/letsencrypt/live/your-domain.com/fullchain.pem mosquitto/certs/server.crt
sudo cp /etc/letsencrypt/live/your-domain.com/privkey.pem mosquitto/certs/server.key
sudo cp /etc/letsencrypt/live/your-domain.com/chain.pem mosquitto/certs/ca.crt
sudo chown -R ubuntu:ubuntu mosquitto/certs/
```

### Step 8: Create Configuration Files

#### 8.1 docker-compose.yml

```yaml
version: '3.8'

services:
  # ===============================================
  # MOSQUITTO - Secure MQTT Broker
  # ===============================================
  mosquitto:
    image: eclipse-mosquitto:2.0
    container_name: bumblebee-mosquitto
    ports:
      - "1883:1883"
      - "8883:8883"
      - "9001:9001"
    volumes:
      - ./mosquitto/config/mosquitto.conf:/mosquitto/config/mosquitto.conf
      - ./mosquitto/config/passwd:/mosquitto/config/passwd
      - ./mosquitto/certs:/mosquitto/certs:ro
      - mosquitto_data:/mosquitto/data
      - mosquitto_log:/mosquitto/log
    networks:
      bumblebee-network:
        ipv4_address: 172.20.0.2
    restart: unless-stopped
    healthcheck:
      test: ["CMD-SHELL", "timeout 5 mosquitto_sub -t '$$SYS/#' -C 1 -u telegraf -P bumblebee2025 || exit 1"]
      interval: 30s
      timeout: 10s
      retries: 3

  # ===============================================
  # INFLUXDB - Time Series Database
  # ===============================================
  influxdb:
    image: influxdb:2.7
    container_name: bumblebee-influxdb
    ports:
      - "8086:8086"
    volumes:
      - influxdb_data:/var/lib/influxdb2
      - influxdb_config:/etc/influxdb2
    environment:
      - DOCKER_INFLUXDB_INIT_MODE=setup
      - DOCKER_INFLUXDB_INIT_USERNAME=admin
      - DOCKER_INFLUXDB_INIT_PASSWORD=bumblebee2025
      - DOCKER_INFLUXDB_INIT_ORG=bumblebee
      - DOCKER_INFLUXDB_INIT_BUCKET=sensor_data
      - DOCKER_INFLUXDB_INIT_ADMIN_TOKEN=bumblebee-super-secret-token
      - DOCKER_INFLUXDB_INIT_RETENTION=0
    networks:
      bumblebee-network:
        ipv4_address: 172.20.0.3
    restart: unless-stopped
    healthcheck:
      test: ["CMD", "influx", "ping"]
      interval: 30s
      timeout: 10s
      retries: 3

  # ===============================================
  # TELEGRAF - Data Bridge
  # ===============================================
  telegraf:
    image: telegraf:latest
    container_name: bumblebee-telegraf
    volumes:
      - ./telegraf/telegraf.conf:/etc/telegraf/telegraf.conf:ro
    environment:
      - MQTT_USERNAME=telegraf
      - MQTT_PASSWORD=bumblebee2025
    depends_on:
      mosquitto:
        condition: service_healthy
      influxdb:
        condition: service_healthy
    networks:
      bumblebee-network:
        ipv4_address: 172.20.0.4
    restart: unless-stopped

  # ===============================================
  # NODE-RED - Dashboard & OTA Server
  # ===============================================
  nodered:
    image: nodered/node-red:latest
    container_name: bumblebee-nodered
    ports:
      - "1880:1880"
    volumes:
      - nodered_data:/data
      - nodered_ota:/data/ota
      - ./nodered/settings.js:/data/settings.js:ro
    environment:
      - TZ=Europe/Rome
      - NODE_RED_ENABLE_PROJECTS=false
      - MQTT_USERNAME=nodered
      - MQTT_PASSWORD=bumblebee2025
      - NODE_RED_CREDENTIAL_SECRET=bumblebee_secret_key_2025
    depends_on:
      mosquitto:
        condition: service_healthy
    networks:
      bumblebee-network:
        ipv4_address: 172.20.0.5
    restart: unless-stopped
    command: >
      sh -c "mkdir -p /data/ota/versions && 
             chmod -R 755 /data/ota &&
             npm start -- --userDir /data"

networks:
  bumblebee-network:
    driver: bridge
    ipam:
      config:
        - subnet: 172.20.0.0/16

volumes:
  mosquitto_data:
  mosquitto_log:
  influxdb_data:
  influxdb_config:
  nodered_data:
  nodered_ota:
```

#### 8.2 mosquitto/config/mosquitto.conf

```conf
# Bumblebee MQTT Broker - Secure Configuration
per_listener_settings true
allow_anonymous false

# Secure MQTT (Port 8883 with TLS)
listener 8883
protocol mqtt
certfile /mosquitto/certs/server.crt
keyfile /mosquitto/certs/server.key
cafile /mosquitto/certs/ca.crt
require_certificate false
password_file /mosquitto/config/passwd
tls_version tlsv1.2

# Internal MQTT (Port 1883 - Docker network only)
listener 1883 0.0.0.0
protocol mqtt
allow_anonymous false
password_file /mosquitto/config/passwd

# WebSocket (Port 9001 with TLS)
listener 9001
protocol websockets
certfile /mosquitto/certs/server.crt
keyfile /mosquitto/certs/server.key
cafile /mosquitto/certs/ca.crt
password_file /mosquitto/config/passwd

# Persistence
persistence true
persistence_location /mosquitto/data/
autosave_interval 300

# Logging
log_dest file /mosquitto/log/mosquitto.log
log_dest stdout
log_type error
log_type warning
log_type notice
log_type information
connection_messages true
log_timestamp true
log_timestamp_format %Y-%m-%dT%H:%M:%S

# Performance
max_connections -1
max_keepalive 65535
message_size_limit 10240
```

#### 8.3 telegraf/telegraf.conf

```toml
[global_tags]
  system = "bumblebee"
  environment = "production"

[agent]
  interval = "1s"
  round_interval = true
  metric_batch_size = 1000
  metric_buffer_limit = 10000
  flush_interval = "1s"
  hostname = "bumblebee-telegraf"

[[inputs.mqtt_consumer]]
  servers = ["tcp://172.20.0.2:1883"]
  topics = ["bumblebee/+/dynamic", "bumblebee/+/alerts"]
  qos = 1
  client_id = "telegraf_bumblebee"
  username = "${MQTT_USERNAME}"
  password = "${MQTT_PASSWORD}"
  data_format = "json_v2"

  [[inputs.mqtt_consumer.json_v2]]
    measurement_name = "bumblebee_dynamic"
    
    [[inputs.mqtt_consumer.json_v2.tag]]
      path = "unit_id"
    
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "tx.voltage"
      rename = "tx_voltage"
      type = "float"
    
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "tx.current"
      rename = "tx_current"
      type = "float"
    
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "rx.voltage"
      rename = "rx_voltage"
      type = "float"
    
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "rx.current"
      rename = "rx_current"
      type = "float"

[[outputs.influxdb_v2]]
  urls = ["http://172.20.0.3:8086"]
  token = "bumblebee-super-secret-token"
  organization = "bumblebee"
  bucket = "sensor_data"
```

#### 8.4 nodered/settings.js

```javascript
module.exports = {
    flowFile: 'flows.json',
    flowFilePretty: true,
    credentialSecret: process.env.NODE_RED_CREDENTIAL_SECRET || "bumblebee_secret_key_2025",
    
    // Authentication
    adminAuth: {
        type: "credentials",
        users: [{
            username: "admin",
            password: "$2b$08$CNi/3RnATkuVlB4QhkcyZOYIxpWgR2pgZKJP4Z7CPij5yEkCG1o3q",
            permissions: "*"
        }]
    },
    
    // HTTP node authentication (for OTA endpoints)
    httpNodeAuth: {
        user: "admin",
        pass: "$2b$08$CNi/3RnATkuVlB4QhkcyZOYIxpWgR2pgZKJP4Z7CPij5yEkCG1o3q"
    },
    
    uiPort: process.env.PORT || 1880,
    uiHost: "0.0.0.0",
    httpAdminRoot: '/',
    httpNodeRoot: '/',
    
    httpNodeCors: {
        origin: "*",
        methods: "GET,PUT,POST,DELETE"
    },
    
    logging: {
        console: {
            level: "info",
            metrics: false,
            audit: false
        }
    },
    
    functionGlobalContext: {
        os: require('os'),
        fs: require('fs'),
        path: require('path'),
        crypto: require('crypto')
    },
    
    apiMaxLength: '10mb',
    
    diagnostics: {
        enabled: false
    }
}
```

### Step 9: Create MQTT Users

```bash
# Start Mosquitto first
docker compose up -d mosquitto

# Create password file with users
docker compose exec mosquitto mosquitto_passwd -c /mosquitto/config/passwd bumblebee
# Enter: bumblebee2025

docker compose exec mosquitto mosquitto_passwd /mosquitto/config/passwd telegraf
# Enter: bumblebee2025

docker compose exec mosquitto mosquitto_passwd /mosquitto/config/passwd nodered
# Enter: bumblebee2025

# Verify
docker compose exec mosquitto cat /mosquitto/config/passwd
```

### Step 10: Start All Services

```bash
docker compose up -d
docker compose ps
docker compose logs -f
```

### Step 11: Configure Node-RED OTA Directory

```bash
# Create OTA directories with proper permissions
docker exec -u 0 bumblebee-nodered mkdir -p /data/ota/versions
docker exec -u 0 bumblebee-nodered chown -R node-red:node-red /data/ota
```

### Step 12: Import Node-RED Dashboard

1. Open Node-RED: http://15.188.29.195:1880
2. Login: admin / bumblebee2025
3. Click hamburger menu → Import
4. Import your dashboard flow JSON
5. Configure MQTT nodes:
   - Server: `172.20.0.2`
   - Port: `1883`
   - Username: `nodered`
   - Password: `bumblebee2025`
6. Deploy

---

## 🔄 OTA System Configuration

### OTA Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Node-RED OTA System                       │
│                                                              │
│  Dashboard (/dashboard/ota)                                  │
│  ├── Upload firmware.bin                                    │
│  ├── Auto-calculate SHA256                                  │
│  ├── Store in /data/ota/firmware.bin                       │
│  └── Trigger OTA via MQTT                                   │
│                                                              │
│  HTTP Endpoint (/ota/firmware.bin)                          │
│  ├── Basic Auth: admin:bumblebee2025                        │
│  └── Serves firmware to ESP32                               │
│                                                              │
│  MQTT Topics                                                 │
│  ├── bumblebee/ota/start (trigger)                          │
│  └── bumblebee/+/ota/status (progress)                      │
└─────────────────────────────────────────────────────────────┘
```

### OTA Trigger Flow

1. Upload firmware via dashboard
2. SHA256 calculated automatically
3. Click "Trigger OTA"
4. MQTT publishes: `bumblebee/ota/start` with `{"sha256":"..."}`
5. ESP32 ROOT receives command
6. ESP32 downloads from: `http://15.188.29.195:1880/ota/firmware.bin`
7. ESP32 verifies SHA256
8. ESP32 flashes and reboots

### Manual OTA Trigger

```bash
# Via MQTT
mosquitto_pub -h 15.188.29.195 -p 8883 \
  --cafile ca.crt \
  -u bumblebee -P bumblebee2025 \
  -t "bumblebee/ota/start" \
  -m '{"sha256":"YOUR_64_CHAR_SHA256_HASH"}'
```

### Test OTA Endpoint

```bash
# Test firmware download
curl -u admin:bumblebee2025 \
  http://15.188.29.195:1880/ota/firmware.bin \
  -o test.bin

# Verify download
sha256sum test.bin
ls -la test.bin
```

### Future Enhancement: nginx Integration

In a future version, nginx will replace the Node-RED HTTP endpoint to enable concurrent firmware downloads for all mesh nodes simultaneously. This will require:

1. Adding nginx container to docker-compose.yml
2. Shared volume between Node-RED and nginx for firmware files
3. Updated ESP32 firmware URL configuration
4. Mesh broadcast of OTA command to all nodes

---

## 🔒 Security Hardening

### 1. Restrict SSH Access

```bash
sudo nano /etc/ssh/sshd_config

# Add:
PermitRootLogin no
PasswordAuthentication no
PubkeyAuthentication yes
AllowUsers ubuntu

sudo systemctl restart sshd
```

### 2. Enable UFW Firewall

```bash
sudo ufw default deny incoming
sudo ufw default allow outgoing
sudo ufw allow 22/tcp
sudo ufw allow 1880/tcp
sudo ufw allow 8086/tcp
sudo ufw allow 8883/tcp
sudo ufw --force enable
sudo ufw status verbose
```

### 3. Install Fail2Ban

```bash
sudo apt install -y fail2ban
sudo cp /etc/fail2ban/jail.conf /etc/fail2ban/jail.local
sudo systemctl enable fail2ban
sudo systemctl start fail2ban
```

### 4. Configure Log Rotation

```bash
cat > /etc/logrotate.d/bumblebee << 'EOF'
/home/ubuntu/bumblebee-monitoring/mosquitto/log/*.log {
    daily
    missingok
    rotate 7
    compress
    notifempty
}
EOF
```

---

## 🔧 Maintenance & Backup

### Automated Backup Script

```bash
cat > ~/backup-bumblebee.sh << 'EOF'
#!/bin/bash
BACKUP_DIR=~/backups/$(date +%Y%m%d_%H%M%S)
mkdir -p $BACKUP_DIR

cd ~/bumblebee-monitoring

# Backup InfluxDB
docker run --rm \
  -v bumblebee-monitoring_influxdb_data:/data \
  -v $BACKUP_DIR:/backup \
  alpine tar czf /backup/influxdb.tar.gz -C /data .

# Backup Node-RED flows
docker run --rm \
  -v bumblebee-monitoring_nodered_data:/data \
  -v $BACKUP_DIR:/backup \
  alpine tar czf /backup/nodered.tar.gz -C /data .

# Backup configurations
tar czf $BACKUP_DIR/configs.tar.gz mosquitto/ telegraf/ docker-compose.yml

echo "Backup completed: $BACKUP_DIR"

# Delete backups older than 30 days
find ~/backups -type d -mtime +30 -exec rm -rf {} \;
EOF

chmod +x ~/backup-bumblebee.sh

# Schedule daily backup at 2 AM
(crontab -l 2>/dev/null; echo "0 2 * * * /home/ubuntu/backup-bumblebee.sh") | crontab -
```

### Service Management Commands

```bash
# Start all services
docker compose up -d

# Stop all services
docker compose down

# Restart specific service
docker compose restart nodered

# View logs
docker compose logs -f
docker compose logs -f mosquitto
docker compose logs -f nodered

# Check resource usage
docker stats
```

### Certificate Renewal (Let's Encrypt)

```bash
cat > ~/renew-certs.sh << 'EOF'
#!/bin/bash
certbot renew --quiet
if [ $? -eq 0 ]; then
    cp /etc/letsencrypt/live/your-domain.com/fullchain.pem ~/bumblebee-monitoring/mosquitto/certs/server.crt
    cp /etc/letsencrypt/live/your-domain.com/privkey.pem ~/bumblebee-monitoring/mosquitto/certs/server.key
    cd ~/bumblebee-monitoring && docker compose restart mosquitto
fi
EOF

chmod +x ~/renew-certs.sh

# Schedule weekly renewal
(crontab -l 2>/dev/null; echo "0 3 * * 1 /home/ubuntu/renew-certs.sh") | crontab -
```

---

## 🐛 Troubleshooting

### Connection Issues

```bash
# Test MQTT connectivity
openssl s_client -connect 15.188.29.195:8883 -CAfile mosquitto/certs/ca.crt

# Check certificate validity
openssl x509 -in mosquitto/certs/server.crt -noout -dates

# Monitor MQTT traffic
docker compose exec mosquitto mosquitto_sub -t '#' -v -u bumblebee -P bumblebee2025
```

### OTA Issues

```bash
# Check OTA directory
docker exec bumblebee-nodered ls -la /data/ota/

# Test firmware endpoint
curl -v -u admin:bumblebee2025 http://localhost:1880/ota/firmware.bin

# Check Node-RED logs
docker compose logs -f nodered | grep -i ota
```

### Performance Issues

```bash
# Check resource usage
htop
docker stats

# Check disk space
df -h

# Clean Docker resources
docker system prune -a
docker volume prune
```

---

## ✅ Production Checklist

### Security

- [ ] Changed all default passwords
- [ ] Configured firewall rules
- [ ] Enabled TLS on MQTT
- [ ] Restricted SSH access
- [ ] Set up fail2ban
- [ ] Updated Node-RED admin password

### Monitoring

- [ ] Health check script scheduled
- [ ] Log rotation configured
- [ ] Resource monitoring enabled
- [ ] Alerts configured

### Backup

- [ ] Automated backups scheduled
- [ ] Backup retention policy set
- [ ] Restore procedure tested

### OTA

- [ ] OTA directory created with permissions
- [ ] HTTP endpoint tested
- [ ] SHA256 verification working
- [ ] ESP32 can download firmware

### Testing

- [ ] ESP32 MQTT connections verified
- [ ] Dashboard functionality confirmed
- [ ] OTA update tested end-to-end
- [ ] Failover tested

---

## 📊 Access Points Summary

| Service | URL | Credentials |
|---------|-----|-------------|
| Dashboard | http://15.188.29.195:1880/dashboard/bumblebee | — |
| Node-RED Editor | http://15.188.29.195:1880 | admin / bumblebee2025 |
| OTA Firmware | http://15.188.29.195:1880/ota/firmware.bin | admin / bumblebee2025 |
| InfluxDB | http://15.188.29.195:8086 | admin / bumblebee2025 |
| MQTT (TLS) | mqtts://15.188.29.195:8883 | bumblebee / bumblebee2025 |

---

**🐝 BUMBLEBEE - Production Wireless Power Transfer Monitoring**  
**Version 0.3.0 - Secured with TLS/SSL + OTA Support**  
**Last Updated:** January 2026