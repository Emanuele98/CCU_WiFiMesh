# 🐝 BUMBLEBEE MONITORING SYSTEM - LOCAL SETUP GUIDE (WINDOWS)

## 📋 Prerequisites

1. **Docker Desktop for Windows**
   - Download: https://www.docker.com/products/docker-desktop
   - Minimum Requirements:
     - Windows 10 64-bit: Pro, Enterprise, or Education (Build 19041 or higher)
     - WSL 2 feature enabled
     - 4 GB RAM minimum (8 GB recommended)

2. **OpenSSL** (for certificate generation)
   - Option 1: Use Git Bash (includes OpenSSL)
   - Option 2: Download from https://slproweb.com/products/Win32OpenSSL.html
   - Option 3: Use WSL2

3. **Git for Windows** (optional, for version control)
   - Download: https://git-scm.com/download/win

## 📁 Project Structure

```
bumblebee-monitoring/
├── docker-compose.yml              # Main orchestration file
├── mosquitto/
│   ├── config/
│   │   ├── mosquitto.conf         # Secure MQTT config
│   │   └── passwd                 # User passwords (generated)
│   └── certs/                     # SSL/TLS certificates
│       ├── ca.crt                 # CA certificate
│       ├── ca.key                 # CA private key
│       ├── server.crt             # Server certificate
│       └── server.key             # Server private key
├── telegraf/
│   └── telegraf.conf              # Data bridge config with auth
├── bumblebee-nodered-flow.json   # Dashboard flow
├── start-bumblebee.bat            # Startup script
└── README.md                      # This file
```

## 🚀 Step-by-Step Installation

### Step 1: Install Docker Desktop

1. Download Docker Desktop from the link above
2. Run the installer
3. **Enable WSL 2** during installation (if prompted)
4. Restart your computer
5. Launch Docker Desktop
6. Wait for Docker to fully start (whale icon in system tray should be stable)
7. Verify installation:
   ```cmd
   docker --version
   docker-compose --version
   ```

### Step 2: Create Project Directory

1. Open Command Prompt or PowerShell as Administrator
2. Create project directory:
   ```cmd
   mkdir C:\bumblebee-monitoring
   cd C:\bumblebee-monitoring
   ```

### Step 3: Generate SSL/TLS Certificates

Open Git Bash (or WSL) and run:

```bash
# Navigate to project directory
cd /c/bumblebee-monitoring

# Create certificates directory
mkdir -p mosquitto/certs
cd mosquitto/certs

# Generate CA key and certificate
openssl req -new -x509 -days 365 -extensions v3_ca -keyout ca.key -out ca.crt \
  -subj "//C=PT\ST=Braga\L=Barcelos\O=Bumblebee\OU=IoT\CN=Bumblebee-CA"

# Generate server key
openssl genrsa -out server.key 2048

# Generate server certificate request
openssl req -new -key server.key -out server.csr \
  -subj "//C=PT\ST=Braga\L=Barcelos\O=Bumblebee\OU=IoT\CN=localhost"

# Sign the server certificate
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -out server.crt -days 365

# Set permissions (Windows compatible)
chmod 644 ca.crt server.crt
chmod 600 server.key ca.key

# Clean up
rm server.csr

# Return to main directory
cd ../..
```

### Step 4: Create Configuration Files

