# monitor-air

ESP32-S3 air/environment monitor firmware. Reads ambient light, temperature,
humidity, pressure and gas resistance over I²C and connects to WiFi for
reporting.

## Hardware

| Item        | Detail                                                        |
|-------------|---------------------------------------------------------------|
| Board       | ESP32-S3-WROOM-1 **N16R8** (16 MB quad flash + 8 MB OPI PSRAM) |
| Light       | **BH1750** ambient light sensor (I²C `0x23`)                  |
| Environment | **BME680** temp / humidity / pressure / gas (I²C `0x77` or `0x76`) |
| Bus         | I²C — `SDA = GPIO8`, `SCL = GPIO9`                             |

Both sensors share the same I²C bus (`Wire`); their addresses don't collide.

### Wiring

```
Sensor VCC  -> 3V3
Sensor GND  -> GND
Sensor SDA  -> GPIO8
Sensor SCL  -> GPIO9
```

## Toolchain

- [PlatformIO](https://platformio.org/) with the Arduino framework for Espressif32.
- Single environment: `esp32-s3-devkitc-1`.

## Getting started

```bash
# 1. Provide your WiFi credentials (secrets.h is gitignored)
cp src/secrets.h.example src/secrets.h
#    then edit src/secrets.h and set WIFI_SSID / WIFI_PASSWORD

# 2. Build
pio run

# 3. Flash (adjust the port to match your machine)
pio run -t upload --upload-port /dev/cu.usbmodem2101

# 4. View serial output (115200 baud)
pio device monitor --port /dev/cu.usbmodem2101 --baud 115200
```

Expected log after a reset:

```
.
192.168.x.x
Boot OK
Temp=24.8C Hum=51.2% Pressure=1009.3hPa Gas=12.4kΩ
...
```

> The WiFi IP and `Boot OK` are printed once in `setup()`. This board uses
> native USB-Serial/JTAG, which does **not** auto-reset when the monitor
> connects — press the **RST/EN** button (with the monitor open) to see the
> boot log again.

## Secrets

WiFi credentials live in `src/secrets.h`, which is **gitignored** and never
committed. Use `src/secrets.h.example` as the template:

```cpp
#pragma once
static const char* WIFI_SSID     = "your-wifi-ssid";
static const char* WIFI_PASSWORD = "your-wifi-password";
```

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

## Project layout

```
monitor-air/
├── platformio.ini        # board, flash config, deps
├── src/
│   ├── main.cpp          # firmware entry point
│   ├── secrets.h         # WiFi credentials (gitignored)
│   └── secrets.h.example # credentials template
├── include/              # project headers
├── lib/                  # private libraries
└── test/                 # unit tests
```
