#!/bin/sh
# One-shot migration runner: applies migrations/*.sql in lexical order against
# $PG_HOST/$PG_DB. Idempotent — safe to re-run in both deployment modes
# (standalone bundled DB, or a shared TimescaleDB cluster).
#
# Connects as PG_MIGRATE_USER (default: postgres). The first run needs a role
# that can CREATE ROLE; afterwards iaq_owner suffices.
set -eu

: "${PG_HOST:=db}"
: "${PG_PORT:=5432}"
: "${PG_DB:=warehouse}"
: "${PG_MIGRATE_USER:=postgres}"
: "${PG_MIGRATE_PASSWORD:?PG_MIGRATE_PASSWORD is required}"
: "${IAQ_OWNER_PW:?IAQ_OWNER_PW is required}"
: "${IAQ_RW_PW:?IAQ_RW_PW is required}"
: "${IAQ_RO_PW:?IAQ_RO_PW is required}"

export PGPASSWORD="$PG_MIGRATE_PASSWORD"

echo "waiting for $PG_HOST:$PG_PORT/$PG_DB..."
tries=0
until pg_isready -h "$PG_HOST" -p "$PG_PORT" -q; do
    tries=$((tries + 1))
    if [ "$tries" -ge 60 ]; then
        echo "database never became ready" >&2
        exit 1
    fi
    sleep 2
done

for f in /migrations/*.sql; do
    echo "applying $f"
    psql -h "$PG_HOST" -p "$PG_PORT" -U "$PG_MIGRATE_USER" -d "$PG_DB" \
        -v ON_ERROR_STOP=1 \
        -v iaq_owner_pw="$IAQ_OWNER_PW" \
        -v iaq_rw_pw="$IAQ_RW_PW" \
        -v iaq_ro_pw="$IAQ_RO_PW" \
        -f "$f"
done
echo "migrations complete"