#### 4.1 Create docker-compose.yml
```yaml
version: '3.8'

services:
  # MOSQUITTO - Secure MQTT Broker
  mosquitto:
    image: eclipse-mosquitto:2.0
    container_name: bumblebee-mosquitto
    ports:
      - "1883:1883"      # Internal MQTT
      - "8883:8883"      # MQTT over TLS
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

  # INFLUXDB - Time Series Database
  influxdb:
    image: influxdb:2.7
    container_name: bumblebee-influxdb
    ports:
      - "8086:8086"
    volumes:
      - influxdb_data:/var/lib/influxdb2
    environment:
      - DOCKER_INFLUXDB_INIT_MODE=setup
      - DOCKER_INFLUXDB_INIT_USERNAME=admin
      - DOCKER_INFLUXDB_INIT_PASSWORD=bumblebee2025
      - DOCKER_INFLUXDB_INIT_ORG=bumblebee
      - DOCKER_INFLUXDB_INIT_BUCKET=sensor_data
      - DOCKER_INFLUXDB_INIT_ADMIN_TOKEN=bumblebee-super-secret-token
    networks:
      bumblebee-network:
        ipv4_address: 172.20.0.3
    restart: unless-stopped

  # TELEGRAF - Data Bridge with Authentication
  telegraf:
    image: telegraf:1.28
    container_name: bumblebee-telegraf
    volumes:
      - ./telegraf/telegraf.conf:/etc/telegraf/telegraf.conf:ro
    environment:
      - MQTT_USERNAME=telegraf
      - MQTT_PASSWORD=bumblebee2025
    depends_on:
      - mosquitto
      - influxdb
    networks:
      bumblebee-network:
        ipv4_address: 172.20.0.4
    restart: unless-stopped

  # NODE-RED - Dashboard
  nodered:
    image: nodered/node-red:latest
    container_name: bumblebee-nodered
    ports:
      - "1880:1880"
    volumes:
      - nodered_data:/data
    environment:
      - TZ=Europe/Lisbon
      - NODE_RED_ENABLE_PROJECTS=false
    depends_on:
      - mosquitto
      - influxdb
    networks:
      bumblebee-network:
        ipv4_address: 172.20.0.5
    restart: unless-stopped

volumes:
  mosquitto_data:
  mosquitto_log:
  influxdb_data:
  nodered_data:

networks:
  bumblebee-network:
    driver: bridge
    ipam:
      config:
        - subnet: 172.20.0.0/24
```

#### 4.2 Create Mosquitto Configuration

Create `mosquitto/config/mosquitto.conf`:

```conf
# BUMBLEBEE - Secure MQTT Configuration

# Default Settings
per_listener_settings true
allow_anonymous false

# Secure MQTT Listener (Port 8883)
listener 8883 0.0.0.0
protocol mqtt
certfile /mosquitto/certs/server.crt
keyfile /mosquitto/certs/server.key
cafile /mosquitto/certs/ca.crt
require_certificate false
password_file /mosquitto/config/passwd
tls_version tlsv1.2

# Local MQTT Listener (Port 1883 - Internal only)
listener 1883 172.20.0.2
protocol mqtt
allow_anonymous false
password_file /mosquitto/config/passwd

# WebSocket over TLS (Port 9001)
listener 9001 0.0.0.0
protocol websockets
certfile /mosquitto/certs/server.crt
keyfile /mosquitto/certs/server.key
cafile /mosquitto/certs/ca.crt
require_certificate false
password_file /mosquitto/config/passwd

# Persistence
persistence true
persistence_location /mosquitto/data/
autosave_interval 300

# Logging
log_dest file /mosquitto/log/mosquitto.log
log_dest stdout
log_type all
log_timestamp true
log_timestamp_format %Y-%m-%dT%H:%M:%S

# Connection Settings
max_connections -1
message_size_limit 10240
```

#### 4.3 Create Telegraf Configuration

Create `telegraf/telegraf.conf` with authentication:

```toml
[agent]
  interval = "10s"
  round_interval = true
  metric_batch_size = 1000
  metric_buffer_limit = 10000
  collection_jitter = "0s"
  flush_interval = "10s"
  flush_jitter = "0s"
  precision = ""
  hostname = ""
  omit_hostname = false

# MQTT Consumer with Authentication
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

# InfluxDB v2 Output
[[outputs.influxdb_v2]]
  urls = ["http://172.20.0.3:8086"]
  token = "bumblebee-super-secret-token"
  organization = "bumblebee"
  bucket = "sensor_data"
```

### Step 5: Create User Passwords

```cmd
# Start the mosquitto container first
docker-compose up -d mosquitto

# Wait a few seconds for it to start
timeout /t 5

# Create users (you'll be prompted for passwords)
docker-compose exec mosquitto mosquitto_passwd -c /mosquitto/config/passwd bumblebee
docker-compose exec mosquitto mosquitto_passwd /mosquitto/config/passwd telegraf
docker-compose exec mosquitto mosquitto_passwd /mosquitto/config/passwd nodered

# Suggested passwords (CHANGE IN PRODUCTION!):
# bumblebee: bumblebee2025
# telegraf: bumblebee2025
# nodered: bumblebee2025
```

### Step 6: Start the Complete Stack

```cmd
# Start all services
docker-compose up -d

# Verify all services are running
docker-compose ps

# Check logs
docker-compose logs -f
```

### Step 7: Import Node-RED Flow

1. Open Node-RED at http://localhost:1880
2. Click the **hamburger menu** (top right) → **Import**
3. Select **"select a file to import"**
4. Choose `bumblebee-nodered-flow.json`
5. Click **Import**
6. **Configure MQTT nodes** with authentication:
   - Double-click any MQTT node
   - Edit Server:
     - Server: `172.20.0.2`
     - Port: `1883`
     - Security tab:
       - Username: `nodered`
       - Password: `bumblebee2025`
   - Click **Update**
