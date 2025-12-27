# Deploying IAQ Monitoring on Windows

Complete guide for deploying the Indoor Air Quality monitoring stack on Windows using Docker Desktop.

## Prerequisites

- **Windows**: Windows 10 version 2004+ (Build 19041+) or Windows 11
- **Hardware**: 64-bit CPU with virtualization support (Intel VT-x/AMD-V), 8GB+ RAM recommended
- **Virtualization**: Enabled in BIOS/UEFI settings
- **Git**: For cloning the repository

## Architecture Overview

The stack consists of 4 services:

| Service | Port | Description |
|---------|------|-------------|
| Mosquitto | 1883 | MQTT broker for sensor data |
| TimescaleDB | 5432 | PostgreSQL with time-series extensions |
| Ingest | - | Python service: MQTT → Database |
| Grafana | 3000 | Dashboards and visualization |

## Step 1: Install Docker Desktop

### 1.1 Enable WSL2

Open PowerShell as Administrator:

```powershell
wsl --install
```

Restart your computer when prompted. After restart:

```powershell
wsl --set-default-version 2
```

### 1.2 Install Docker Desktop

1. Download from https://www.docker.com/products/docker-desktop/
2. Run the installer
3. Ensure "Use WSL 2 instead of Hyper-V" is checked
4. Restart your computer

### 1.3 Verify Installation

```powershell
docker --version
docker compose version
docker run hello-world
```

## Step 2: Clone the Repository

```powershell
git clone https://github.com/YOUR_USERNAME/env_monitoring.git
cd env_monitoring
```

## Step 3: Start the Services

Navigate to the infrastructure directory and start all services:

```powershell
cd infra
docker compose up -d
```

First run will:
- Pull images (~500MB total)
- Build the ingest service
- Initialize the TimescaleDB database schema
- Provision Grafana dashboards

Monitor startup progress:

```powershell
docker compose logs -f
```

Press `Ctrl+C` to exit logs.

## Step 4: Verify Deployment

### 4.1 Check Container Status

```powershell
docker compose ps
```

All 4 services should show `running`:

```
NAME        STATUS
grafana     running
ingest      running
mosquitto   running
postgres    running
```

### 4.2 Test MQTT Broker

Open two terminals. In the first, subscribe to telemetry:

```powershell
docker exec mosquitto mosquitto_sub -t "iaq/+/telemetry" -v
```

In the second, publish a test message:

```powershell
docker exec mosquitto mosquitto_pub -t "iaq/test/telemetry" -m "{\"device_id\":\"test\",\"co2_ppm\":450}"
```

You should see the message appear in the first terminal.

### 4.3 Verify Database

Connect to the database and check the schema:

```powershell
docker exec -it postgres psql -U iaq -d iaq -c "\dt"
```

Expected output:

```
              List of relations
 Schema |     Name     | Type  | Owner
--------+--------------+-------+-------
 public | iaq_readings | table | iaq
```

### 4.4 Access Grafana

1. Open http://localhost:3000 in your browser
2. Login with `admin` / `admin`
3. Change password when prompted (or skip)
4. Navigate to Dashboards → Browse
5. You should see: General Overview, PM Deep Dive, Data Quality

## Step 5: Configure the Sensor

Update the firmware to point to your Windows machine:

1. Find your IP address:
   ```powershell
   ipconfig
   ```
   Look for "IPv4 Address" under your active network adapter (e.g., `192.168.1.50`)

2. Edit `firmware/nano-esp32-iaq/secrets.h`:
   ```cpp
   #define MQTT_SERVER "192.168.1.50"
   ```

3. Flash the firmware to your Arduino Nano ESP32

4. Watch for incoming data:
   ```powershell
   docker exec mosquitto mosquitto_sub -t "iaq/+/telemetry" -v
   ```

## Service Access

| Service | URL/Address | Credentials |
|---------|-------------|-------------|
| Grafana | http://localhost:3000 | admin / admin |
| MQTT Broker | localhost:1883 | None (anonymous) |
| TimescaleDB | localhost:5432 | iaq / iaqpass |

## Common Commands

```powershell
# View all logs
docker compose logs -f

# View specific service logs
docker compose logs -f ingest

# Restart a single service
docker compose restart ingest

# Stop all services
docker compose down

# Stop and delete all data (fresh start)
docker compose down -v

# Rebuild after code changes
docker compose up -d --build

# Check database row count
docker exec postgres psql -U iaq -d iaq -c "SELECT COUNT(*) FROM iaq_readings"

# Recent readings
docker exec postgres psql -U iaq -d iaq -c "SELECT time, device_id, co2_ppm, temp_c FROM iaq_readings ORDER BY time DESC LIMIT 5"
```

## Troubleshooting

### Docker Desktop won't start

1. Verify virtualization is enabled in BIOS
2. Check WSL status: `wsl --status`
3. Update WSL: `wsl --update`
4. Restart Docker Desktop service

### "Port already in use" error

Check what's using the port:

```powershell
netstat -ano | findstr :1883
netstat -ano | findstr :3000
netstat -ano | findstr :5432
```

Either stop the conflicting service or change the port in `docker-compose.yml`.

### Grafana shows "No data"

1. Verify data is being ingested:
   ```powershell
   docker exec postgres psql -U iaq -d iaq -c "SELECT COUNT(*) FROM iaq_readings"
   ```

2. Check ingest service logs:
   ```powershell
   docker compose logs ingest
   ```

3. Verify time range in Grafana (top right) includes your data timestamps

### Sensor data not arriving

1. Check MQTT connectivity:
   ```powershell
   docker exec mosquitto mosquitto_sub -t "#" -v
   ```

2. Verify firewall allows port 1883:
   - Windows Firewall → Allow an app through → Check Docker Desktop

3. Ensure sensor and PC are on the same network

### Database connection errors

Check Postgres is healthy:

```powershell
docker exec postgres pg_isready -U iaq
```

View Postgres logs:

```powershell
docker compose logs postgres
```

### Fresh reinstall

To completely reset and start fresh:

```powershell
docker compose down -v
docker compose up -d
```

This deletes all data and recreates the database schema.

## Updating

Pull latest changes and rebuild:

```powershell
git pull
cd infra
docker compose down
docker compose up -d --build
```

## Network Diagram

```
┌─────────────────┐     MQTT/1883      ┌─────────────────┐
│  Arduino Nano   │ ─────────────────→ │   Mosquitto     │
│     ESP32       │      Wi-Fi         │   (broker)      │
└─────────────────┘                    └────────┬────────┘
                                                │
                                                │ subscribe
                                                ↓
                                       ┌─────────────────┐
                                       │  Ingest Service │
                                       │    (Python)     │
                                       └────────┬────────┘
                                                │
                                                │ INSERT
                                                ↓
┌─────────────────┐     SQL/5432       ┌─────────────────┐
│     Grafana     │ ←───────────────── │   TimescaleDB   │
│   (dashboards)  │      query         │   (database)    │
└─────────────────┘                    └─────────────────┘
```
