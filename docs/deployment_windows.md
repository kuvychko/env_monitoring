# Deploying IAQ Monitoring on Windows

Complete guide for deploying the Indoor Air Quality monitoring stack on Windows using Docker Desktop.

## Prerequisites

- **Windows**: Windows 10 version 2004+ (Build 19041+) or Windows 11
- **Hardware**: 64-bit CPU with virtualization support (Intel VT-x/AMD-V), 8GB+ RAM recommended
- **Virtualization**: Enabled in BIOS/UEFI settings
- **Git**: For cloning the repository

## Architecture Overview

The stack consists of 5 long-running services plus a one-shot migration job:

| Service | Port | Description |
|---------|------|-------------|
| Mosquitto | 1883 | MQTT broker for indoor sensor data |
| TimescaleDB (`db`) | 5432 | PostgreSQL with time-series extensions (internal only) |
| migrate | - | One-shot job: applies idempotent SQL migrations, then exits |
| ingest_mqtt | - | Python service: MQTT → Database (indoor) |
| ingest_purpleair | - | Python service: PurpleAir HTTP API → Database (outdoor) |
| Grafana | 3000 | Dashboards and visualization |

This guide covers **standalone mode** — the bundled database, fully
self-contained (the `--profile standalone` flag below).

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
git clone https://github.com/kuvychko/env_monitoring.git
cd env_monitoring
```

## Step 3: Configure Credentials

```powershell
cd infra
copy .env.example .env

# Open .env in your editor — see comments inside for what each value should be.
# Generate strong passwords with whichever tool you have available, e.g.:
#   - Git Bash:   openssl rand -base64 24
#   - PowerShell: -join ((48..57) + (65..90) + (97..122) | Get-Random -Count 24 | % {[char]$_})
notepad .env
```

Save the generated passwords in your password manager — you'll need
`MQTT_PASSWORD` again when you flash the firmware.

**Optional — PurpleAir outdoor sensor:** if you have a PurpleAir node, also set
`PURPLEAIR_API_KEY`, `PURPLEAIR_SENSOR_INDEX`, and `PURPLEAIR_READ_KEY` in
`.env`. Get the keys from `develop.purpleair.com`. If you don't have a
PurpleAir sensor, leave the variables blank or comment out the
`ingest_purpleair` service in `docker-compose.yml`.

## Step 4: Start the Services

```powershell
docker compose --profile standalone up -d --build
```

First run will:
- Pull images (~500MB total)
- Build the ingest services
- Run the `migrate` job (creates the `iaq` schema, roles, and hypertables, then exits)
- Provision Grafana dashboards

Monitor startup progress:

```powershell
docker compose logs -f
```

Press `Ctrl+C` to exit logs.

## Step 5: Verify Deployment

### 5.1 Check Container Status

```powershell
docker compose ps
```

The long-running services show `running`; `migrate` shows `Exited (0)` —
that's its job done:

```
NAME              STATUS
db                running
grafana           running
infra-migrate-1   exited (0)
ingest            running
ingest_purpleair  running
mosquitto         running
```

### 5.2 Test MQTT Broker

The broker requires authentication. Replace `<MQTT_PASSWORD>` below with the
value from `infra/.env`.

Open two terminals. In the first, subscribe to telemetry:

```powershell
docker exec mosquitto mosquitto_sub -h localhost -u iaq -P "<MQTT_PASSWORD>" -t "iaq/+/telemetry" -v
```

In the second, publish a test message:

```powershell
docker exec mosquitto mosquitto_pub -h localhost -u iaq -P "<MQTT_PASSWORD>" -t "iaq/test/telemetry" -m "{\"device_id\":\"test\",\"co2_ppm\":450}"
```

You should see the message appear in the first terminal.

### 5.3 Verify Database

Connect to the database and check the schema:

```powershell
docker exec -it db psql -U iaq_owner -d warehouse -c "\dt iaq.*"
```

Expected output:

```
                List of relations
 Schema |        Name        | Type  |   Owner
--------+--------------------+-------+-----------
 iaq    | iaq_readings       | table | iaq_owner
 iaq    | purpleair_readings | table | iaq_owner
