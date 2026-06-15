#!/bin/sh
# Synthetic telemetry publisher for pipeline testing.
# Publishes float JSON to monitor-air/sim/telemetry every few seconds.
# POSIX sh (busybox) — no $RANDOM; uses awk rand() reseeded each iteration.

BROKER="${BROKER_HOST:-mosquitto}"
TOPIC="${TOPIC:-monitor-air/sim/telemetry}"
INTERVAL="${INTERVAL:-5}"

echo "sim: publishing to ${BROKER} topic=${TOPIC} every ${INTERVAL}s"

i=0
while true; do
  i=$((i + 1))
  json=$(awk -v seed="$i" 'BEGIN {
    srand(seed * 7 + 13);
    temp = 22 + rand() * 6;      # 22-28 C
    hum  = 45 + rand() * 20;     # 45-65 %
    pres = 1005 + rand() * 15;   # 1005-1020 hPa
    gas  = 8 + rand() * 10;      # 8-18 kohm
    lux  = 100 + rand() * 800;   # 100-900 lux
    printf "{\"temp\":%.1f,\"hum\":%.1f,\"pressure\":%.1f,\"gas\":%.1f,\"lux\":%.1f}", temp, hum, pres, gas, lux
  }')
  if mosquitto_pub -h "$BROKER" -t "$TOPIC" -m "$json"; then
    echo "published: $json"
  else
    echo "publish failed (broker not reachable?), retrying..." >&2
  fi
  sleep "$INTERVAL"
done
