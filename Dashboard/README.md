# 🐝 BUMBLEBEE MONITORING DASHBOARD

## Complete Software Stack: Mosquitto → Telegraf → InfluxDB → Node-RED

[![Docker](https://img.shields.io/badge/Docker-Compose-blue)](https://docs.docker.com/compose/)
[![MQTT](https://img.shields.io/badge/MQTT-TLS--Secured-red)](https://mosquitto.org/)
[![Security](https://img.shields.io/badge/Security-TLS%201.2-green)](https://mosquitto.org/man/mosquitto-tls-7.html)

## 🔒 Security Features

- **MQTT over TLS/SSL** (Port 8883)
- **Username/Password Authentication**
- **Self-signed certificates** (Development) / Let's Encrypt (Production)
- **Firewall rules** for secure ports
- **Internal Docker network** isolation

## 📋 Quick Start

### Windows Local Setup

1. **Install Docker Desktop** from https://www.docker.com/products/docker-desktop
2. **Download files** to `C:\bumblebee-monitoring`
3. **Configure certificates** (see security setup below)
4. **Run** `start-bumblebee.bat` and choose option 1
5. **Access dashboard** at http://localhost:1880/ui

📘 Detailed guide: [SETUP-LOCAL-WINDOWS.md](SETUP-LOCAL-WINDOWS.md)

### AWS Lightsail Deployment

📘 Production deployment with full security: [AWS-DEPLOYMENT.md](AWS-DEPLOYMENT.md)

## 🔐 Security Configuration

### Generate Self-Signed Certificates (Development)

```bash
# Windows (using Git Bash or WSL)
mkdir -p mosquitto/certs
cd mosquitto/certs

# Generate CA certificate
openssl req -new -x509 -days 365 -extensions v3_ca -keyout ca.key -out ca.crt \
  -subj "//C=PT\ST=Braga\L=Barcelos\O=Bumblebee\OU=IoT\CN=Bumblebee-CA"

# Generate server certificate
openssl genrsa -out server.key 2048
openssl req -new -key server.key -out server.csr \
  -subj "//C=PT\ST=Braga\L=Barcelos\O=Bumblebee\OU=IoT\CN=localhost"
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -out server.crt -days 365

# Clean up
del server.csr
```

### Create Password File

```bash
# Start containers
docker-compose up -d

# Create users (you'll be prompted for passwords)
docker-compose exec mosquitto mosquitto_passwd -c /mosquitto/config/passwd bumblebee
docker-compose exec mosquitto mosquitto_passwd /mosquitto/config/passwd telegraf
docker-compose exec mosquitto mosquitto_passwd /mosquitto/config/passwd nodered

# Store passwords securely (NOT in repository!)
echo "bumblebee: YOUR_PASSWORD" > passwords.txt
echo "telegraf: YOUR_PASSWORD" >> passwords.txt
echo "nodered: YOUR_PASSWORD" >> passwords.txt
```

## 📋 Files Included

- `docker-compose.yml` - Service orchestration with TLS support
- `mosquitto/config/mosquitto.conf` - Secure MQTT broker config
- `mosquitto/config/passwd` - User credentials (generated)
- `mosquitto/certs/` - SSL/TLS certificates
- `telegraf/telegraf.conf` - Data processing with auth
- `bumblebee-flowfuse-dashboard.json` - Dashboard flow
- `start-bumblebee.bat` - Windows management script
- `SETUP-LOCAL-WINDOWS.md` - Detailed Windows setup
- `AWS-DEPLOYMENT.md` - AWS deployment with security
- `SECURE-MQTT-SETUP-GUIDE.md` - Complete security guide

## 📡 MQTT Configuration

### Ports

| Port | Protocol | Use | Security |
|------|----------|-----|----------|
| 1883 | MQTT | Internal Docker only | Password auth |
| 8883 | MQTTS | External secure access | TLS + Password |
| 9001 | WSS | WebSocket over TLS | TLS + Password |

### Topics

#### Published by ESP32
- `bumblebee/{unit_id}/dynamic` - Real-time sensor data
- `bumblebee/{unit_id}/alerts` - Alert conditions

#### Subscribed by ESP32
- `bumblebee/control` - Master ON/OFF control (0 or 1)

## 🖥️ Service URLs

### Local Development

| Service | URL | Credentials |
|---------|-----|-------------|
| Dashboard | http://localhost:1880/ui | None |
| Node-RED | http://localhost:1880 | None |
| InfluxDB | http://localhost:8086 | admin / bumblebee2025 |
| MQTT | localhost:1883 (internal) | User/Pass required |
| MQTTS | localhost:8883 (TLS) | User/Pass + Certificate |

### Production (AWS)

| Service | URL | Credentials |
|---------|-----|-------------|
| Dashboard | http://15.188.29.195:1880/dashboard/bumblebee | None |
| Node-RED | http://15.188.29.195:1880 | None |
| InfluxDB | http://15.188.29.195:8086 | admin / bumblebee2025 |
| MQTTS | mqtts://15.188.29.195:8883 | User/Pass + Certificate |

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
docker compose logs -f telegraf
```

### Test MQTT Connection
```bash
# Test with authentication (should work)
docker compose exec mosquitto mosquitto_pub \
  -h localhost -p 1883 \
  -u bumblebee -P YOUR_PASSWORD \
  -t test/topic -m "Hello Secure MQTT"

# Test without auth (should fail)
docker compose exec mosquitto mosquitto_pub \
  -h localhost -p 1883 \
  -t test/topic -m "This should fail"
```

### Test TLS Connection
```bash
# From outside container (requires mosquitto-clients)
mosquitto_pub \
  -h localhost -p 8883 \
  --cafile mosquitto/certs/ca.crt \
  -u bumblebee -P YOUR_PASSWORD \
  -t test/topic -m "Hello TLS"
```

## 🔒 Security Notes

### Development Environment
- **Passwords**: Visible in docker-compose.yml (acceptable for development)
- **Certificates**: Self-signed (browser warnings expected)
- **Firewall**: All ports open locally

### Production Environment
- **Passwords**: Use environment variables or Docker secrets
- **Certificates**: Use Let's Encrypt or commercial CA
- **Firewall**: Restrict to specific IP ranges
- **Certificate Renewal**: Implement auto-renewal script
- **Monitoring**: Enable security audit logs

### Password Management

**Development** (current):
```yaml
# In docker-compose.yml
environment:
  - MQTT_PASSWORD=bumblebee2025  # Visible but OK for dev
```

**Production** (recommended):
```yaml
# Use .env file (add to .gitignore!)
environment:
  - MQTT_PASSWORD=${MQTT_PASSWORD}
```

```bash
# .env file
MQTT_PASSWORD=your_secure_password_here
```

### Certificate Renewal

While the current implementation skips certificate verification for development (`skip_cert_common_name_check = true`), production deployments should:

1. Use proper domain names
2. Implement certificate renewal:
```bash
# For Let's Encrypt (production)
certbot renew --quiet
docker compose restart mosquitto
```

3. Set up auto-renewal cron job:
```bash
0 2 * * 1 /usr/bin/certbot renew --quiet && docker compose restart mosquitto
```

## 🐛 Troubleshooting

### Connection Refused
- Check if port 8883 is open in firewall
- Verify certificates are in correct location
- Check password file exists

### TLS Handshake Failed
```bash
# Test certificate
openssl s_client -connect localhost:8883 -CAfile mosquitto/certs/ca.crt

# Check certificate dates
openssl x509 -in mosquitto/certs/server.crt -noout -dates
```

### Authentication Failed
```bash
# Verify password file
docker compose exec mosquitto cat /mosquitto/config/passwd

# Reset password
docker compose exec mosquitto mosquitto_passwd /mosquitto/config/passwd bumblebee
```

### ESP32 Can't Connect
1. Ensure CA certificate is correctly embedded in firmware
2. Check ESP32 has enough heap memory for TLS (~40KB required)
3. Verify username/password in firmware matches
4. Check firewall allows ESP32 IP range

## 🎯 Next Steps

### Local Development
1. ✅ Generate certificates
2. ✅ Create password file
3. ✅ Start local stack
4. ✅ Configure ESP32 with certificates
5. ✅ Test secure connection

### Production Deployment
1. ✅ Deploy to AWS Lightsail
2. ✅ Configure proper domain (optional)
3. ✅ Set up Let's Encrypt (if using domain)
4. ✅ Restrict firewall rules
5. ✅ Implement monitoring
6. ✅ Schedule certificate renewal

## 📚 Additional Resources

- [Mosquitto TLS Configuration](https://mosquitto.org/man/mosquitto-tls-7.html)
- [Docker Secrets](https://docs.docker.com/engine/swarm/secrets/)
- [Let's Encrypt Docker](https://hub.docker.com/r/certbot/certbot/)
- [ESP32 TLS Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/mqtt.html)

---

**🐝 BUMBLEBEE - Secure Wireless Power Transfer Monitoring**  
**Version 2.0 - With TLS/SSL Security**