# 🚀 AWS LIGHTSAIL DEPLOYMENT GUIDE - SECURE PRODUCTION SETUP

## Complete deployment guide for Bumblebee monitoring system on AWS Lightsail with TLS/SSL security and OTA firmware updates

## 📋 Prerequisites

- AWS Account with billing enabled
- Basic Linux/SSH knowledge
- Domain name (optional, for proper SSL certificates)
- Local development environment tested and working

## 🏗️ Infrastructure Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                    AWS Lightsail Instance                       │
│                                                                 │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │                  Docker Compose Stack                     │  │
│  │                                                           │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐        │  │
│  │  │  Mosquitto  │  │  InfluxDB   │  │  Telegraf   │        │  │
│  │  │  (MQTT)     │  │  (DB)       │  │  (Bridge)   │        │  │
│  │  │  172.20.0.2 │  │  172.20.0.3 │  │  172.20.0.4 │        │  │
│  │  │  :8883 TLS  │  │  :8086      │  │             │        │  │
│  │  └─────────────┘  └─────────────┘  └─────────────┘        │  │
│  │                                                           │  │
│  │  ┌─────────────┐  ┌─────────────┐                         │  │
│  │  │  Node-RED   │  │   nginx     │                         │  │
│  │  │ (Dashboard) │  │ (OTA Files) │                         │  │
│  │  │  172.20.0.5 │  │  172.20.0.6 │                         │  │
│  │  │  :1880      │  │  :8080      │                         │  │
│  │  └──────┬──────┘  └──────┬──────┘                         │  │
│  │         │                │                                │  │
│  │         └───────┬────────┘                                │  │
│  │                 │                                         │  │
│  │         [ota_firmware volume]                             │  │
│  │         (shared for OTA updates)                          │  │
│  │                                                           │  │
│  └───────────────────────────────────────────────────────────┘  │
│                                                                 │
│         Firewall Rules:                                         │
│         - 22   (SSH)                                            │
│         - 1880 (Node-RED Dashboard)                             │
│         - 8080 (nginx OTA Firmware)                             │
│         - 8086 (InfluxDB)                                       │
│         - 8883 (MQTT/TLS)                                       │
└─────────────────────────────────────────────────────────────────┘
```

## 🔄 OTA Update Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     OTA FIRMWARE UPDATE FLOW                    │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1. Upload Firmware                                             │
│     └─> Node-RED Dashboard (/ota/upload)                        │
│         └─> Calculate SHA256                                    │
│             └─> Save to /data/ota/firmware.bin                  │
│                 └─> [ota_firmware volume]                       │
│                                                                 │
│  2. Trigger OTA                                                 │
│     └─> Dashboard "Trigger OTA" button                          │
│         └─> MQTT: bumblebee/ota/start                           │
│             └─> Payload: {"sha256":"...","version":"...",       │
│                          "url":"http://IP:8080/ota/firmware.bin"}
│                                                                 │
│  3. ESP32 Nodes Download (Parallel)                             │
│     └─> All nodes receive MQTT trigger                          │
│         └─> Each node downloads from nginx :8080                │
│             └─> Basic Auth: admin/bumblebee2025                 │
│                 └─> Verify SHA256                               │
│                     └─> Flash & Reboot                          │
│                                                                 │
│  4. Status Reporting                                            │
│     └─> Each node publishes to: bumblebee/{MAC}/ota/status      │
│         └─> Dashboard shows per-node progress                   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

## 📝 Step-by-Step Deployment

### Step 1: Create Lightsail Instance

1. Log into [AWS Lightsail Console](https://lightsail.aws.amazon.com/)
2. Click **"Create Instance"**
3. Select:
   - **Platform**: Linux/Unix
   - **Blueprint**: Ubuntu 22.04 LTS
   - **Instance Plan**: $10/month (2GB RAM, 60GB SSD) or higher
   - **Instance Name**: `bumblebee-monitor`
4. Click **"Create Instance"**

### Step 2: Configure Firewall

1. Click on your instance → **Networking tab**
2. Add firewall rules:

| Application | Protocol | Port | Restrict to | Purpose |
|-------------|----------|------|-------------|---------|
| SSH | TCP | 22 | Your IP (recommended) | Remote access |
| Custom | TCP | 1880 | Specific IPs or Any | Node-RED Dashboard |
| Custom | TCP | 8080 | Any | nginx OTA Firmware Server |
| Custom | TCP | 8086 | Specific IPs or Any | InfluxDB |
| Custom | TCP | 8883 | Any (for ESP32s) | MQTT over TLS |

### Step 3: Assign Static IP

1. Go to **Networking tab**
2. Click **"Create static IP"**
3. Attach to your instance
4. Note the IP (e.g., `15.188.29.195`)

### Step 4: Connect via SSH

```bash
# Using Lightsail browser console
# Or from your terminal:
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
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg

