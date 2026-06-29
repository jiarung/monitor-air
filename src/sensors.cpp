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

// All sensors share one I2C bus (Wire). Addresses don't collide: BME680 0x76/0x77,
// BH1750 #1 0x23, BH1750 #2 0x5C — so a single SDA/SCL pair serves all of them.
constexpr uint8_t I2C_SDA = 17;
constexpr uint8_t I2C_SCL = 18;

// Two BH1750s by ADDR strap: floating/low -> 0x23 (primary, plant location -> lux),
// tied to 3V3 -> 0x5C (reference, no-natural-light spot -> lux_ref).
constexpr uint8_t BH1750_ADDR_MAIN = 0x23;
constexpr uint8_t BH1750_ADDR_REF  = 0x5C;

Adafruit_BME680 bme;
BH1750 lightMeter;     // primary    @ 0x23 -> lux
BH1750 lightMeterRef;  // reference  @ 0x5C -> lux_ref

bool bmeOk = false;
bool bh1750Ok = false;     // primary present
bool bh1750RefOk = false;  // reference present

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

// Each BH1750 is bound to a FIXED address (no scan) so the two never get confused:
// 0x23 -> lux, 0x5C -> lux_ref. A missing one just leaves its flag false (fail-open).
bool beginBh1750At(BH1750& meter, uint8_t addr, const char* label) {
    if (meter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, addr, &Wire)) {
        logf("[sensors] BH1750 %s ok @ 0x%02X\n", label, addr);
        return true;
    }
    logf("[sensors] BH1750 %s NOT found @ 0x%02X\n", label, addr);
    return false;
}

}  // namespace

bool sensorsBegin() {
    Wire.begin(I2C_SDA, I2C_SCL);
    bmeOk = beginBme();
    bh1750Ok    = beginBh1750At(lightMeter,    BH1750_ADDR_MAIN, "#1 (lux)");
    bh1750RefOk = beginBh1750At(lightMeterRef, BH1750_ADDR_REF,  "#2 (lux_ref)");
    return bmeOk || bh1750Ok || bh1750RefOk;
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

    if (bh1750RefOk) {
        float lux = lightMeterRef.readLightLevel();
        if (lux >= 0.0f && finiteF(lux)) {
            r.lux_ref = lux;
            r.lux_refValid = true;
        }
    }

    return r;
}
