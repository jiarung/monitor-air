#include <Arduino.h>
#include <Wire.h>
#include <BH1750.h>
#include <WiFi.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
#include "secrets.h"

Adafruit_BME680 bme;
BH1750 lightMeter;

void setup() {
    Serial.begin(115200);

    Wire.begin(8, 9);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    if (!bme.begin()) {
        Serial.println("BME680 not found");
        while (1);
    }

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.println(WiFi.localIP());


    Serial.println("Boot OK");
}

void loop() {
    if (bme.performReading()) {

        Serial.printf(
            "Temp=%.1fC Hum=%.1f%% Pressure=%.1fhPa Gas=%.1fkΩ\n",
            bme.temperature,
            bme.humidity,
            bme.pressure / 100.0,
            bme.gas_resistance / 1000.0
        );
    }

    delay(3000);
}
