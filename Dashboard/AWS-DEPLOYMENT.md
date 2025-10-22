# 🐝 BUMBLEBEE MONITORING SYSTEM - AWS LIGHTSAIL DEPLOYMENT GUIDE

## 📋 Overview

This guide will help you deploy the Bumblebee monitoring stack on AWS Lightsail for production use. The setup includes:
- Mosquitto MQTT Broker (port 1883)
- InfluxDB Time Series Database (port 8086)
- Telegraf Data Bridge
- Node-RED Dashboard (port 1880)

## 💰 Cost Estimate

**Recommended Lightsail Plan:** $7/month (first 90 days free)
- 2 GB RAM
- 2 vCPU
- 60 GB SSD
- 2 TB Transfer

For 50+ devices, consider the $20/month plan (2 GB RAM, 1 vCPU).

## 🚀 Step-by-Step Deployment

### Step 1: Create Lightsail Instance

1. Go to AWS Lightsail Console: https://lightsail.aws.amazon.com/
2. Click **"Create instance"**
3. Select **Instance location**: Choose closest to your devices
4. Select **Platform**: Linux/Unix
5. Select **Blueprint**: OS Only → Ubuntu 22.04 LTS
6. **Optional**: Upload your SSH key or use Lightsail's default
7. Choose **Instance plan**: $10/month (1 GB RAM) or higher
8. Name your instance: `bumblebee-monitor`
9. Click **Create instance**
10. Wait 2-3 minutes for instance to start

### Step 2: Configure Firewall Rules

1. Click on your instance → **Networking tab**
2. Under **Firewall**, add these rules:

| Application | Protocol | Port Range |
|-------------|----------|------------|
| SSH | TCP | 22 |
| HTTP | TCP | 80 (optional) |
| Custom | TCP | 1883 (MQTT) |
| Custom | TCP | 1880 (Node-RED) |
| Custom | TCP | 8086 (InfluxDB) |

**Security Note**: For production, restrict access to known IP ranges instead of "Any IP address (0.0.0.0/0)"

### Step 3: Assign Static IP --> 15.188.29.195

1. In your instance page, go to **Networking tab**
2. Click **"Create static IP"**
3. Attach to your instance
4. Note the IP address (e.g., `54.123.45.67`)

### Step 4: Connect to Your Instance

#### Option A: Using Lightsail Browser SSH
1. Click **"Connect using SSH"** button on instance page

#### Option B: Using Your Own SSH Client
```bash
ssh -i /path/to/your/key.pem ubuntu@YOUR_STATIC_IP
```

### Step 5: Install Docker on Ubuntu

Once connected via SSH, run these commands:

```bash
# Update package list
sudo apt update

# Install prerequisites
sudo apt install -y ca-certificates curl gnupg lsb-release

# Add Docker's official GPG key
sudo mkdir -p /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg

# Set up Docker repository
echo \
  "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu \
  $(lsb_release -cs) stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

# Install Docker
sudo apt update
sudo apt install -y docker-ce docker-ce-cli containerd.io docker-compose-plugin

# Verify Docker installation
sudo docker --version
sudo docker compose version

# Add ubuntu user to docker group (no sudo needed)
sudo usermod -aG docker ubuntu

# Apply group changes (or logout/login)
newgrp docker

# Test Docker without sudo
docker run hello-world
```

### Step 6: Create Project Directory

```bash
# Create project directory
mkdir -p ~/bumblebee-monitoring
cd ~/bumblebee-monitoring

# Create subdirectories
mkdir -p mosquitto/config
mkdir -p telegraf
```

### Step 7: Create Configuration Files

#### Create docker-compose.yml

```bash
nano docker-compose.yml
```

Copy the contents of `docker-compose.yml` from your local setup, then save (Ctrl+X, Y, Enter).

#### Create Mosquitto Configuration

```bash
nano mosquitto/config/mosquitto.conf
```

Copy the contents of `mosquitto.conf` from your local setup, then save.

#### Create Telegraf Configuration

```bash
nano telegraf/telegraf.conf
```