7. Click **Deploy**
8. Access dashboard at http://localhost:1880/ui

### Step 8: Configure InfluxDB

1. Open http://localhost:8086
2. Login with: admin / bumblebee2025
3. Verify the `sensor_data` bucket exists
4. Check incoming data in Data Explorer

### Step 9: Configure ESP32 Firmware

Update your ESP32 code with local settings:

```c
// For local testing (using internal network)
#define MQTT_BROKER_HOST "192.168.1.100"  // Your PC's IP
#define MQTT_BROKER_PORT 1883              // Internal port
#define MQTT_USERNAME "bumblebee"
#define MQTT_PASSWORD "bumblebee2025"

// For secure connection (requires certificate in ESP32)
// #define MQTT_BROKER_PORT 8883
// Add CA certificate to firmware
```

### Step 10: Test the Setup

#### Test MQTT Authentication
```cmd
# Test with password (should work)
docker-compose exec mosquitto mosquitto_pub -h localhost -p 1883 -u bumblebee -P bumblebee2025 -t test -m "Hello"

# Test without password (should fail)
docker-compose exec mosquitto mosquitto_pub -h localhost -p 1883 -t test -m "This should fail"
```

#### Test TLS Connection
```cmd
# From Git Bash or WSL
mosquitto_pub -h localhost -p 8883 --cafile mosquitto/certs/ca.crt -u bumblebee -P bumblebee2025 -t test -m "TLS Test"
```

## 🔧 Troubleshooting

### Docker Desktop Issues
- Ensure WSL 2 is enabled
- Restart Docker Desktop
- Run as Administrator

### Certificate Issues
```bash
# Verify certificate
openssl x509 -in mosquitto/certs/server.crt -text -noout

# Check certificate dates
openssl x509 -in mosquitto/certs/server.crt -noout -dates
```

### Connection Issues
```cmd
# Check if services are running
docker-compose ps

# Check mosquitto logs
docker-compose logs mosquitto

# Check if ports are listening
netstat -an | findstr "1883"
netstat -an | findstr "8883"
```

### Node-RED Connection Issues
1. Check MQTT connection:
   - Node-RED → hamburger menu → Configuration nodes → mqtt-broker
   - Status should be "Connected"
2. Check logs: `docker-compose logs -f nodered`
3. Verify ESP32 is publishing to correct topics

## 📝 Useful Commands

```cmd
# Start all services
docker-compose up -d

# Stop all services
docker-compose down

# View logs
docker-compose logs -f

# View specific service logs
docker-compose logs -f mosquitto

# Restart a service
docker-compose restart mosquitto

# Remove all data and start fresh
docker-compose down -v

# Check service status
docker-compose ps

# Enter mosquitto container
docker-compose exec mosquitto sh
```

## 🔐 Security Notes for Local Development

### Current Setup (Development)
- Self-signed certificates (browser warnings expected)
- Passwords visible in configuration files
- All ports accessible locally

### Moving to Production
1. Use environment variables for passwords
2. Get proper SSL certificates
3. Restrict network access
4. Enable firewall rules
5. Use Docker secrets for sensitive data

### Windows Firewall Configuration
If ESP32 devices can't connect from the network:
1. Open Windows Defender Firewall
2. Click "Allow an app"
3. Add Docker Desktop
4. Allow both Private and Public networks

## 💾 Data Persistence

All data is persisted in Docker volumes:
- **Mosquitto**: Message queue and logs
- **InfluxDB**: Time-series data
- **Node-RED**: Flows and settings

To backup:
```cmd
# Create backup directory
mkdir C:\bumblebee-backup

# Backup InfluxDB data
docker run --rm -v bumblebee-monitoring_influxdb_data:/data -v C:\bumblebee-backup:/backup alpine tar czf /backup/influxdb-backup.tar.gz -C /data .
```

## 🎯 Next Steps

1. ✅ Verify all services are running
2. ✅ Test MQTT with authentication
3. ✅ Test TLS connection
4. ✅ Configure your ESP32 devices
5. ✅ Monitor the dashboard
6. ✅ Check InfluxDB for data
7. 📱 Proceed to AWS deployment for production

---

**🐝 BUMBLEBEE - Secure Local Development Environment**  
**With TLS/SSL and Authentication**
