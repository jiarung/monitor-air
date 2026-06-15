#pragma once

#include "sensors.h"

// Configure the MQTT client and validate config. Does not connect (non-blocking).
void mqttSetup();

// Pump the client and run rate-limited, non-blocking reconnection. Call every
// loop iteration so keepalive/PINGREQ stays alive between publishes.
void mqttLoop();

// True when the client currently has a live broker connection.
bool mqttConnected();

// Publish one reading as on-contract JSON to monitor-air/<device>/telemetry
// (QoS 0, not retained). Returns true if the publish was accepted by the client.
bool mqttPublish(const SensorReading& r);