# Set up repository
echo \
  "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu \
  $(lsb_release -cs) stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

# Install Docker
sudo apt update
sudo apt install -y docker-ce docker-ce-cli containerd.io docker-compose-plugin

# Add user to docker group
sudo usermod -aG docker ubuntu
newgrp docker

# Verify installation
docker --version
docker compose version
```

### Step 6: Create Project Structure

```bash
# Create directories
mkdir -p ~/bumblebee
cd ~/bumblebee
mkdir -p mosquitto/config mosquitto/certs telegraf nodered nginx
```

### Step 7: Generate SSL/TLS Certificates

```bash
cd ~/bumblebee/mosquitto/certs

# Generate CA certificate
openssl req -new -x509 -days 365 -extensions v3_ca -keyout ca.key -out ca.crt \
  -subj "/C=PT/ST=Braga/L=Barcelos/O=Bumblebee/OU=IoT/CN=Bumblebee-CA"

# Generate server key
openssl genrsa -out server.key 2048

# Generate server certificate request
openssl req -new -key server.key -out server.csr \
  -subj "/C=PT/ST=Braga/L=Barcelos/O=Bumblebee/OU=IoT/CN=15.188.29.195"

# Sign the certificate
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -out server.crt -days 365

# Set permissions
chmod 644 ca.crt server.crt
chmod 600 server.key ca.key
rm server.csr

cd ~/bumblebee
```

### Step 8: Create Configuration Files

#### 8.1 Create docker-compose.yml

```bash
cat > docker-compose.yml << 'EOF'
version: '3.8'

services:
  # ===============================================
  # MOSQUITTO - MQTT Broker (SECURE)
  # ===============================================
  mosquitto:
    image: eclipse-mosquitto:2.0
    container_name: bumblebee-mosquitto
    ports:
      - "1883:1883"      # MQTT (internal)
      - "8883:8883"      # MQTT over TLS (secure)
      - "9001:9001"      # WebSocket over TLS
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
      test: ["CMD-SHELL", "nc -z 127.0.0.1 1883 || exit 1"]
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
  # TELEGRAF - Data Bridge (MQTT to InfluxDB)
  # ===============================================
  telegraf:
    image: telegraf:1.28
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
  # NODE-RED - Real-time Dashboard + OTA Upload
  # ===============================================
  nodered:
    image: nodered/node-red:latest
    container_name: bumblebee-nodered
    ports:
      - "1880:1880"
    volumes:
      - nodered_data:/data
      - ota_firmware:/data/ota
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

  # ===============================================
  # NGINX - OTA Firmware Server
  # ===============================================
  nginx:
    image: nginx:alpine
    container_name: bumblebee-nginx
    ports:
      - "8080:80"
    volumes:
      - ota_firmware:/usr/share/nginx/html/ota:ro
      - ./nginx/nginx.conf:/etc/nginx/nginx.conf:ro
      - ./nginx/.htpasswd:/etc/nginx/.htpasswd:ro
    networks:
      bumblebee-network:
        ipv4_address: 172.20.0.6
    restart: unless-stopped
    healthcheck:
      test: ["CMD", "wget", "-q", "--spider", "http://localhost/health"]
      interval: 30s
      timeout: 10s
      retries: 3

# ===============================================
# NETWORKS
# ===============================================
networks:
  bumblebee-network:
    driver: bridge
    ipam:
      config:
        - subnet: 172.20.0.0/16

# ===============================================
# VOLUMES (Persistent Data)
# ===============================================
volumes:
  mosquitto_data:
  mosquitto_log:
  influxdb_data:
  influxdb_config:
  nodered_data:
  ota_firmware:
EOF
```

#### 8.2 Create Mosquitto Configuration

```bash
cat > mosquitto/config/mosquitto.conf << 'EOF'
# BUMBLEBEE - Production MQTT Configuration

per_listener_settings true
allow_anonymous false

# Secure External Listener (Port 8883)
listener 8883 0.0.0.0
protocol mqtt
certfile /mosquitto/certs/server.crt
keyfile /mosquitto/certs/server.key
cafile /mosquitto/certs/ca.crt
require_certificate false
password_file /mosquitto/config/passwd
tls_version tlsv1.2