```

### 5.4 Access Grafana

1. Open http://localhost:3000 in your browser
2. Login with `admin` and the value of `GRAFANA_ADMIN_PASSWORD` from `.env`
3. Navigate to Dashboards → Browse
4. You should see: Overview, PM Deep Dive, Data Quality, PurpleAir Outdoor

## Step 6: Configure the Sensor

Update the firmware to point to your Windows machine:

1. Find your IP address:
   ```powershell
   ipconfig
   ```
   Look for "IPv4 Address" under your active network adapter (e.g., `192.168.1.50`)

2. Copy the secrets template and edit it:
   ```powershell
   copy firmware\nano-esp32-iaq\secrets.example.h firmware\nano-esp32-iaq\secrets.h
   ```

3. Edit `firmware/nano-esp32-iaq/secrets.h` — fill in Wi-Fi credentials, set
   `MQTT_HOST` to your machine's IP, and paste the `MQTT_PASSWORD` from `.env`:
   ```cpp
   #define WIFI_SSID "your_wifi_ssid"
   #define WIFI_PASS "your_wifi_password"
   #define MQTT_HOST "192.168.1.50"
   #define MQTT_PORT 1883
   #define MQTT_USER "iaq"
   #define MQTT_PASS "<paste MQTT_PASSWORD from infra/.env>"
   ```

4. Flash the firmware to your Arduino Nano ESP32

5. Watch for incoming data (replace `<MQTT_PASSWORD>` with the value from `infra/.env`):
   ```powershell
   docker exec mosquitto mosquitto_sub -h localhost -u iaq -P "<MQTT_PASSWORD>" -t "iaq/+/telemetry" -v
   ```

## Service Access

| Service | URL/Address | Credentials |
|---------|-------------|-------------|
| Grafana | http://localhost:3000 | admin / \<GRAFANA_ADMIN_PASSWORD from .env\> |
| MQTT Broker | localhost:1883 | iaq / \<MQTT_PASSWORD from .env\> |
| TimescaleDB | Internal only | Not exposed externally |

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
docker exec db psql -U iaq_owner -d warehouse -c "SELECT COUNT(*) FROM iaq_readings"

# Recent readings
docker exec db psql -U iaq_owner -d warehouse -c "SELECT time, device_id, co2_ppm, temp_c FROM iaq_readings ORDER BY time DESC LIMIT 5"

# Re-apply migrations (idempotent, safe anytime)
docker compose run --rm migrate
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
   docker exec db psql -U iaq_owner -d warehouse -c "SELECT COUNT(*) FROM iaq_readings"
   ```

2. Check ingest service logs:
   ```powershell
   docker compose logs ingest
   ```

3. Verify time range in Grafana (top right) includes your data timestamps

### Sensor data not arriving

1. Check MQTT connectivity (replace `<MQTT_PASSWORD>` with the value from `infra/.env`):
   ```powershell
   docker exec mosquitto mosquitto_sub -h localhost -u iaq -P "<MQTT_PASSWORD>" -t "#" -v
   ```

2. Verify firewall allows port 1883:
   - Windows Firewall → Allow an app through → Check Docker Desktop

3. Ensure sensor and PC are on the same network

### Database connection errors

Check Postgres is healthy:

```powershell
docker exec db pg_isready -U postgres
```

View Postgres logs:

```powershell
docker compose logs db
```

### Fresh reinstall

To completely reset and start fresh:

```powershell
docker compose --profile standalone down -v
docker compose --profile standalone up -d --build
```

This deletes all data and recreates the database schema.

## Updating

Pull latest changes and rebuild:

```powershell
git pull
cd infra
docker compose --profile standalone down
docker compose --profile standalone up -d --build
```

## Network Diagram

```mermaid
flowchart LR
    esp["Arduino Nano ESP32"] -->|"MQTT/1883\nWi-Fi"| mosq["Mosquitto\n(broker)"]
    mosq -->|subscribe| ingest["Ingest Service\n(Python)"]
    ingest -->|INSERT| db[("TimescaleDB\n(database)")]
    db -->|"SQL/5432"| grafana["Grafana\n(dashboards)"]
```