Copy the contents of `telegraf.conf` from your local setup, then save.

### Step 8: Start the Stack

```bash
# Start all services
docker compose up -d

# Verify all services are running
docker compose ps

# Check logs
docker compose logs -f
```

You should see all four services running:
- bumblebee-mosquitto
- bumblebee-influxdb  
- bumblebee-telegraf
- bumblebee-nodered

### Step 9: Access Services

Your services are now accessible at:

| Service | URL | Credentials |
|---------|-----|-------------|
| **Node-RED Dashboard** | http://YOUR_STATIC_IP:1880 | None |
| **Dashboard UI** | http://YOUR_STATIC_IP:1880/ui | None |
| **InfluxDB** | http://YOUR_STATIC_IP:8086 | admin / bumblebee2024 |
| **MQTT Broker** | YOUR_STATIC_IP:1883 | None |

### Step 10: Import Node-RED Flow

1. Open Node-RED: `http://YOUR_STATIC_IP:1880`
2. Click hamburger menu → Import
3. Copy/paste the contents of `bumblebee-flowfuse-dashboard.json`
4. Click Import
5. Click Deploy
6. Access dashboard: `http://YOUR_STATIC_IP:1880/bumblebee/dashboard`

### Step 11: Configure ESP32 Devices

Update your ESP32 firmware MQTT configuration:

```c
#define MQTT_BROKER_HOST "YOUR_STATIC_IP"  // e.g., "54.123.45.67"
#define MQTT_BROKER_PORT 1883
```

Compile and upload to your ESP32 devices.

## 🔒 Security Hardening (Recommended for Production)

### 1. Add MQTT Authentication

Edit `mosquitto/config/mosquitto.conf`:

```bash
# Create password file
docker compose exec mosquitto mosquitto_passwd -c /mosquitto/config/passwd bumblebee

# Edit mosquitto.conf
nano mosquitto/config/mosquitto.conf
```

Add these lines:
```
allow_anonymous false
password_file /mosquitto/config/passwd
```

Restart Mosquitto:
```bash
docker compose restart mosquitto
```

### 2. Enable SSL/TLS for MQTT

Generate certificates:
```bash
# Install certbot
sudo apt install -y certbot

# Get SSL certificate (requires domain name)
sudo certbot certonly --standalone -d your-domain.com
```

Update `mosquitto.conf` for TLS.

### 3. Add Node-RED Authentication

```bash
# Access Node-RED container
docker compose exec nodered bash

# Generate password hash
npx node-red-admin hash-pw

# Exit container
exit
```

Create `settings.js` in Node-RED data directory with admin user.

### 4. Restrict Firewall Rules

In Lightsail Firewall, change rules to only allow:
- Your office IP for Node-RED and InfluxDB
- Your ESP32 device network for MQTT

### 5. Enable UFW Firewall

```bash
# Enable UFW
sudo ufw allow 22/tcp      # SSH
sudo ufw allow 1883/tcp    # MQTT
sudo ufw allow 1880/tcp    # Node-RED
sudo ufw allow 8086/tcp    # InfluxDB
sudo ufw enable
```

## 📊 Monitoring and Maintenance

### Check Service Status
```bash
docker compose ps
```

### View Logs
```bash
# All services
docker compose logs -f

# Specific service
docker compose logs -f nodered
docker compose logs -f mosquitto
docker compose logs -f telegraf
docker compose logs -f influxdb
```

### Restart Services
```bash
# Restart all
docker compose restart

# Restart specific service
docker compose restart nodered
```

### Update Services
```bash
# Pull latest images
docker compose pull

# Recreate containers with new images
docker compose down
docker compose up -d
```

## 💾 Backup Strategy

### Manual Backup

```bash
# Create backup directory
mkdir -p ~/backups

# Backup InfluxDB data
docker run --rm \
  -v bumblebee_influxdb_data:/data \
  -v ~/backups:/backup \
  alpine tar czf /backup/influxdb-$(date +%Y%m%d).tar.gz -C /data .

# Backup Node-RED flows
docker run --rm \
  -v bumblebee_nodered_data:/data \
  -v ~/backups:/backup \
  alpine tar czf /backup/nodered-$(date +%Y%m%d).tar.gz -C /data .
```