# Internal Listener (Port 1883 - Docker network only)
listener 1883 172.20.0.2
protocol mqtt
allow_anonymous false
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
EOF
```

#### 8.3 Create Telegraf Configuration

```bash
cat > telegraf/telegraf.conf << 'EOF'
[agent]
  interval = "10s"
  round_interval = true
  metric_batch_size = 1000
  metric_buffer_limit = 10000
  flush_interval = "10s"
  hostname = "bumblebee-telegraf"

[[inputs.mqtt_consumer]]
  servers = ["tcp://172.20.0.2:1883"]
  topics = [
    "bumblebee/+/dynamic",
    "bumblebee/+/alerts"
  ]
  username = "telegraf"
  password = "bumblebee2025"
  data_format = "json"
  json_strict = false

[[outputs.influxdb_v2]]
  urls = ["http://172.20.0.3:8086"]
  token = "bumblebee-super-secret-token"
  organization = "bumblebee"
  bucket = "sensor_data"
EOF
```

#### 8.4 Create nginx Configuration

```bash
cat > nginx/nginx.conf << 'EOF'
worker_processes auto;
error_log /var/log/nginx/error.log warn;
pid /var/run/nginx.pid;

events {
    worker_connections 1024;
}

http {
    include /etc/nginx/mime.types;
    default_type application/octet-stream;

    log_format main '$remote_addr - $remote_user [$time_local] "$request" '
                    '$status $body_bytes_sent "$http_referer" '
                    '"$http_user_agent" "$http_x_forwarded_for"';

    access_log /var/log/nginx/access.log main;

    sendfile on;
    tcp_nopush on;
    tcp_nodelay on;
    keepalive_timeout 65;

    # Optimized for ESP32 firmware downloads
    client_max_body_size 10M;
    proxy_read_timeout 300;
    proxy_connect_timeout 300;

    server {
        listen 80;
        server_name _;

        # Health check endpoint (no auth required)
        location /health {
            access_log off;
            return 200 "OK\n";
            add_header Content-Type text/plain;
        }

        # OTA firmware endpoint (Basic Auth required)
        location /ota/ {
            alias /usr/share/nginx/html/ota/;
            
            auth_basic "OTA Firmware";
            auth_basic_user_file /etc/nginx/.htpasswd;

            # CORS headers for ESP32
            add_header 'Access-Control-Allow-Origin' '*' always;
            add_header 'Access-Control-Allow-Methods' 'GET, OPTIONS' always;
            add_header 'Access-Control-Allow-Headers' 'Authorization' always;

            # Handle preflight
            if ($request_method = 'OPTIONS') {
                add_header 'Access-Control-Allow-Origin' '*';
                add_header 'Access-Control-Allow-Methods' 'GET, OPTIONS';
                add_header 'Access-Control-Allow-Headers' 'Authorization';
                add_header 'Access-Control-Max-Age' 1728000;
                add_header 'Content-Type' 'text/plain charset=UTF-8';
                add_header 'Content-Length' 0;
                return 204;
            }

            autoindex off;
            try_files $uri =404;
        }
    }
}
EOF
```

#### 8.5 Create nginx Password File

```bash
# Install apache2-utils for htpasswd (if not already installed)
sudo apt install -y apache2-utils

# Create password file
htpasswd -cb nginx/.htpasswd admin bumblebee2025
```

### Step 9: Create MQTT User Accounts

```bash
# Start mosquitto first
docker compose up -d mosquitto

# Create password file with users
docker compose exec mosquitto mosquitto_passwd -c /mosquitto/config/passwd bumblebee
# Enter password: bumblebee2025

docker compose exec mosquitto mosquitto_passwd /mosquitto/config/passwd telegraf
# Enter password: bumblebee2025

docker compose exec mosquitto mosquitto_passwd /mosquitto/config/passwd nodered
# Enter password: bumblebee2025

# Restart mosquitto to apply
docker compose restart mosquitto
```

### Step 10: Start All Services

```bash
# Start the complete stack
docker compose up -d

# Verify all services are running
docker compose ps

# Check logs
docker compose logs -f
```

### Step 11: Fix Node-RED OTA Directory Permissions

⚠️ **IMPORTANT**: After starting the stack for the first time (or after recreating volumes), you must fix the OTA directory permissions:

```bash
# Fix Node-RED OTA directory permissions
docker exec -u 0 bumblebee-nodered sh -c "mkdir -p /data/ota/versions && chown -R node-red:node-red /data/ota && chmod -R 755 /data/ota"
```

> **Note**: This command must be run as root (`-u 0`) because the `chown` command in docker-compose runs as the `node-red` user which doesn't have permission to change ownership. Run this command every time you recreate the volumes.

### Step 12: Import Node-RED Dashboard

1. Open Node-RED: http://15.188.29.195:1880
2. Click hamburger menu → Import
3. Copy/paste your flow JSON
4. Configure MQTT nodes:
   - Server: `172.20.0.2`
   - Port: `1883`
   - Security:
     - Username: `nodered`
     - Password: `bumblebee2025`
5. Deploy
6. Access dashboard: http://15.188.29.195:1880/dashboard/bumblebee

### Step 13: Test OTA Firmware Server

```bash
# Test nginx health endpoint (no auth)
curl http://15.188.29.195:8080/health

