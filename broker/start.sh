#!/usr/bin/env bash
# Bring up the monitor-air server stack (Mosquitto + InfluxDB + Telegraf + Grafana).
# Idempotent: safe to run anytime, including after a reboot. Does NOT start the
# sim publisher (that's opt-in: docker compose --profile sim up -d sim).
set -euo pipefail

BROKER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$BROKER_DIR"

# --- prerequisites ---
if [ ! -f .env ]; then
  echo "ERROR: $BROKER_DIR/.env not found (copy .env.example and fill it in)" >&2
  exit 1
fi
mkdir -p /data/influx-backups   # HDD backup target (no-op if it already exists)

# --- start core services ---
echo "Starting stack..."
docker compose up -d

# --- wait for InfluxDB to report healthy ---
echo -n "Waiting for InfluxDB to be healthy"
for _ in $(seq 1 30); do
  status="$(docker inspect -f '{{.State.Health.Status}}' monitor-air-influxdb 2>/dev/null || echo unknown)"
  if [ "$status" = "healthy" ]; then echo " OK"; break; fi
  echo -n "."; sleep 2
done

echo
docker compose ps
echo
echo "Grafana:  http://localhost:3001   (also http://100.118.198.67:3001 over Tailscale)"
echo "To feed test data:  docker compose --profile sim up -d sim"
