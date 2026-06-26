#!/usr/bin/env bash
# Configure the Telegram contact point + notification route for the deadman alert
# via Grafana's API. We use the API (not file provisioning) because file
# provisioning coerces an env-interpolated numeric chat id into a number and
# Grafana's telegram integration rejects it. Idempotent — safe to re-run after
# changing the token. Reads secrets from broker/.env (gitignored). Needs jq.
set -euo pipefail
command -v jq >/dev/null || { echo "needs jq" >&2; exit 1; }
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"   # broker/
get() { grep -E "^$1=" "$DIR/.env" | head -1 | cut -d= -f2-; }

GRAFANA="${GRAFANA_URL:-http://localhost:3001}"
PW="$(get GF_SECURITY_ADMIN_PASSWORD)"
TOKEN="$(get TELEGRAM_BOT_TOKEN)"
CHATID="$(get TELEGRAM_CHAT_ID)"
UID_CP="telegram-deadman"
[ -n "$TOKEN" ] && [ -n "$CHATID" ] || { echo "Set TELEGRAM_BOT_TOKEN and TELEGRAM_CHAT_ID in broker/.env first." >&2; exit 1; }

# Auth via header (keeps the password out of argv); body via stdin (out of argv).
AUTH_HDR="Authorization: Basic $(printf 'admin:%s' "$PW" | base64 | tr -d '\n')"

# fail loudly on any non-2xx (except the PUT's 404, which means "create instead")
api() { # method path  (body on stdin)
  local method="$1" path="$2" code
  code=$(curl -s -o /tmp/gf_resp -w '%{http_code}' -X "$method" \
    -H "$AUTH_HDR" -H 'Content-Type: application/json' -H 'X-Disable-Provenance: true' \
    --data @- "$GRAFANA$path")
  echo "$code"
}

cp_body=$(jq -n --arg uid "$UID_CP" --arg token "$TOKEN" --arg chatid "$CHATID" \
  '{uid:$uid, name:"telegram", type:"telegram", settings:{bottoken:$token, chatid:$chatid}, disableResolveMessage:false}')

code=$(printf '%s' "$cp_body" | api PUT "/api/v1/provisioning/contact-points/$UID_CP")
if [ "$code" = "404" ]; then
  code=$(printf '%s' "$cp_body" | api POST "/api/v1/provisioning/contact-points")
fi
case "$code" in 2*) ;; *) echo "contact point setup failed (HTTP $code): $(cat /tmp/gf_resp)" >&2; exit 1;; esac

pol_body='{"receiver":"telegram","group_by":["alertname","device","_field"],"group_wait":"30s","group_interval":"5m","repeat_interval":"6h"}'
code=$(printf '%s' "$pol_body" | api PUT "/api/v1/provisioning/policies")
case "$code" in 2*) ;; *) echo "policy setup failed (HTTP $code): $(cat /tmp/gf_resp)" >&2; exit 1;; esac

rm -f /tmp/gf_resp
echo "OK: telegram contact point + route configured."
echo "Test delivery:  curl -s \"https://api.telegram.org/bot\$TELEGRAM_BOT_TOKEN/sendMessage\" -d chat_id=\$TELEGRAM_CHAT_ID -d text=monitor-air-test"