# Test OTA endpoint (with auth)
curl -u admin:bumblebee2025 http://15.188.29.195:8080/ota/

# After uploading firmware via Node-RED:
curl -u admin:bumblebee2025 http://15.188.29.195:8080/ota/firmware.bin -o test.bin
```

### Step 14: Configure ESP32 Devices

Update ESP32 firmware with production settings:

```c
// main/include/mqtt_client_manager.h
#define MQTT_BROKER_HOST "15.188.29.195"
#define MQTT_BROKER_PORT 8883  // Secure port
#define MQTT_USERNAME "bumblebee"
#define MQTT_PASSWORD "bumblebee2025"

// main/include/ota_manager.h
#define OTA_FIRMWARE_URL "http://15.188.29.195:8080/ota/firmware.bin"
#define OTA_HTTP_USERNAME "admin"
#define OTA_HTTP_PASSWORD "bumblebee2025"
```

## 🔒 Security Hardening

### 1. Restrict SSH Access

```bash
sudo nano /etc/ssh/sshd_config

# Add these lines:
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
sudo ufw allow 22/tcp        # SSH
sudo ufw allow 1880/tcp      # Node-RED
sudo ufw allow 8080/tcp      # nginx OTA
sudo ufw allow 8086/tcp      # InfluxDB
sudo ufw allow 8883/tcp      # MQTT/TLS
sudo ufw --force enable

sudo ufw status verbose
```

### 3. Implement Fail2Ban

```bash
sudo apt install -y fail2ban
sudo cp /etc/fail2ban/jail.conf /etc/fail2ban/jail.local
sudo systemctl enable fail2ban
sudo systemctl start fail2ban
```

## 📊 Service URLs

| Service | URL | Credentials | Purpose |
|---------|-----|-------------|---------|
| Dashboard | http://15.188.29.195:1880/dashboard/bumblebee | admin/bumblebee2025 | Main monitoring UI |
| Node-RED | http://15.188.29.195:1880 | admin/bumblebee2025 | Flow editor |
| OTA Page | http://15.188.29.195:1880/dashboard/ota | admin/bumblebee2025 | Firmware management |
| nginx OTA | http://15.188.29.195:8080/ota/firmware.bin | admin/bumblebee2025 | Firmware download |
| InfluxDB | http://15.188.29.195:8086 | admin/bumblebee2025 | Database UI |
| MQTT/TLS | mqtts://15.188.29.195:8883 | bumblebee/bumblebee2025 | ESP32 connection |

## 🐛 Troubleshooting

### Node-RED OTA Permission Error

If you see `EACCES: permission denied, mkdir '/data/ota/versions'`:

```bash
docker exec -u 0 bumblebee-nodered sh -c "mkdir -p /data/ota/versions && chown -R node-red:node-red /data/ota && chmod -R 755 /data/ota"
```

### nginx 403 Forbidden

```bash
# Check .htpasswd file exists
cat nginx/.htpasswd

# Recreate if needed
htpasswd -cb nginx/.htpasswd admin bumblebee2025
docker compose restart nginx
```

### ESP32 Can't Download Firmware

1. Check nginx is running: `docker compose ps`
2. Test from server: `curl -u admin:bumblebee2025 http://localhost:8080/ota/firmware.bin`
3. Check firewall: `sudo ufw status`
4. Check ESP32 has correct URL and credentials

### MQTT Connection Issues

```bash
# Test TLS connection
openssl s_client -connect 15.188.29.195:8883 -CAfile mosquitto/certs/ca.crt

# Monitor MQTT
docker compose logs -f mosquitto
```

## 🎉 Deployment Complete!

Your production Bumblebee system is now running with:
- ✅ Secure MQTT over TLS
- ✅ Real-time monitoring dashboard
- ✅ OTA firmware updates via nginx
- ✅ Multi-node parallel update support

---

**🐝 BUMBLEBEE - Production-Ready Wireless Power Transfer Monitoring**  
**Version 3.0 - With OTA Firmware Updates**