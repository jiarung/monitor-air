# monitor-air

ESP32-S3 air/environment monitor firmware. Reads ambient light, temperature,
humidity, pressure and gas resistance over I²C and connects to WiFi for
reporting.

## Features

- Measures temperature, humidity, pressure, gas resistance (a VOC proxy) and ambient light.
- ESP32-S3 over WiFi with a non-blocking event loop; publishes every 15 s.
- End-to-end MQTT → InfluxDB → Grafana pipeline for long-term storage and visualization.
- Fail-open telemetry: a sensor that fails to read just omits its field instead of blocking the message.
- Multi-node ready: flash the same firmware and change only `MQTT_DEVICE_ID` per unit.

## Hardware

| Item        | Detail                                                        |
|-------------|---------------------------------------------------------------|
| Board       | ESP32-S3-WROOM-1 **N16R8** (16 MB quad flash + 8 MB OPI PSRAM) |
| Light       | **BH1750** ambient light sensor (I²C `0x23`)                  |
| Environment | **BME680** temp / humidity / pressure / gas (I²C `0x77` or `0x76`) |
| Bus         | I²C — `SDA = GPIO17`, `SCL = GPIO18`                          |

Both sensors share the same I²C bus (`Wire`); their addresses don't collide.

### Wiring

```
Sensor VCC  -> 3V3
Sensor GND  -> GND
Sensor SDA  -> GPIO17
Sensor SCL  -> GPIO18
```

## Toolchain

