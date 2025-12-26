# 🚀 AWS LIGHTSAIL DEPLOYMENT GUIDE - SECURE PRODUCTION SETUP

## Complete deployment guide for Bumblebee monitoring system on AWS Lightsail with TLS/SSL security

## 📋 Prerequisites

- AWS Account with billing enabled
- Basic Linux/SSH knowledge
- Domain name (optional, for proper SSL certificates)
- Local development environment tested and working

## 🏗️ Infrastructure Overview

```
┌────────────────────────────────────────┐
│         AWS Lightsail Instance         │
│                                        │
│  ┌──────────────────────────────────┐  │
│  │     Docker Compose Stack         │  │
│  │                                  │  │
│  │  ├─ Mosquitto (8883/TLS)         │  │
│  │  ├─ InfluxDB (8086)              │  │
│  │  ├─ Telegraf                     │  │
│  │  └─ Node-RED (1880)              │  │
│  └──────────────────────────────────┘  │
│                                        │
│         Firewall Rules:                │
│         - 22 (SSH)                     │
│         - 1880 (Node-RED)              │
│         - 8086 (InfluxDB)              │
│         - 8883 (MQTT/TLS)              │
└────────────────────────────────────────┘
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

| Application | Protocol | Port | Restrict to |
|-------------|----------|------|-------------|
| SSH | TCP | 22 | Your IP (recommended) |
| Custom | TCP | 1880 | Specific IPs or Any |
| Custom | TCP | 8086 | Specific IPs or Any |
| Custom | TCP | 8883 | Any (for ESP32s) |

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
mkdir -p ~/bumblebee-monitoring
cd ~/bumblebee-monitoring
mkdir -p mosquitto/config mosquitto/certs telegraf
```

### Step 7: Generate SSL/TLS Certificates

#### Option A: Self-Signed Certificates (Testing)

```bash
cd ~/bumblebee-monitoring/mosquitto/certs

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

cd ~/bumblebee-monitoring
```

#### Option B: Let's Encrypt (Production with Domain)

```bash
# Install certbot
sudo apt install -y certbot

# Stop any services using port 80
docker compose down

# Get certificate (replace with your domain)
sudo certbot certonly --standalone -d your-domain.com

# Copy certificates
sudo cp /etc/letsencrypt/live/your-domain.com/fullchain.pem mosquitto/certs/server.crt
sudo cp /etc/letsencrypt/live/your-domain.com/privkey.pem mosquitto/certs/server.key
sudo cp /etc/letsencrypt/live/your-domain.com/chain.pem mosquitto/certs/ca.crt

# Fix ownership
sudo chown -R ubuntu:ubuntu mosquitto/certs/
chmod 644 mosquitto/certs/*.crt
chmod 600 mosquitto/certs/*.key
```

### Step 8: Create Configuration Files

#### 8.1 Create docker-compose.yml

```bash
cat > docker-compose.yml << 'EOF'
version: '3.8'

services:
  # MOSQUITTO - Secure MQTT Broker
  mosquitto:
    image: eclipse-mosquitto:2.0
    container_name: bumblebee-mosquitto
    ports:
      - "1883:1883"      # Internal only (optional)
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
    healthcheck:
      test: ["CMD-SHELL", "timeout 5 mosquitto_sub -t '$$SYS/#' -C 1 -u telegraf -P bumblebee2025 || exit 1"]
      interval: 30s
      timeout: 10s
      retries: 3

  # INFLUXDB - Time Series Database
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
      - DOCKER_INFLUXDB_INIT_RETENTION=30d
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
      mosquitto:
        condition: service_healthy
      influxdb:
        condition: service_started
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
  influxdb_config:
  nodered_data:

networks:
  bumblebee-network:
    driver: bridge
    ipam:
      config:
        - subnet: 172.20.0.0/24
EOF
```

#### 8.2 Create Mosquitto Configuration

```bash
cat > mosquitto/config/mosquitto.conf << 'EOF'
# BUMBLEBEE - Production MQTT Configuration

# Security Settings
per_listener_settings true
allow_anonymous false

# Secure External Listener (Port 8883)
listener 8883 0.0.0.0
protocol mqtt
certfile /mosquitto/certs/server.crt
keyfile /mosquitto/certs/server.key
cafile /mosquitto/certs/ca.crt
require_certificate false
use_identity_as_username false
password_file /mosquitto/config/passwd
tls_version tlsv1.2

# Internal Listener (Port 1883 - Docker network only)
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
  precision = ""
  hostname = "bumblebee-telegraf"

# Secure MQTT Consumer
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
EOF
```

### Step 9: Create User Accounts

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

