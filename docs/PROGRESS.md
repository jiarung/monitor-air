# monitor-air — Progress

_Last updated: 2026-06-24_

A long-running environment-monitoring rig for a **north-facing, narrow-alley balcony** — built to
answer a concrete question: *can ~15 small cacti survive (and a grow light meaningfully
supplement) a low-light north balcony?* The system measures the light/air environment, logs it
forever, and visualizes it, with spectral plant-health sensing planned later.

> Note: this doc was reconciled against the actual repo (git log + `src` + `broker`), so a few
> details differ from earlier hand-written notes — see **Corrections** at the bottom.

---

## Goals

- Quantify the balcony's natural light (lux → daily light integral) in a hard north-facing spot.
- Measure the effect of a full-spectrum grow light as supplemental lighting.
- Collect temperature / humidity / pressure / gas + light continuously, kept forever.
- Visualize in Grafana; later add spectral analysis (AS7341) for grow-light quality and leaf
  reflectance.

## Architecture

```
ESP32-S3 ──MQTT──▶ Mosquitto ──▶ Telegraf ──▶ InfluxDB (SSD) ──▶ Grafana (charts)
 (BME680 + BH1750)                                   └──▶ backups to HDD (/data)
```

Firmware in C++ (PlatformIO / Arduino-esp32). Server side is a single Docker Compose stack in
[`broker/`](../broker/). MVP end-to-end is **done and running**.

## Current state by subsystem

| Subsystem | Status | Detail |
|-----------|--------|--------|
| ESP32-S3 firmware | ✅ done | ESP32-S3-WROOM-1 N16R8; non-blocking loop; publishes every **15 s** |
| I²C bus | ✅ done | Shared `Wire` bus, **SDA=GPIO17 / SCL=GPIO18**; both sensors co-exist (no addr collision) |
| BME680 | ✅ live | temp / humidity / pressure / gas @ `0x77` (or `0x76`); publishing |
| BH1750 | ✅ live | lux @ `0x23`; publishing and charting (the `lux` panel in Grafana) |
| AS7341 (spectral) | 🔲 planned | purchased, not integrated; shares the same I²C bus |
| MQTT broker | ✅ done | Mosquitto 2, `:1883` |
| Ingest + storage | ✅ done | Telegraf 1.30 → InfluxDB 2.7 (bucket `sensors`, kept forever) |
| Grafana | ✅ done | 5 panels (temp/hum/pressure/gas/lux) at `:3001`; provisioned datasource + dashboard |
| Backups | ✅ done | InfluxDB backups to HDD `/data/influx-backups` |
| Power feed | 🟡 ~80% | indoor 110 V → WAGO splice → VCTF 2C 1.25 mm² → through the AC-unit wall port → balcony (validated) |
| Grow light | ✅ online | JIUNPEY 2 ft / 50 W / full-spectrum / IP65 — commissioned today for ~15× 4″ cacti |
| Smart plug | ✅ done | TP-Link Tapo **P110M** — energy metering + scheduling for the grow light |
| Balcony deployment | ✅ deployed | node + sensors + grow light **physically on the balcony** (photos 2026-06-24); node sits in a makeshift tubular shade housing on the railing. Pending: weatherproofing (breadboard → sealed/perfboard box) + rename device tag + move remaining plants over |
| Automation | 🔲 next | grow-light scheduling script — planned for tomorrow |

## Telemetry contract

- Topic: `monitor-air/<device>/telemetry` (`<device>` = `MQTT_DEVICE_ID`, currently `livingroom`).
- Payload: flat float JSON, QoS 0, **not retained** — Telegraf timestamps each message on ingest.
- A sensor that fails to read **omits its field** (fail-open): an unwired BH1750 just drops `lux`.

```json
{"temp":24.8,"hum":51.2,"pressure":1009.3,"gas":12.4,"lux":350.0}
```

## Live evidence (2026-06-24, balcony — device tag still `livingroom`)

The node, both sensors, and the grow light are physically deployed on the north balcony (photos
2026-06-24); the MQTT `device` tag is just not renamed yet (still `livingroom`).

- Grafana (last hour): temp ~35–37 °C, humidity ~48–54 %, pressure ~1003 hPa, gas ~45–60 kΩ,
  lux ~2500–3000 (spike to ~3500 at light-on).
