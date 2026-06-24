#include "mqtt_client.h"

#include <string.h>

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

#include "secrets.h"
#include "log.h"

namespace {

// PubSubClient's buffer holds the whole MQTT packet (CONNECT included: client id
// + user + pass + headers), not just the publish payload — keep it roomy.
constexpr uint16_t MQTT_BUFFER_SIZE   = 512;
constexpr uint16_t MQTT_KEEPALIVE_S   = 60;
constexpr uint16_t MQTT_SOCKET_TMO_S  = 5;   // bound how long a blocking connect() can stall
constexpr uint32_t RECONNECT_EVERY_MS = 5000;
constexpr size_t   MAX_ID_LEN         = 30;  // keeps CONNECT packet well within the buffer
constexpr size_t   MAX_CRED_LEN       = 64;  // clientId(~45)+user+pass+headers must fit MQTT_BUFFER_SIZE

WiFiClient espClient;
PubSubClient client(espClient);

char topic[64] = {0};
char clientId[48] = {0};
bool configValid = false;
uint32_t lastReconnectAttempt = 0;

// Device id must be a single safe topic segment: server-side topic_parsing splits
// on '/', and '+'/'#' are MQTT wildcards — any of those would break the contract.
bool isValidDeviceId(const char* s) {
    if (s == nullptr) return false;
    size_t n = strlen(s);
    if (n == 0 || n > MAX_ID_LEN) return false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

bool tryConnect() {
    bool ok;
    if (MQTT_USER != nullptr && MQTT_USER[0] != '\0') {
        ok = client.connect(clientId, MQTT_USER, MQTT_PASS);
    } else {
        ok = client.connect(clientId);
    }
    if (ok) {
        logf("[mqtt] connected as %s -> %s:%u\n", clientId, MQTT_HOST, MQTT_PORT);
    } else {
        logf("[mqtt] connect failed, state=%d (retry in %us)\n",
                      client.state(), (unsigned)(RECONNECT_EVERY_MS / 1000));
    }
    return ok;
}

}  // namespace

void mqttSetup() {
    if (!isValidDeviceId(MQTT_DEVICE_ID)) {
        logf("[mqtt] FATAL: invalid MQTT_DEVICE_ID \"%s\" — must be 1-%u of [A-Za-z0-9_-]. "
                      "MQTT disabled.\n", MQTT_DEVICE_ID ? MQTT_DEVICE_ID : "(null)", (unsigned)MAX_ID_LEN);
        configValid = false;
        return;
    }
    if ((MQTT_USER != nullptr && strlen(MQTT_USER) > MAX_CRED_LEN) ||
        (MQTT_PASS != nullptr && strlen(MQTT_PASS) > MAX_CRED_LEN)) {
        logf("[mqtt] FATAL: MQTT_USER/MQTT_PASS exceed %u chars (CONNECT packet would "
                      "overflow the %u-byte buffer). MQTT disabled.\n",
                      (unsigned)MAX_CRED_LEN, (unsigned)MQTT_BUFFER_SIZE);
        configValid = false;
        return;
    }
    snprintf(topic, sizeof(topic), "monitor-air/%s/telemetry", MQTT_DEVICE_ID);
    snprintf(clientId, sizeof(clientId), "monitor-air-%s", MQTT_DEVICE_ID);
    configValid = true;

    client.setServer(MQTT_HOST, MQTT_PORT);
    client.setBufferSize(MQTT_BUFFER_SIZE);
    client.setKeepAlive(MQTT_KEEPALIVE_S);
    client.setSocketTimeout(MQTT_SOCKET_TMO_S);
    logf("[mqtt] topic=%s\n", topic);
}

void mqttLoop() {
    if (!configValid) return;
    // connect() is blocking; only attempt once WiFi is up, and rate-limit retries.
    if (WiFi.status() != WL_CONNECTED) return;

    if (client.connected()) {
        client.loop();  // keeps keepalive/PINGREQ alive between publishes
        return;
    }

    // connect() is synchronous (bounded by setSocketTimeout); rate-limit retries.
    uint32_t now = millis();
    if (lastReconnectAttempt != 0 && now - lastReconnectAttempt < RECONNECT_EVERY_MS) return;
    lastReconnectAttempt = now;
    tryConnect();
}

bool mqttConnected() {
    return configValid && client.connected();
}

bool mqttPublish(const SensorReading& r) {
    if (!mqttConnected()) return false;

    char payload[200];
    const size_t cap = sizeof(payload);
    size_t n = 0;
    bool first = true;

    // Bounds-checked append: snprintf returns the length it WOULD write, so on
    // truncation n must not advance past cap (that would underflow cap-n and the
    // next write would be out of bounds). Returns false to abort the whole publish.
    auto append = [&](const char* fmt, const char* key, float v) -> bool {
        int ret = snprintf(payload + n, cap - n, fmt, key, v);
        if (ret < 0 || (size_t)ret >= cap - n) return false;
        n += (size_t)ret;
        return true;
    };
    auto addField = [&](const char* key, float v, bool valid) -> bool {
        // re-check finiteness so a NaN/Inf never reaches the wire as bad JSON
        if (!valid || v != v || v >= 3.4e38f || v <= -3.4e38f) return true;  // skip, not an error
        if (!append(first ? "\"%s\":%.1f" : ",\"%s\":%.1f", key, v)) return false;
        first = false;
        return true;
    };

    payload[n++] = '{';
    bool built = addField("temp", r.temp, r.tempValid) &&
                 addField("hum", r.hum, r.humValid) &&
                 addField("pressure", r.pressure, r.pressureValid) &&
                 addField("gas", r.gas, r.gasValid) &&
                 addField("lux", r.lux, r.luxValid);
    if (!built || n + 2 > cap) {  // +2: closing '}' and NUL
        logln("[mqtt] publish aborted: payload overflow");
        return false;
    }
    payload[n++] = '}';
    payload[n] = '\0';

    if (first) {  // no valid fields — nothing worth sending
        logln("[mqtt] skip publish: no valid sensor fields");
        return false;
    }

    bool ok = client.publish(topic, payload, false);  // QoS 0, not retained
    if (ok) {
        logf("[mqtt] published %s %s\n", topic, payload);
    } else {
        logf("[mqtt] publish FAILED: state=%d connected=%d wifi=%d topicLen=%u payloadLen=%u\n",
                      client.state(), client.connected(), WiFi.status(),
                      (unsigned)strlen(topic), (unsigned)strlen(payload));
    }
    return ok;
}