# Verify passwords were created
docker compose exec mosquitto cat /mosquitto/config/passwd
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

### Step 11: Import Node-RED Dashboard

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
6. Access dashboard: http://15.188.29.195:1880/bumblebee

### Step 12: Configure ESP32 Devices

Update ESP32 firmware with production settings:

```c
// main/include/mqtt_client_manager.h
#define MQTT_BROKER_HOST "15.188.29.195"
#define MQTT_BROKER_PORT 8883  // Secure port
#define MQTT_USERNAME "bumblebee"
#define MQTT_PASSWORD "bumblebee2025"

// In mqtt_client_manager.c, add the CA certificate:
static const char *mqtt_ca_cert = \
"-----BEGIN CERTIFICATE-----\n" \
// ... (paste content of ca.crt) ...
"-----END CERTIFICATE-----\n";
```

### Step 13: Test Secure Connections

#### From Server
```bash
# Test internal connection
docker compose exec mosquitto mosquitto_pub \
  -h localhost -p 1883 \
  -u bumblebee -P bumblebee2025 \
  -t test/topic -m "Internal test"

# Test TLS from outside container
mosquitto_pub \
  -h localhost -p 8883 \
  --cafile mosquitto/certs/ca.crt \
  -u bumblebee -P bumblebee2025 \
  -t test/topic -m "TLS test"
```

#### From Remote Machine
```bash
# Download ca.crt to your local machine, then:
mosquitto_pub \
  -h 15.188.29.195 -p 8883 \
  --cafile ca.crt \
  -u bumblebee -P bumblebee2025 \
  -t test/topic -m "Remote TLS test"
```

## 🔒 Security Hardening

### 1. Restrict SSH Access

```bash
# Edit SSH config
sudo nano /etc/ssh/sshd_config

# Add these lines:
PermitRootLogin no
PasswordAuthentication no
PubkeyAuthentication yes
AllowUsers ubuntu

# Restart SSH
sudo systemctl restart sshd
```

### 2. Enable UFW Firewall

```bash
# Configure UFW
sudo ufw default deny incoming
sudo ufw default allow outgoing
sudo ufw allow 22/tcp        # SSH
sudo ufw allow 1880/tcp      # Node-RED
sudo ufw allow 8086/tcp      # InfluxDB
sudo ufw allow 8883/tcp      # MQTT/TLS
sudo ufw --force enable

# Check status
sudo ufw status verbose
```

### 3. Implement Fail2Ban

```bash
# Install fail2ban
sudo apt install -y fail2ban

# Create local config
sudo cp /etc/fail2ban/jail.conf /etc/fail2ban/jail.local

# Start service
sudo systemctl enable fail2ban
sudo systemctl start fail2ban
```

### 4. Setup Automatic Updates

```bash
# Install unattended-upgrades
sudo apt install -y unattended-upgrades

# Enable automatic security updates
sudo dpkg-reconfigure --priority=low unattended-upgrades
```

## 📊 Monitoring & Maintenance

### Health Checks

```bash
# Create health check script
cat > ~/health-check.sh << 'EOF'
#!/bin/bash
echo "=== Bumblebee System Health Check ==="
echo "Date: $(date)"
echo ""
echo "=== Docker Services ==="
docker compose ps
echo ""
echo "=== Resource Usage ==="
docker stats --no-stream
echo ""
echo "=== Mosquitto Connections ==="
docker compose exec mosquitto mosquitto_sub -t '$SYS/broker/clients/connected' -C 1 -u telegraf -P bumblebee2025
echo ""
echo "=== Disk Usage ==="
df -h
echo ""
echo "=== Memory Usage ==="
free -h
EOF

chmod +x ~/health-check.sh

# Run health check
./health-check.sh
```

### Log Management

```bash
# View logs
docker compose logs -f mosquitto  # MQTT logs
docker compose logs -f telegraf   # Data bridge logs
docker compose logs -f influxdb   # Database logs
docker compose logs -f nodered    # Dashboard logs

# Setup log rotation
cat > ~/docker-log-rotate.sh << 'EOF'
#!/bin/bash
docker compose logs --no-color > logs/bumblebee-$(date +%Y%m%d).log
find logs/ -name "*.log" -mtime +30 -delete
EOF

chmod +x ~/docker-log-rotate.sh

# Add to crontab
(crontab -l 2>/dev/null; echo "0 0 * * * /home/ubuntu/docker-log-rotate.sh") | crontab -
```

## 💾 Backup Strategy

### Automated Backup Script