- [PlatformIO](https://platformio.org/) with the Arduino framework for Espressif32.
- Single environment: `esp32-s3-devkitc-1`.

## Getting started

```bash
# 1. Provide your config (secrets.h is gitignored)
cp src/secrets.h.example src/secrets.h
#    then edit src/secrets.h: WIFI_SSID / WIFI_PASSWORD, MQTT_HOST (broker LAN
#    IP), and MQTT_DEVICE_ID (this unit's name)

# 2. Build
pio run

# 3. Flash (adjust the port to match your machine)
pio run -t upload --upload-port /dev/cu.usbmodem2101

# 4. View serial output (115200 baud)
pio device monitor --port /dev/cu.usbmodem2101 --baud 115200
```

Expected log after a reset:

```
[boot] monitor-air starting
[sensors] BME680 ok @ 0x77
[sensors] BH1750 ok @ 0x23
[mqtt] topic=monitor-air/sensor-01/telemetry
[wifi] connecting to <ssid> ...
[mqtt] connected as monitor-air-sensor-01 -> 192.168.x.x:1883
[mqtt] published monitor-air/sensor-01/telemetry {"temp":24.8,"hum":51.2,"pressure":1009.3,"gas":12.4,"lux":350.0}
```

> The WiFi IP and `Boot OK` are printed once in `setup()`. This board uses
> native USB-Serial/JTAG, which does **not** auto-reset when the monitor
> connects — press the **RST/EN** button (with the monitor open) to see the
> boot log again.

## Secrets

WiFi credentials and MQTT settings live in `src/secrets.h`, which is
**gitignored** and never committed. Use `src/secrets.h.example` as the template:

```cpp
#pragma once
static const char*    WIFI_SSID      = "your-wifi-ssid";
static const char*    WIFI_PASSWORD  = "your-wifi-password";
static const char*    MQTT_HOST      = "192.168.x.x"; // broker machine's LAN IP
static const uint16_t MQTT_PORT      = 1883;
static const char*    MQTT_DEVICE_ID = "sensor-01";   // topic segment + InfluxDB device tag
static const char*    MQTT_USER      = "";             // empty = anonymous broker
static const char*    MQTT_PASS      = "";
```

`MQTT_DEVICE_ID` must be `[A-Za-z0-9_-]` (no spaces / `+` / `#`) — it becomes a
topic segment and is validated at boot. Flash the same firmware to several
devices and just change this one value per unit.

ESP32-S3 only supports **2.4 GHz** WiFi — a 5 GHz SSID will never connect.

## Flash mode note (important)

This particular unit ships with an off-brand flash chip (manufacturer `0x46`)
that does **not** boot in DIO/QIO mode — it fails with
`Invalid image block, can't boot`. The fix, already baked into
`platformio.ini`, is to use **DOUT** flash mode:

```ini
board_build.flash_mode = dout   ; header mode byte -> DOUT
board_build.boot       = dio    ; only qio/dio/opi bootloaders ship prebuilt
```

If you swap to a board with a mainstream flash chip you can switch back to
`qio` for faster execution. Diagnose with `esptool flash_id` (check the
manufacturer ID) if a fresh board won't boot.

## Adding libraries

PlatformIO does **not** auto-download a header just because you `#include` it —
declare it in `lib_deps` first, or you'll get `No such file or directory`:

```bash
# 1. add the package to lib_deps in platformio.ini
# 2. build (downloads the dep)
pio run
# 3. refresh the clang index so editor highlighting/diagnostics work
pio run -t compiledb
```

Current dependencies:

- `claws/BH1750`
- `adafruit/Adafruit BME680 Library` (pulls Adafruit Unified Sensor + BusIO)

## Server-side data pipeline

The whole server side is a Docker Compose stack in [`broker/`](broker/):

```
ESP32 ──MQTT──▶ Mosquitto ──▶ Telegraf ──▶ InfluxDB (SSD) ──▶ Grafana (charts)
                                                  └──▶ backups to HDD (/data)
```

- **Mosquitto** — MQTT broker, devices publish to `<host>:1883`
- **Telegraf** — subscribes to MQTT, writes to InfluxDB (no code, just config)
- **InfluxDB** — time-series storage on the SSD (bucket `sensors`, kept forever)
- **Grafana** — dashboard at `http://<host>:3001` with a time-range picker
- backups run to the HDD at `/data/influx-backups`

Bring it up with `cd broker && cp .env.example .env && docker compose up -d`.
For testing without hardware, `docker compose --profile sim up -d sim` feeds
synthetic data. See [`broker/README.md`](broker/README.md) for setup, the
MQTT topic/JSON contract, backups, and lockdown steps.

The firmware publishes to this pipeline. Every 15 seconds it sends flat float
JSON to `monitor-air/<device>/telemetry` (QoS 0, not retained — Telegraf
timestamps each message on ingest), e.g.:

```json
{"temp":24.8,"hum":51.2,"pressure":1009.3,"gas":12.4,"lux":350.0}
```

`<device>` is set by `MQTT_DEVICE_ID` in `src/secrets.h`. A sensor that fails
to read simply omits its field (so an unwired BH1750 just drops `lux`). See
[`broker/README.md`](broker/README.md) for the full contract.

## Security

This stack targets a **trusted LAN**: the MQTT broker is anonymous-open and
Grafana is served over plain HTTP. Before exposing anything beyond your LAN,
enable MQTT auth, put Grafana behind a TLS reverse proxy, and rotate the
InfluxDB token — see the lockdown steps in
[`broker/README.md`](broker/README.md#security-note). Real credentials live only
in `src/secrets.h` and `broker/.env`, both gitignored and never committed.

## Known limitations

- **BME680 temperature reads high:** the gas-sensor heater self-heats the die,
  so readings run ~3–5 °C above ambient. Temperature-offset compensation isn't
  done yet.
- **Light is lux, not PPFD:** the BH1750 measures white-light illuminance; plant
  photosynthetic light (PPFD / spectral quality) needs a separate sensor (e.g. AS7341).
- **WiFi is 2.4 GHz only:** the ESP32-S3 will never connect to a 5 GHz SSID.
- **Broker is open by default:** anonymous MQTT and plain-HTTP Grafana — intended
  for a trusted LAN only (see [Security](#security)).

## Project layout

```
monitor-air/
├── platformio.ini        # board, flash config, deps
├── broker/               # server stack: Mosquitto + Telegraf + InfluxDB + Grafana
├── src/
│   ├── main.cpp          # non-blocking orchestration + publish schedule
│   ├── sensors.h/.cpp    # BME680 + BH1750 -> SensorReading snapshot
│   ├── mqtt_client.h/.cpp# PubSubClient wrapper: connect + publish telemetry
│   ├── secrets.h         # WiFi + MQTT config (gitignored)
│   └── secrets.h.example # config template
├── include/              # project headers
├── lib/                  # private libraries
└── test/                 # unit tests
```
