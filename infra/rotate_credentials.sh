#!/usr/bin/env bash
# rotate_credentials.sh — rotate service credentials before making the repo public.
#
# Rotates:  POSTGRES_PASSWORD, GRAFANA_ADMIN_PASSWORD, PURPLEAIR_API_KEY, PURPLEAIR_READ_KEY
# Skipped:  MQTT_PASSWORD (baked into ESP32 firmware via secrets.h)
# Manual:   SSH password — see reminder at the end
#
# Run from the infra/ directory:
#   cd infra && bash rotate_credentials.sh

set -euo pipefail

BOLD='\033[1m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

info()    { echo -e "${BOLD}[rotate]${NC} $*"; }
success() { echo -e "${GREEN}[ok]${NC}    $*"; }
warn()    { echo -e "${YELLOW}[warn]${NC}  $*"; }
die()     { echo -e "${RED}[error]${NC} $*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# 1. Preflight
# ---------------------------------------------------------------------------
[[ -f ".env" ]] || die ".env not found — run this script from the infra/ directory."
command -v docker   >/dev/null 2>&1 || die "docker not found."
command -v openssl  >/dev/null 2>&1 || die "openssl not found."
command -v python3  >/dev/null 2>&1 || die "python3 not found."
command -v curl     >/dev/null 2>&1 || die "curl not found."

for svc in postgres grafana; do
    state=$(docker inspect --format='{{.State.Status}}' "$svc" 2>/dev/null || true)
    [[ "$state" == "running" ]] || die "Container '$svc' is not running (state: ${state:-not found}). Start the stack first."
done

info "Preflight checks passed."

# ---------------------------------------------------------------------------
# 2. Read current credentials from .env
# ---------------------------------------------------------------------------
OLD_PG_PASS=$(grep    '^POSTGRES_PASSWORD='        .env | cut -d= -f2-)
OLD_GF_PASS=$(grep    '^GRAFANA_ADMIN_PASSWORD='   .env | cut -d= -f2-)
OLD_PA_KEY=$(grep     '^PURPLEAIR_API_KEY='        .env | cut -d= -f2-)
OLD_PA_READ=$(grep    '^PURPLEAIR_READ_KEY='       .env | cut -d= -f2-)

[[ -n "$OLD_PG_PASS" ]] || die "POSTGRES_PASSWORD not found in .env"
[[ -n "$OLD_GF_PASS" ]] || die "GRAFANA_ADMIN_PASSWORD not found in .env"

# ---------------------------------------------------------------------------
# 3. Generate new passwords (hex = no special chars to escape)
# ---------------------------------------------------------------------------
NEW_PG_PASS=$(openssl rand -hex 24)
NEW_GF_PASS=$(openssl rand -hex 24)
info "Generated new passwords for Postgres and Grafana."

# ---------------------------------------------------------------------------
# 4. Prompt for PurpleAir keys (Enter = keep current)
# ---------------------------------------------------------------------------
echo ""
echo "PurpleAir keys (press Enter to keep current value):"
read -r -p "  New PURPLEAIR_API_KEY  [current: ${OLD_PA_KEY:0:8}...]: " INPUT_PA_KEY
read -r -p "  New PURPLEAIR_READ_KEY [current: ${OLD_PA_READ:0:8}...]: " INPUT_PA_READ
NEW_PA_KEY="${INPUT_PA_KEY:-$OLD_PA_KEY}"
NEW_PA_READ="${INPUT_PA_READ:-$OLD_PA_READ}"
echo ""

# ---------------------------------------------------------------------------
# 5. Backup .env
# ---------------------------------------------------------------------------
BACKUP=".env.bak.$(date +%Y%m%d_%H%M%S)"
cp .env "$BACKUP"
success "Backed up .env → $BACKUP"

# ---------------------------------------------------------------------------
# 6. Rotate Postgres password (live — no service interruption)
# ---------------------------------------------------------------------------
info "Rotating Postgres password..."
docker exec postgres psql -U iaq -d iaq \
    -c "ALTER USER iaq WITH PASSWORD '$NEW_PG_PASS';" -q
success "Postgres password updated."

# ---------------------------------------------------------------------------
# 7. Rotate Grafana admin password via HTTP API (Grafana stays running)
# ---------------------------------------------------------------------------
info "Rotating Grafana admin password..."
GF_OUTPUT=$(docker exec grafana grafana-cli admin reset-admin-password "$NEW_GF_PASS" 2>&1)

if echo "$GF_OUTPUT" | grep -q "Admin password changed successfully"; then
    success "Grafana password updated."
else
    warn "grafana-cli failed: $GF_OUTPUT. Rolling back Postgres password..."
    docker exec postgres psql -U iaq -d iaq \
        -c "ALTER USER iaq WITH PASSWORD '$OLD_PG_PASS';" -q
    cp "$BACKUP" .env
    die "Grafana password rotation failed. .env and Postgres restored from backup."
fi

# ---------------------------------------------------------------------------
# 8. Update .env (Python handles arbitrary characters safely)
# ---------------------------------------------------------------------------
info "Updating .env..."
python3 - <<PYEOF
import re

with open('.env', 'r') as f:
    content = f.read()

def replace_var(text, key, value):
    return re.sub(
        rf'^{re.escape(key)}=.*$',
        f'{key}={value}',
        text,
        flags=re.MULTILINE
    )

content = replace_var(content, 'POSTGRES_PASSWORD',      '${NEW_PG_PASS}')
content = replace_var(content, 'GRAFANA_ADMIN_PASSWORD', '${NEW_GF_PASS}')
content = replace_var(content, 'PURPLEAIR_API_KEY',      '${NEW_PA_KEY}')
content = replace_var(content, 'PURPLEAIR_READ_KEY',     '${NEW_PA_READ}')

with open('.env', 'w') as f:
    f.write(content)
PYEOF
success ".env updated."

# ---------------------------------------------------------------------------
# 9. Restart services so they pick up the new .env values
#    (docker compose up -d recreates containers when env changes)
# ---------------------------------------------------------------------------
info "Restarting services..."
docker compose up -d --build ingest ingest_purpleair grafana
success "Services restarted."

# ---------------------------------------------------------------------------
# 10. Wait for containers to reach running state (up to 30s)
# ---------------------------------------------------------------------------
info "Waiting for services to come up..."
for svc in ingest ingest_purpleair grafana; do
    for i in $(seq 1 15); do
        state=$(docker inspect --format='{{.State.Status}}' "$svc" 2>/dev/null || true)
        if [[ "$state" == "running" ]]; then
            success "$svc is running."
            break
        fi
        if [[ $i -eq 15 ]]; then
            warn "$svc did not reach running state — check: docker compose logs $svc"
        fi
        sleep 2
    done
done

# ---------------------------------------------------------------------------
# 11. Summary
# ---------------------------------------------------------------------------
echo ""
echo -e "${BOLD}════════════════════════════════════════════════════════${NC}"
echo -e "${BOLD}  New credentials — save these in your password manager  ${NC}"
echo -e "${BOLD}════════════════════════════════════════════════════════${NC}"
printf "  %-26s %s\n" "POSTGRES_PASSWORD"      "$NEW_PG_PASS"
printf "  %-26s %s\n" "GRAFANA_ADMIN_PASSWORD"  "$NEW_GF_PASS"
printf "  %-26s %s\n" "PURPLEAIR_API_KEY"       "$NEW_PA_KEY"
printf "  %-26s %s\n" "PURPLEAIR_READ_KEY"      "$NEW_PA_READ"
echo -e "${BOLD}════════════════════════════════════════════════════════${NC}"
echo ""
echo -e "${YELLOW}Not rotated (flashed to ESP32 firmware):${NC}"
echo "  MQTT_PASSWORD — unchanged. To rotate it you must update"
echo "  secrets.h, reflash the firmware, update infra/mosquitto/passwd,"
echo "  and restart Mosquitto."
echo ""
echo -e "${YELLOW}SSH password (manual step):${NC}"
echo "  Run on the Pi:  passwd"
echo ""
success "Done. Old credentials are in $BACKUP — delete it after saving the new ones."
