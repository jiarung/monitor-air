# monitor-air MQTT broker

A self-contained [Mosquitto](https://mosquitto.org/) MQTT broker for the
`monitor-air` project, run via Docker Compose. The ESP32 firmware will publish
its sensor readings here once the client side is added.

> **Scope:** broker only. The firmware in `../src` is unchanged.

## Run

```bash
cd broker
docker compose up -d
```

| Action            | Command                                  |
|-------------------|------------------------------------------|
| Status            | `docker compose ps`                      |
| Logs (follow)     | `docker compose logs -f mosquitto`       |
| Stop (keep data)  | `docker compose down`                    |
| Stop + wipe data  | `docker compose down -v`                 |
| Restart           | `docker compose restart`                 |

Retained messages survive `down`/restart — they live in the `mosquitto-data`
named volume. Use `down -v` to clear them.

## Connecting

| Field    | Value                          |
|----------|--------------------------------|
| Host     | the broker machine's LAN IP    |
| Port     | `1883`                         |
| Protocol | plain MQTT (no TLS)            |
| Auth     | none — **anonymous open**      |

The host firewall must allow inbound **1883** for devices to reach the broker.
The ESP32-S3 only supports **2.4 GHz** WiFi, so it must be on a 2.4 GHz network
that can route to this host.

## Test

```bash
# subscribe in the background (inside the container)
docker exec -d monitor-air-mqtt sh -c "mosquitto_sub -t 'monitor-air/#' -v > /tmp/sub.log 2>&1"

# publish a message
docker exec monitor-air-mqtt mosquitto_pub -t 'monitor-air/test' -m 'hello'

# check it arrived
docker exec monitor-air-mqtt cat /tmp/sub.log    # -> monitor-air/test hello
```

From another machine on the LAN (needs `mosquitto-clients` installed):

```bash
mosquitto_pub -h <broker-ip> -t test -m hi
```

## Security — locking down later

Anonymous access is fine for an isolated LAN, but for anything exposed you
should add authentication. In `config/mosquitto.conf`:

```conf
allow_anonymous false
password_file /mosquitto/config/passwd
```

Create the password file (the running broker mounts `config` read-only, so
generate it on the host with a throwaway container, then restart):

```bash
docker run --rm -it -v "$PWD/config:/c" eclipse-mosquitto:2 \
  mosquitto_passwd -c /c/passwd <username>
docker compose restart
```

For encrypted transport, add a TLS listener on `8883` with `cafile` /
`certfile` / `keyfile`.
