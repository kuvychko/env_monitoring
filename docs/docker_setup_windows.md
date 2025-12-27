# Docker Setup on Windows

This guide covers installing Docker Desktop on Windows for local development.

## Prerequisites

- Windows 10 version 2004+ (Build 19041+) or Windows 11
- 64-bit processor with Second Level Address Translation (SLAT)
- 4GB+ RAM
- BIOS-level hardware virtualization enabled

## Installation

### 1. Enable WSL2

Open PowerShell as Administrator:

```powershell
wsl --install
```

Restart your computer when prompted.

After restart, set WSL2 as default:

```powershell
wsl --set-default-version 2
```

### 2. Install Docker Desktop

1. Download Docker Desktop from https://www.docker.com/products/docker-desktop/
2. Run the installer
3. During installation, ensure "Use WSL 2 instead of Hyper-V" is checked
4. Restart your computer

### 3. Verify Installation

Open a new terminal:

```powershell
docker --version
docker compose version
```

Test with a simple container:

```powershell
docker run hello-world
```

## Common Commands

```powershell
# Start the stack (from infra/ directory)
docker compose up -d

# View running containers
docker compose ps

# View logs (follow mode)
docker compose logs -f

# View logs for specific service
docker compose logs -f mosquitto

# Stop containers
docker compose down

# Stop and remove volumes (deletes all data!)
docker compose down -v

# Rebuild after changes
docker compose up -d --build
```

## Troubleshooting

### Docker Desktop won't start

1. Ensure WSL2 is properly installed: `wsl --status`
2. Check that virtualization is enabled in BIOS
3. Try restarting the Docker Desktop service

### Containers can't reach each other

Containers on the same Docker network can reach each other by service name. For example, the `ingest` service connects to `mosquitto` and `postgres` by those hostnames.

### Port conflicts

If you see "port already in use":
- Check what's using the port: `netstat -ano | findstr :1883`
- Either stop the conflicting service or change the port mapping in docker-compose.yml

### Accessing services from host

- Grafana: http://localhost:3000 (default: admin/admin)
- MQTT: localhost:1883
- Postgres: localhost:5432

## Testing MQTT

Install mosquitto-clients or use Docker:

```powershell
# Subscribe to all telemetry (in one terminal)
docker run -it --rm --network host eclipse-mosquitto mosquitto_sub -h localhost -t "iaq/+/telemetry" -v

# Publish a test message (in another terminal)
docker run -it --rm --network host eclipse-mosquitto mosquitto_pub -h localhost -t "iaq/test/telemetry" -m '{"test":true}'
```