- Tapo P110M: 0.045 kWh today, ~0.8 h runtime, <0.01 NT$/day — grow-light power draw is already
  being logged for the supplemental-lighting cost analysis.

> The ~35–37 °C reading combines a hot June balcony with the BME680's gas-sensor heater, which
> self-heats the die a few °C above ambient — a known quirk; temp-offset compensation is on the
> roadmap (P4).

## Engineering notes (decisions worth keeping)

- **Non-blocking event loop.** WiFi and MQTT reconnect on timers, never spin-wait. The publish
  slot is claimed unconditionally each cycle so a failed/empty read can't hot-loop the sensors
  (`src/main.cpp`).
- **Fail-open telemetry.** Missing sensor → missing field, not a blocked message.
- **Idempotent server boot.** `broker/start.sh` brings the stack up repeatably; restart-safe.
- **DOUT flash-mode workaround.** This unit's off-brand flash chip (mfr `0x46`) won't boot DIO/QIO;
  `platformio.ini` pins `flash_mode = dout`. Diagnose new boards with `esptool flash_id`.
- **Lockdown.** InfluxDB admin API bound to `127.0.0.1` (SSH tunnel only); Grafana on LAN;
  `src/secrets.h` gitignored.
- **Hardware-free testing.** `docker compose --profile sim up -d sim` feeds synthetic telemetry.

## Maturity

```
ESP32 base        ██████████ 100%
MQTT→Influx→Grafana ██████████ 100%
BME680            ██████████ 100%  (calibration pending)
BH1750            ██████████ 100%
Power feed        ████████░░  80%
Grow light        █████████░  90%  (online; scheduling pending)
Balcony deploy    ███████░░░  75%  (deployed; weatherproofing + tag rename)
AS7341 spectral   █░░░░░░░░░  10%  (purchased)
Automation        ░░░░░░░░░░   0%  (next)
```

## Roadmap

**P0 — Deployment hardening.** Node is already on the balcony in a makeshift tubular shade. Finish
the job: rename `MQTT_DEVICE_ID` `livingroom → balcony` (correct data tag), weatherproof the node
(breadboard → perfboard/soldered in a sealed box — a north balcony is shaded but not rain-proof),
and move the remaining cacti over.

**P1 — Automation (tomorrow).** Script grow-light scheduling via the Tapo P110M (TP-Link/Matter
API). Target: lux/DLI-driven — turn on when natural light drops below a threshold and cap the
daily light dose, rather than a dumb timer.

**P2 — Grafana DLI.** Compute daily cumulative light (DLI approximation from lux; note the
lux→PPFD conversion caveat for white/grow-light spectra) and add a daily-integral panel.

**P3 — AS7341 spectral.** Add to the I²C bus; build a measurement flow comparing grow light vs.
natural light vs. leaf reflectance, to judge supplemental-light quality.

**P4 — BME680 calibration.** Compensate the gas-heater temp offset; establish a gas baseline
(BSEC) for a usable VOC/IAQ signal.

**P5 — Alerting & scale.** Grafana alerts (sensor offline, temp/humidity thresholds for plant
health); multi-node by flashing the same firmware with a different `MQTT_DEVICE_ID`.

## Changelog (from git)

- `refactor` BME680 + BH1750 onto a shared I²C bus
- `fix` wait for USB-CDC before printing boot logs
- `chore` publish interval → 15 s
- `feat` timestamps in serial logs
- `feat` idempotent server-stack boot script
- `feat` BH1750 on I²C (initially Wire1 GPIO17/18, since merged to the shared bus)
- `feat` publish sensor telemetry to MQTT
- `feat` InfluxDB + Telegraf + Grafana pipeline with HDD backups

## Corrections vs. earlier notes

- **I²C pins:** earlier notes said GPIO8/9. That was the original BME680-only `Wire` bus (with
  BH1750 on a separate `Wire1` at GPIO17/18); the firmware has since been refactored to a **single
  shared `Wire` bus on GPIO17/18**.
- **BH1750:** earlier notes had it at ~30% / "not yet in MQTT." It is **fully live** — publishing
  `lux` and charting in Grafana.
- **Publish interval:** **15 s** (not 1–3 min).
- **Database:** the store is specifically **InfluxDB** (time-series) fed by **Telegraf**, not a
  generic DB.
- **Deployment ≠ device tag:** the node is **physically on the balcony** (photos 2026-06-24); the
  `livingroom` MQTT tag is just un-renamed config, not a location indicator.