```bash
cat > ~/backup-bumblebee.sh << 'EOF'
#!/bin/bash
BACKUP_DIR=~/backups/$(date +%Y%m%d)
mkdir -p $BACKUP_DIR

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

# Backup Mosquitto data
docker run --rm \
  -v bumblebee-monitoring_mosquitto_data:/data \
  -v $BACKUP_DIR:/backup \
  alpine tar czf /backup/mosquitto.tar.gz -C /data .

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

### Certificate Renewal (Let's Encrypt)

```bash
# Create renewal script
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

# Schedule weekly renewal check
(crontab -l 2>/dev/null; echo "0 3 * * 1 /home/ubuntu/renew-certs.sh") | crontab -
```

## 🐛 Troubleshooting

### Connection Issues

```bash
# Test MQTT connectivity
openssl s_client -connect 15.188.29.195:8883 -CAfile mosquitto/certs/ca.crt

# Check certificate validity
openssl x509 -in mosquitto/certs/server.crt -noout -dates

# Monitor MQTT connections
docker compose exec mosquitto mosquitto_sub -t '#' -v -u bumblebee -P bumblebee2025

# Check firewall
sudo ufw status
sudo iptables -L -n
```

### Performance Issues

```bash
# Check resource usage
htop
docker stats

# Check disk I/O
iotop

# Optimize Docker
docker system prune -a
docker volume prune
```

### ESP32 Connection Problems

1. Verify CA certificate is correctly embedded
2. Check heap memory (TLS needs ~40KB)
3. Ensure correct username/password
4. Monitor MQTT logs: `docker compose logs -f mosquitto`
5. Check ESP32 serial output for TLS errors

## 📈 Scaling Considerations

### When to Upgrade

Monitor these metrics:
- CPU usage > 70% sustained
- Memory usage > 80%
- Disk I/O wait > 20%
- Network bandwidth saturation
- More than 100 connected devices

### Upgrade Options

1. **Vertical Scaling**: Upgrade Lightsail instance
   - $20/month: 4GB RAM, 80GB SSD
   - $40/month: 8GB RAM, 160GB SSD

2. **Horizontal Scaling**: Multiple instances
   - Separate MQTT broker
   - Dedicated InfluxDB server
   - Load balancer for Node-RED

3. **AWS Services Integration**:
   - AWS IoT Core for MQTT
   - AWS Timestream for time-series data
   - AWS ECS/EKS for container orchestration

## 🎯 Production Checklist

### Before Going Live

- [ ] **Security**
  - [ ] Changed all default passwords
  - [ ] Configured firewall rules
  - [ ] Enabled SSL/TLS on all services
  - [ ] Restricted SSH access
  - [ ] Set up fail2ban

- [ ] **Monitoring**
  - [ ] Health check script scheduled
  - [ ] Log rotation configured
  - [ ] Resource monitoring enabled
  - [ ] Alerts configured

- [ ] **Backup**
  - [ ] Automated backups scheduled
  - [ ] Backup retention policy set
  - [ ] Restore procedure tested

- [ ] **Documentation**
  - [ ] Password vault updated
  - [ ] Network diagram current
  - [ ] Runbook created
  - [ ] Contact list maintained

- [ ] **Testing**
  - [ ] Load testing completed
  - [ ] Failover tested
  - [ ] ESP32 connections verified
  - [ ] Dashboard functionality confirmed

## 📞 Support & Maintenance

### Regular Maintenance Tasks

**Daily:**
- Check service health
- Monitor error logs
- Verify data ingestion

**Weekly:**
- Review resource usage
- Check certificate expiration
- Test backups
- Update documentation

**Monthly:**
- Security updates
- Performance optimization
- Capacity planning
- Cost review

### Emergency Procedures

```bash
# Quick service restart
cd ~/bumblebee-monitoring
docker compose restart

# Full system restart
docker compose down
docker compose up -d

# Emergency backup
./backup-bumblebee.sh

# View all logs
docker compose logs --tail=100

# Roll back to previous version
docker compose down
git checkout <previous-commit>
docker compose up -d
```

## 🎉 Deployment Complete!

Your production Bumblebee monitoring system is now running securely on AWS Lightsail!

**Access Points:**
- Dashboard: http://15.188.29.195:1880/dashboard/bumblebee
- Node-RED: http://15.188.29.195:1880
- InfluxDB: http://15.188.29.195:8086
- Secure MQTT: mqtts://15.188.29.195:8883

**Next Steps:**
1. Configure all ESP32 devices with production settings
2. Verify data flow from devices to dashboard
3. Set up monitoring alerts
4. Document any customizations
5. Train operators on system usage

---

**🐝 BUMBLEBEE - Production-Ready Wireless Power Transfer Monitoring**  
**Version 2.0 - Secured with TLS/SSL**