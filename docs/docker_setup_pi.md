# Docker Setup on Raspberry Pi

This guide covers installing Docker on Raspberry Pi OS (Bookworm or newer).

## Prerequisites

- Raspberry Pi 3, 4, or 5 (4+ recommended for running the full stack)
- Raspberry Pi OS (64-bit recommended for Pi 4/5)
- At least 2GB RAM (4GB+ recommended)
- Internet connection

## Installation

### 1. Update system

```bash
sudo apt update && sudo apt upgrade -y
```

### 2. Install Docker using the convenience script

```bash
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh
```

### 3. Add your user to the docker group

This allows running docker commands without sudo:

```bash
sudo usermod -aG docker $USER
```

Log out and back in for the group change to take effect, or run:

```bash
newgrp docker
```

### 4. Enable Docker to start on boot

```bash
sudo systemctl enable docker
sudo systemctl enable containerd
```

### 5. Verify installation

```bash
docker --version
docker compose version
docker run hello-world
```

## Common Commands

```bash
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

## ARM Considerations

All images used in this project have ARM64 support:
- `eclipse-mosquitto:2` - ARM64 native
- `postgres:16` - ARM64 native
- `grafana/grafana:latest` - ARM64 native

If you need TimescaleDB later, use `timescale/timescaledb:latest-pg16` which also supports ARM64.

## Performance Tips

### Use a good SD card or SSD

For database workloads, consider:
- High-endurance microSD card (A2 rated)
- USB SSD boot (recommended for production)

### Memory limits

On a 2GB Pi, you may want to add memory limits to docker-compose.yml:

```yaml
services:
  postgres:
    mem_limit: 512m
  grafana:
    mem_limit: 256m
```

### Swap

Ensure swap is enabled for memory-intensive operations:

```bash
# Check current swap
free -h

# If no swap, create a swapfile
sudo fallocate -l 2G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile

# Make permanent
echo '/swapfile none swap sw 0 0' | sudo tee -a /etc/fstab
```

## Network Configuration

### Find your Pi's IP address

```bash
hostname -I
```

### Static IP (recommended for servers)

Edit `/etc/dhcpcd.conf`:

```
interface eth0
static ip_address=192.168.1.100/24
static routers=192.168.1.1
static domain_name_servers=192.168.1.1 8.8.8.8
```

### Firewall (optional)

If using UFW:

```bash
sudo ufw allow 1883/tcp  # MQTT
sudo ufw allow 3000/tcp  # Grafana
sudo ufw allow 5432/tcp  # Postgres (only if external access needed)
```

## Testing MQTT

```bash
# Install mosquitto clients
sudo apt install mosquitto-clients

# Subscribe to all telemetry
mosquitto_sub -h localhost -t "iaq/+/telemetry" -v

# Publish a test message
mosquitto_pub -h localhost -t "iaq/test/telemetry" -m '{"test":true}'
```

## Accessing Services

From any device on the same network (replace `192.168.1.100` with your Pi's IP):

- Grafana: http://192.168.1.100:3000 (default: admin/admin)
- MQTT broker: 192.168.1.100:1883
- Postgres: 192.168.1.100:5432 (if port exposed)
