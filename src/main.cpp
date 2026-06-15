#include <Arduino.h>
#include <WiFi.h>

#include "secrets.h"
#include "sensors.h"
#include "mqtt_client.h"

// Publish cadence — change this one line to retune. 3 minutes by design.
static const uint32_t PUBLISH_INTERVAL_MS = 180000;
static const uint32_t WIFI_RETRY_MS       = 10000;

static uint32_t lastPublish = 0;       // 0 = never published yet (publish ASAP once connected)
static uint32_t lastWifiAttempt = 0;

// Non-blocking WiFi: kick off a connect, retry on a timer, never spin-wait.
static void wifiEnsure() {
    if (WiFi.status() == WL_CONNECTED) return;
    uint32_t now = millis();
    if (lastWifiAttempt != 0 && now - lastWifiAttempt < WIFI_RETRY_MS) return;
    lastWifiAttempt = now;
    Serial.printf("[wifi] connecting to %s ...\n", WIFI_SSID);
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n[boot] monitor-air starting");

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    if (!sensorsBegin()) {
        Serial.println("[sensors] none available — continuing (will publish nothing useful)");
    }
    mqttSetup();
    wifiEnsure();  // start the first (non-blocking) connection attempt
}

void loop() {
    wifiEnsure();  // non-blocking reconnect
    mqttLoop();    // pump client + rate-limited MQTT reconnect

    uint32_t now = millis();
    bool due = (lastPublish == 0) || (now - lastPublish >= PUBLISH_INTERVAL_MS);
    if (due && mqttConnected()) {
        // Claim this slot unconditionally: advancing only on success would hot-loop
        // (re-reading sensors every iteration) whenever a read yields no valid
        // fields or a publish fails. A failed/empty cycle just waits for the next.
        lastPublish = now;
        SensorReading r = sensorsRead();
        mqttPublish(r);  // logs success / failure / empty-skip internally
    }
}
