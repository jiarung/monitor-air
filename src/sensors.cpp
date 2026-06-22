#include "sensors.h"

#include "log.h"

// Portable finite check: NaN fails (v == v), and the bounds reject +/-Inf.
// Avoids the newlib isfinite() macro vs std::isfinite ambiguity on xtensa.
static inline bool finiteF(float v) {
    return v == v && v < 3.4e38f && v > -3.4e38f;
}

#include <Wire.h>
#include <BH1750.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>

namespace {

// BME680 on the primary I2C bus (Wire); BH1750 on a separate bus (Wire1) since
// it's wired to its own pins.
constexpr uint8_t BME_SDA   = 8;
constexpr uint8_t BME_SCL   = 9;
constexpr uint8_t LIGHT_SDA = 17;
constexpr uint8_t LIGHT_SCL = 18;

Adafruit_BME680 bme;
BH1750 lightMeter;

bool bmeOk = false;
bool bh1750Ok = false;

// BME680 sits at 0x77 on most Adafruit-style modules, 0x76 on some clones.
bool beginBme() {
    for (uint8_t addr : {0x77, 0x76}) {
        if (bme.begin(addr)) {
            bme.setTemperatureOversampling(BME680_OS_8X);
            bme.setHumidityOversampling(BME680_OS_2X);
            bme.setPressureOversampling(BME680_OS_4X);
            bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
            bme.setGasHeater(320, 150);  // 320 degC for 150 ms
            logf("[sensors] BME680 ok @ 0x%02X\n", addr);
            return true;
        }
    }
    logln("[sensors] BME680 NOT found (0x77/0x76)");
    return false;
}

// BH1750 ADDR unconnected -> 0x23 (ADDR high would be 0x5C). On Wire1.
bool beginBh1750() {
    for (uint8_t addr : {0x23, 0x5C}) {
        if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, addr, &Wire1)) {
            logf("[sensors] BH1750 ok @ 0x%02X\n", addr);
            return true;
        }
    }
    logln("[sensors] BH1750 NOT found (0x23/0x5C)");
    return false;
}

}  // namespace

bool sensorsBegin() {
    Wire.begin(BME_SDA, BME_SCL);
    Wire1.begin(LIGHT_SDA, LIGHT_SCL);
    bmeOk = beginBme();
    bh1750Ok = beginBh1750();
    return bmeOk || bh1750Ok;
}

SensorReading sensorsRead() {
    SensorReading r;

    if (bmeOk && bme.performReading()) {
        if (finiteF(bme.temperature))   { r.temp = bme.temperature;            r.tempValid = true; }
        if (finiteF(bme.humidity))      { r.hum = bme.humidity;                r.humValid = true; }
        if (finiteF(bme.pressure))      { r.pressure = bme.pressure / 100.0f;  r.pressureValid = true; }  // Pa -> hPa
        if (finiteF(bme.gas_resistance)){ r.gas = bme.gas_resistance / 1000.0f; r.gasValid = true; }      // Ohm -> kOhm
    }

    if (bh1750Ok) {
        float lux = lightMeter.readLightLevel();
        if (lux >= 0.0f && finiteF(lux)) {  // BH1750 returns -1/-2 on error
            r.lux = lux;
            r.luxValid = true;
        }
    }

    return r;
}