### Automated Backup with Cron

```bash
# Edit crontab
crontab -e

# Add daily backup at 2 AM
0 2 * * * docker run --rm -v bumblebee_influxdb_data:/data -v ~/backups:/backup alpine tar czf /backup/influxdb-$(date +\%Y\%m\%d).tar.gz -C /data .
```

### Download Backups to Local Machine

```bash
# From your local machine
scp -i /path/to/key.pem ubuntu@YOUR_STATIC_IP:~/backups/* ./local-backups/
```

## 🔄 Restore from Backup

```bash
# Stop services
docker compose down

# Remove old data
docker volume rm bumblebee_influxdb_data

# Restore from backup
docker run --rm \
  -v bumblebee_influxdb_data:/data \
  -v ~/backups:/backup \
  alpine sh -c "cd /data && tar xzf /backup/influxdb-YYYYMMDD.tar.gz"

# Restart services
docker compose up -d
```

## 📈 Scaling Considerations

### When to Upgrade

Upgrade your Lightsail plan if:
- CPU usage consistently > 70%
- RAM usage consistently > 80%
- InfluxDB queries become slow
- More than 50 active devices

### Monitoring Resources

```bash
# Check CPU and Memory usage
docker stats

# Check disk usage
df -h

# Check Docker volume sizes
docker system df -v
```

## 🐛 Troubleshooting

### Services Not Starting

```bash
# Check logs
docker compose logs

# Check disk space
df -h

# Restart Docker
sudo systemctl restart docker
```

### Cannot Connect from ESP32

1. Verify firewall rules in Lightsail
2. Test MQTT connection:
   ```bash
   docker compose exec mosquitto mosquitto_pub -t test -m "hello"
   ```
3. Check mosquitto logs:
   ```bash
   docker compose logs -f mosquitto
   ```

### High Memory Usage

1. Check InfluxDB retention policies
2. Restart services:
   ```bash
   docker compose restart
   ```
3. Consider upgrading Lightsail plan

## 🎯 Production Checklist

- [ ] Static IP assigned
- [ ] Firewall rules configured
- [ ] MQTT authentication enabled
- [ ] SSL/TLS certificates installed
- [ ] Node-RED admin password set
- [ ] Automated backups configured
- [ ] Monitoring alerts set up
- [ ] ESP32 devices configured with production IP
- [ ] Dashboard tested with real data
- [ ] Documentation updated with production URLs

## 📞 Support and Maintenance

### Regular Tasks

**Weekly:**
- Check service logs for errors
- Verify all devices are connected
- Review dashboard for anomalies

**Monthly:**
- Update Docker images
- Review and optimize InfluxDB data
- Check backup integrity
- Review firewall logs

**Quarterly:**
- Security patches for Ubuntu
- Review and update retention policies
- Capacity planning review

## 🔧 Useful Commands Reference

```bash
# Service Management
docker compose up -d          # Start all services
docker compose down           # Stop all services
docker compose restart        # Restart all services
docker compose ps             # Check status

# Logs
docker compose logs -f                    # All logs
docker compose logs -f nodered           # Specific service
docker compose logs --tail=100 mosquitto # Last 100 lines

# System Information
docker stats                  # Resource usage
df -h                        # Disk usage
free -h                      # Memory usage
top                          # Process monitor

# Cleanup
docker system prune -a       # Remove unused images
docker volume prune          # Remove unused volumes
```

---

## 🎉 Deployment Complete!

Your Bumblebee monitoring system is now running on AWS Lightsail!

**Dashboard URL**: `http://15.188.29.195:1880/bumblebee/dashboard`

**Next Steps:**
1. Configure your ESP32 devices to connect to the production MQTT broker
2. Monitor the dashboard for incoming data
3. Set up automated backups
4. Implement security hardening

---

**🐝 BUMBLEBEE - Wireless Power Transfer Monitoring System**
