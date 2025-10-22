# 🐝 BUMBLEBEE MONITORING SYSTEM - LOCAL SETUP GUIDE (WINDOWS)

## 📋 Prerequisites

1. **Docker Desktop for Windows**
   - Download: https://www.docker.com/products/docker-desktop
   - Minimum Requirements:
     - Windows 10 64-bit: Pro, Enterprise, or Education (Build 19041 or higher)
     - WSL 2 feature enabled
     - 4 GB RAM minimum (8 GB recommended)

2. **Git for Windows** (optional, for version control)
   - Download: https://git-scm.com/download/win

## 📁 Project Structure

```
bumblebee-monitoring/
├── docker-compose.yml              # Main orchestration file
├── mosquitto/
│   └── config/
│       └── mosquitto.conf         # MQTT broker config
├── telegraf/
│   └── telegraf.conf              # Data bridge config
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

1. Open Command Prompt or PowerShell
2. Create project directory:
   ```cmd
   mkdir C:\bumblebee-monitoring
   cd C:\bumblebee-monitoring
   ```

### Step 3: Create Configuration Files

#### 3.1 Create docker-compose.yml
Copy the `docker-compose.yml` file from this repository to your project directory.

#### 3.2 Create Mosquitto Configuration
```cmd
mkdir mosquitto\config
```
Copy the `mosquitto/config/mosquitto.conf` file to this location.

#### 3.3 Create Telegraf Configuration
```cmd
mkdir telegraf
```
Copy the `telegraf/telegraf.conf` file to this location.

#### 3.4 Copy Node-RED Flow
Copy the `bumblebee-nodered-flow.json` file to your project directory.

### Step 4: Start the Stack

#### Option A: Using the Batch Script (Recommended)
1. Copy `start-bumblebee.bat` to your project directory
2. Double-click `start-bumblebee.bat`
3. Choose option 1 to start the stack

#### Option B: Manual Docker Compose
```cmd
docker-compose up -d
```

### Step 5: Verify Services Are Running

```cmd
docker-compose ps
```

You should see all services running:
- bumblebee-mosquitto
- bumblebee-influxdb
- bumblebee-telegraf
- bumblebee-nodered

### Step 6: Access the Services

| Service | URL | Credentials |
|---------|-----|-------------|
| **Node-RED Dashboard** | http://localhost:1880 | None |
| **InfluxDB UI** | http://localhost:8086 | admin / bumblebee2024 |
| **MQTT Broker** | localhost:1883 | None |

### Step 7: Import Node-RED Flow

1. Open Node-RED at http://localhost:1880
2. Click the **hamburger menu** (top right) → **Import**
3. Select **"select a file to import"**
4. Choose `bumblebee-nodered-flow.json`
5. Click **Import**
6. Click **Deploy** (top right)
7. Open the **Dashboard UI**: http://localhost:1880/ui

### Step 8: Add Bumblebee Logo (Optional)

To add your custom Bumblebee logo:

1. In Node-RED, find the "Bumblebee Dashboard UI" template node
2. Double-click to edit
3. Find this line in the HTML:
   ```html
   <div class="logo-placeholder">🐝</div>
   ```
4. Replace with your logo:
   ```html
   <img src="data:image/png;base64,YOUR_BASE64_IMAGE_HERE" alt="Bumblebee Logo" style="width: 60px; height: 60px;">
   ```
   OR use an external URL:
   ```html
   <img src="/path/to/your/logo.png" alt="Bumblebee Logo" style="width: 60px; height: 60px;">
   ```
5. Click **Done** and **Deploy**

## 🔧 Configuration for Your ESP32

Update your ESP32 firmware MQTT configuration:

```c
#define MQTT_BROKER_HOST "YOUR_WINDOWS_IP"  // e.g., "192.168.1.100"
#define MQTT_BROKER_PORT 1883
```

To find your Windows IP:
```cmd
ipconfig
```
Look for "IPv4 Address" under your active network adapter.

## 📊 Testing the Dashboard

### Test with MQTT Explorer (Optional)

1. Download MQTT Explorer: http://mqtt-explorer.com/
2. Connect to `localhost:1883`
3. Publish test messages to:
   - `bumblebee/1/dynamic`
   - `bumblebee/1/alerts`

### Example Test Message (Dynamic Data)

Topic: `bumblebee/1/dynamic`
Payload:
```json
{
  "tx": {
    "voltage": 48.5,
    "current": 2.3,
    "temp": 36.8,
    "status": "TX_DEPLOY"
  },
  "rx": {
    "id": 5,
    "mac": "AA:BB:CC:DD:EE:FF",
    "voltage": 45.2,
    "current": 2.1,
    "temp": 33.1,
    "status": "RX_CHARGING"
  },
  "timestamp": 1234567890
}
```

### Example Test Message (Alerts)

Topic: `bumblebee/1/alerts`
Payload:
```json
{
  "tx": {
    "overtemperature": false,
    "overcurrent": false,
    "overvoltage": false,
    "fod": false
  },
  "rx": {
    "overtemperature": false,
    "overcurrent": false,
    "overvoltage": false,
    "fully_charged": false
  }
}
```

## 🛠️ Troubleshooting

### Docker Not Starting
- Ensure Hyper-V and WSL 2 are enabled
- Restart Docker Desktop
- Check Windows Firewall settings

### Services Not Accessible
- Verify all containers are running: `docker-compose ps`
- Check logs: `docker-compose logs -f [service_name]`
- Ensure ports are not blocked by firewall

### Node-RED Dashboard Not Showing Data
1. Check MQTT connection: 
   - Node-RED → hamburger menu → Configuration nodes → mqtt-broker
   - Status should be "Connected"
2. Check logs: `docker-compose logs -f nodered`
3. Verify ESP32 is publishing to correct topics

### InfluxDB Connection Issues
- Wait 30 seconds after first startup for initialization
- Check credentials: admin / bumblebee2024
- Verify token in telegraf.conf matches InfluxDB token

## 📝 Useful Commands

```cmd
# Start all services
docker-compose up -d

# Stop all services
docker-compose down

# View logs
docker-compose logs -f

# View logs for specific service
docker-compose logs -f nodered

# Restart a service
docker-compose restart nodered

# Remove all data and start fresh
docker-compose down -v

# Check service status
docker-compose ps
```

## 🔄 Updating the Stack

```cmd
# Pull latest images
docker-compose pull

# Restart with new images
docker-compose down
docker-compose up -d
```

## 💾 Data Persistence

All data is persisted in Docker volumes:
- **Mosquitto**: Message persistence
- **InfluxDB**: Time-series data
- **Node-RED**: Flows and settings

To backup data:
```cmd
docker run --rm -v bumblebee_influxdb_data:/data -v C:\backup:/backup alpine tar czf /backup/influxdb-backup.tar.gz -C /data .
```

## 🎯 Next Steps

1. ✅ Verify all services are running
2. ✅ Configure your ESP32 devices to connect to your Windows IP
3. ✅ Monitor the dashboard at http://localhost:1880/ui
4. ✅ Check InfluxDB for historical data
5. 📱 Proceed to AWS Lightsail deployment (see AWS-DEPLOYMENT.md)

## 📞 Support

For issues or questions, check:
- Docker logs: `docker-compose logs`
- Node-RED logs: http://localhost:1880
- InfluxDB UI: http://localhost:8086

---

**🐝 BUMBLEBEE - Wireless Power Transfer Monitoring System**
