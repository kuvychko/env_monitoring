/*
 * secrets.example.h
 *
 * Copy this file to secrets.h and fill in your credentials.
 * secrets.h is gitignored and will not be committed.
 */

#ifndef SECRETS_H
#define SECRETS_H

// Wi-Fi credentials
#define WIFI_SSID "your_wifi_ssid"
#define WIFI_PASS "your_wifi_password"

// MQTT broker
#define MQTT_HOST "your.mqtt.broker.ip"  // IP address or hostname of your MQTT broker
#define MQTT_PORT 1883

// Device ID (optional override - defaults to "nanoesp32_office" in main sketch)
// #define DEVICE_ID "nanoesp32_bedroom"

#endif // SECRETS_H
