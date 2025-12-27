/*
 * nano-esp32-iaq.ino
 *
 * Indoor Air Quality sensor firmware for Arduino Nano ESP32
 * Reads: PAS CO2, BME280 (temp/humidity/pressure), SPS30 (particulate matter)
 * Publishes: MQTT JSON telemetry every 60 seconds
 *
 * Required libraries:
 *   - pas-co2-ino (Infineon XENSIV PAS CO2)
 *   - Adafruit BME280 Library
 *   - Sensirion I2C SPS30 (+ Sensirion Core)
 *   - PubSubClient (MQTT)
 */

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <WiFi.h>
#include <time.h>
#include <PubSubClient.h>

#include <pas-co2-ino.hpp>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <SensirionI2cSps30.h>

#include "secrets.h"

// ---- SPS30 reading struct (must be before function prototypes) ----
typedef struct {
  float mc1p0, mc2p5, mc4p0, mc10p0;
  float nc0p5, nc1p0, nc2p5, nc4p0, nc10p0;
  float typicalSize;
} SpsReading;

// ---- Device Configuration ----
#ifndef DEVICE_ID
#define DEVICE_ID "nanoesp32_office"
#endif

// ---- Objects ----
PASCO2Ino co2(&Wire);
Adafruit_BME280 bme;
SensirionI2cSps30 sps;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// ---- Settings ----
static constexpr uint32_t BAUD           = 115200;
static constexpr uint32_t I2C_HZ         = 100000;   // limited by SPS30
static constexpr uint32_t PUBLISH_MS     = 60000;    // publish telemetry every 60s
static constexpr uint32_t SPS_POLL_MS    = 1000;     // poll SPS30 ready flag ~1 Hz
static constexpr uint32_t CO2_POLL_MS    = 15000;    // poll CO2 sensor every 15s
static constexpr uint32_t PRESS_UPDATE_MS = 30000;   // update pressure reference every 30s
static constexpr uint32_t WIFI_RETRY_MS  = 5000;     // Wi-Fi reconnect interval
static constexpr uint32_t MQTT_RETRY_MS  = 5000;     // MQTT reconnect interval

static constexpr uint8_t CO2_FAIL_THRESHOLD = 3;     // soft reset after 3 consecutive failures

static constexpr uint8_t  BME_ADDR = 0x77;

// PAS pressure reference range (hPa)
static constexpr int PRESS_MIN_HPA = 750;
static constexpr int PRESS_MAX_HPA = 1150;

// NTP settings
static constexpr const char* NTP_SERVER = "pool.ntp.org";
static constexpr long GMT_OFFSET_SEC    = 0;         // UTC
static constexpr int  DAYLIGHT_OFFSET   = 0;

// MQTT topics
static char topicTelemetry[64];
static char topicStatus[64];

// ---- Helper functions ----
static uint16_t clamp_u16(int v, int lo, int hi) {
  if (v < lo) return (uint16_t)lo;
  if (v > hi) return (uint16_t)hi;
  return (uint16_t)v;
}

static uint16_t hpa_from_pa(float pa) {
  float hpa = pa / 100.0f;
  int rounded = (int)lroundf(hpa);
  return clamp_u16(rounded, PRESS_MIN_HPA, PRESS_MAX_HPA);
}

static uint16_t smooth_press_hpa(uint16_t new_hpa) {
  static bool init = false;
  static float filt = 0.0f;
  if (!init) { filt = (float)new_hpa; init = true; }
  filt = 0.8f * filt + 0.2f * (float)new_hpa;
  return (uint16_t)lroundf(filt);
}

// ---- SPS30 ring buffer for averaging ----
static constexpr uint8_t SPS_BUF_SIZE = 10;  // average over 10 readings (~10s at 1Hz)
static SpsReading spsBuf[SPS_BUF_SIZE];
static uint8_t spsBufHead = 0;      // next write position
static uint8_t spsBufCount = 0;     // number of valid samples (0 to SPS_BUF_SIZE)

// Compute average of SPS30 readings in buffer
static bool getSpsAverage(SpsReading& avg) {
  if (spsBufCount == 0) return false;

  avg = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  for (uint8_t i = 0; i < spsBufCount; i++) {
    const SpsReading& r = spsBuf[i];
    avg.mc1p0 += r.mc1p0;
    avg.mc2p5 += r.mc2p5;
    avg.mc4p0 += r.mc4p0;
    avg.mc10p0 += r.mc10p0;
    avg.nc0p5 += r.nc0p5;
    avg.nc1p0 += r.nc1p0;
    avg.nc2p5 += r.nc2p5;
    avg.nc4p0 += r.nc4p0;
    avg.nc10p0 += r.nc10p0;
    avg.typicalSize += r.typicalSize;
  }
  float n = (float)spsBufCount;
  avg.mc1p0 /= n;
  avg.mc2p5 /= n;
  avg.mc4p0 /= n;
  avg.mc10p0 /= n;
  avg.nc0p5 /= n;
  avg.nc1p0 /= n;
  avg.nc2p5 /= n;
  avg.nc4p0 /= n;
  avg.nc10p0 /= n;
  avg.typicalSize /= n;
  return true;
}

#ifdef NO_ERROR
#undef NO_ERROR
#endif
#define NO_ERROR 0

// ---- Timing state ----
static uint32_t lastPublish = 0;
static uint32_t lastSpsPoll = 0;
static uint32_t lastCo2Poll = 0;
static uint32_t lastPressUpdate = 0;
static uint32_t lastWifiCheck = 0;
static uint32_t lastMqttCheck = 0;
static uint16_t lastPressRefSent_hPa = 0;

// ---- CO2 sensor state ----
static int16_t lastValidCo2 = 0;         // last successful CO2 reading
static uint32_t lastCo2ReadTime = 0;     // millis() when last valid reading was obtained
static uint8_t co2FailCount = 0;         // consecutive failure count
static uint16_t co2ResetCount = 0;       // total soft resets performed

// ---- Wi-Fi functions ----
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("Connected! IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println();
    Serial.println("WiFi connection failed, will retry...");
  }
}

// ---- NTP functions ----
void syncTime() {
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET, NTP_SERVER);
  Serial.print("Waiting for NTP sync");

  time_t now = time(nullptr);
  uint32_t startAttempt = millis();
  while (now < 1700000000 && millis() - startAttempt < 10000) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }

  if (now >= 1700000000) {
    Serial.println();
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    Serial.print("NTP synced: ");
    Serial.println(asctime(&timeinfo));
  } else {
    Serial.println();
    Serial.println("NTP sync failed, timestamps may be incorrect");
  }
}

// ---- MQTT functions ----
void connectMQTT() {
  if (mqtt.connected()) return;
  if (WiFi.status() != WL_CONNECTED) return;

  Serial.print("Connecting to MQTT: ");
  Serial.print(MQTT_HOST);
  Serial.print(":");
  Serial.println(MQTT_PORT);

  // Build LWT message
  char lwt[64];
  snprintf(lwt, sizeof(lwt), "{\"device_id\":\"%s\",\"status\":\"offline\"}", DEVICE_ID);

  if (mqtt.connect(DEVICE_ID, NULL, NULL, topicStatus, 1, true, lwt)) {
    Serial.println("MQTT connected!");

    // Publish online status
    char online[64];
    snprintf(online, sizeof(online), "{\"device_id\":\"%s\",\"status\":\"online\"}", DEVICE_ID);
    mqtt.publish(topicStatus, online, true);
  } else {
    Serial.print("MQTT connection failed, rc=");
    Serial.println(mqtt.state());
  }
}

// ---- SPS30 polling ----
static void pollSps30() {
  uint16_t ready = 0;
  int16_t err = sps.readDataReadyFlag(ready);
  if (err != NO_ERROR) {
    Serial.print("SPS30 readDataReadyFlag() err=");
    Serial.println(err);
    spsBufCount = 0;  // invalidate buffer on error
    return;
  }
  if (ready == 0) return;

  SpsReading r;
  err = sps.readMeasurementValuesFloat(
    r.mc1p0, r.mc2p5, r.mc4p0, r.mc10p0,
    r.nc0p5, r.nc1p0, r.nc2p5, r.nc4p0, r.nc10p0,
    r.typicalSize
  );
  if (err != NO_ERROR) {
    Serial.print("SPS30 readMeasurementValuesFloat() err=");
    Serial.println(err);
    spsBufCount = 0;  // invalidate buffer on error
    return;
  }

  // Add to ring buffer
  spsBuf[spsBufHead] = r;
  spsBufHead = (spsBufHead + 1) % SPS_BUF_SIZE;
  if (spsBufCount < SPS_BUF_SIZE) {
    spsBufCount++;
  }
}

// ---- CO2 polling with soft reset recovery ----
static void pollCo2() {
  int16_t reading = 0;
  int32_t err = co2.getCO2(reading);

  if (err == XENSIV_PASCO2_OK && reading > 0) {
    // Success - update cached value and reset fail count
    lastValidCo2 = reading;
    lastCo2ReadTime = millis();
    co2FailCount = 0;
  } else {
    // Failure - increment count
    co2FailCount++;
    Serial.print("CO2 read failed: err=");
    Serial.print(err);
    Serial.print(", reading=");
    Serial.print(reading);
    Serial.print(", failCount=");
    Serial.println(co2FailCount);

    // Recovery after threshold consecutive failures
    if (co2FailCount >= CO2_FAIL_THRESHOLD) {
      Serial.println("CO2 sensor: attempting recovery...");

      // Reinitialize the sensor
      int32_t initErr = co2.begin();
      if (initErr != XENSIV_PASCO2_OK) {
        Serial.print("CO2 sensor: begin() failed, err=");
        Serial.println(initErr);
      }
      delay(100);

      // Restart continuous measurements
      int32_t startErr = co2.startMeasure(10);
      if (startErr == XENSIV_PASCO2_OK) {
        Serial.println("CO2 sensor: recovery successful, measurement restarted");
      } else {
        Serial.print("CO2 sensor: startMeasure failed after recovery, err=");
        Serial.println(startErr);
      }

      co2FailCount = 0;
      co2ResetCount++;
    }
  }
}

// ---- Pressure reference update (separate from CO2 read) ----
static void updatePressureRef() {
  float pressurePa = bme.readPressure();
  if (isnan(pressurePa)) return;

  const uint16_t nextPressRef = smooth_press_hpa(hpa_from_pa(pressurePa));
  if (nextPressRef != lastPressRefSent_hPa) {
    int32_t err = co2.setPressRef(nextPressRef);
    if (err == XENSIV_PASCO2_OK) {
      lastPressRefSent_hPa = nextPressRef;
    }
  }
}

// ---- ISO8601 timestamp ----
void getTimestamp(char* buf, size_t len) {
  time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
}

// ---- Build and publish telemetry ----
void publishTelemetry() {
  if (!mqtt.connected()) return;

  // Use cached CO2 value (updated by pollCo2)
  // If reading is stale (>60s old), report -1
  const uint32_t now = millis();
  const bool co2Valid = (lastValidCo2 > 0) && ((now - lastCo2ReadTime) < 60000);

  // Read BME280 with NaN checks
  float tempC       = bme.readTemperature();
  float humidity    = bme.readHumidity();
  float pressurePa  = bme.readPressure();
  float pressurehPa = pressurePa / 100.0f;

  const bool bmeOk = !isnan(tempC) && !isnan(humidity) && !isnan(pressurePa);
  if (!bmeOk) {
    Serial.println("BME280 read error (NaN)");
    tempC = -999.0f;
    humidity = -1.0f;
    pressurehPa = -1.0f;
  }

  // Get averaged SPS30 values
  SpsReading spsAvg;
  const bool spsOk = getSpsAverage(spsAvg);

  // Build timestamp
  char ts[32];
  getTimestamp(ts, sizeof(ts));

  // Calculate CO2 reading age in seconds
  const uint32_t co2AgeS = (now - lastCo2ReadTime) / 1000;

  // Build JSON payload (with diagnostic fields)
  char payload[896];
  int len = snprintf(payload, sizeof(payload),
    "{"
    "\"device_id\":\"%s\","
    "\"ts\":\"%s\","
    "\"co2_ppm\":%d,"
    "\"temp_c\":%.2f,"
    "\"rh_pct\":%.1f,"
    "\"pressure_hpa\":%.1f,"
    "\"pm1_0_ugm3\":%.1f,"
    "\"pm2_5_ugm3\":%.1f,"
    "\"pm4_0_ugm3\":%.1f,"
    "\"pm10_ugm3\":%.1f,"
    "\"nc0_5_pcm3\":%.1f,"
    "\"nc1_0_pcm3\":%.1f,"
    "\"nc2_5_pcm3\":%.1f,"
    "\"nc4_0_pcm3\":%.1f,"
    "\"nc10_pcm3\":%.1f,"
    "\"typical_size_um\":%.2f,"
    "\"rssi_dbm\":%d,"
    "\"uptime_s\":%lu,"
    "\"co2_age_s\":%lu,"
    "\"co2_resets\":%u"
    "}",
    DEVICE_ID,
    ts,
    co2Valid ? lastValidCo2 : -1,
    tempC,
    humidity,
    pressurehPa,
    spsOk ? spsAvg.mc1p0 : -1.0f,
    spsOk ? spsAvg.mc2p5 : -1.0f,
    spsOk ? spsAvg.mc4p0 : -1.0f,
    spsOk ? spsAvg.mc10p0 : -1.0f,
    spsOk ? spsAvg.nc0p5 : -1.0f,
    spsOk ? spsAvg.nc1p0 : -1.0f,
    spsOk ? spsAvg.nc2p5 : -1.0f,
    spsOk ? spsAvg.nc4p0 : -1.0f,
    spsOk ? spsAvg.nc10p0 : -1.0f,
    spsOk ? spsAvg.typicalSize : -1.0f,
    WiFi.RSSI(),
    now / 1000,
    co2AgeS,
    co2ResetCount
  );

  // Publish to MQTT
  if (mqtt.publish(topicTelemetry, payload)) {
    Serial.print("Published: ");
    Serial.println(payload);
  } else {
    Serial.println("MQTT publish failed!");
  }
}

void setup() {
  Serial.begin(BAUD);
  delay(200);

  Serial.println();
  Serial.println("========================================");
  Serial.println("Indoor Air Quality Monitor - " DEVICE_ID);
  Serial.println("========================================");

  // Build MQTT topics
  snprintf(topicTelemetry, sizeof(topicTelemetry), "iaq/%s/telemetry", DEVICE_ID);
  snprintf(topicStatus, sizeof(topicStatus), "iaq/%s/status", DEVICE_ID);
  Serial.print("Telemetry topic: ");
  Serial.println(topicTelemetry);

  // Initialize I2C
  Wire.begin();
  Wire.setClock(I2C_HZ);

  // --- BME280 init ---
  Serial.println("Initializing BME280...");
  if (!bme.begin(BME_ADDR, &Wire)) {
    Serial.println("ERROR: BME280 begin() failed. Check wiring/address (0x77).");
    while (true) delay(1000);
  }
  Serial.println("BME280 OK");

  // --- PAS CO2 init ---
  Serial.println("Initializing PAS CO2...");
  int32_t err32 = co2.begin();
  if (err32 != XENSIV_PASCO2_OK) {
    Serial.print("ERROR: PAS CO2 begin() failed, err=");
    Serial.println(err32);
    while (true) delay(1000);
  }
  Serial.println("PAS CO2 OK");

  // Seed pressure reference
  float pPa0 = bme.readPressure();
  uint16_t pressRef0 = smooth_press_hpa(hpa_from_pa(pPa0));
  err32 = co2.setPressRef(pressRef0);
  if (err32 == XENSIV_PASCO2_OK) {
    lastPressRefSent_hPa = pressRef0;
    Serial.print("Initial pressure ref: ");
    Serial.print(lastPressRefSent_hPa);
    Serial.println(" hPa");
  }

  // Start PAS continuous measurements every 10 seconds
  err32 = co2.startMeasure(10);
  if (err32 != XENSIV_PASCO2_OK) {
    Serial.print("ERROR: PAS CO2 startMeasure(10) failed, err=");
    Serial.println(err32);
    while (true) delay(1000);
  }

  // --- SPS30 init ---
  Serial.println("Initializing SPS30...");
  sps.begin(Wire, SPS30_I2C_ADDR_69);
  sps.stopMeasurement();

  int8_t sn[32] = {0};
  int8_t pt[8]  = {0};
  (void)sps.readSerialNumber(sn, 32);
  (void)sps.readProductType(pt, 8);
  Serial.print("SPS30 serial: "); Serial.println((const char*)sn);
  Serial.print("SPS30 type:   "); Serial.println((const char*)pt);

  int16_t err16 = sps.startMeasurement(SPS30_OUTPUT_FORMAT_OUTPUT_FORMAT_FLOAT);
  if (err16 != NO_ERROR) {
    Serial.print("ERROR: SPS30 startMeasurement() failed, err=");
    Serial.println(err16);
    while (true) delay(1000);
  }
  Serial.println("SPS30 OK");

  // --- WiFi ---
  connectWiFi();

  // --- NTP ---
  if (WiFi.status() == WL_CONNECTED) {
    syncTime();
  }

  // --- MQTT ---
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(1024);  // increased for diagnostic fields
  connectMQTT();

  Serial.println("----");
  Serial.println("Setup complete. Publishing telemetry every 60s.");
  Serial.println("----");

  const uint32_t now = millis();
  lastPublish = now;
  lastSpsPoll = now;
  lastCo2Poll = now;
  lastPressUpdate = now;
}

void loop() {
  const uint32_t now = millis();

  // 1) Maintain WiFi connection
  if (now - lastWifiCheck >= WIFI_RETRY_MS) {
    lastWifiCheck = now;
    if (WiFi.status() != WL_CONNECTED) {
      connectWiFi();
      if (WiFi.status() == WL_CONNECTED) {
        syncTime();
      }
    }
  }

  // 2) Maintain MQTT connection
  if (now - lastMqttCheck >= MQTT_RETRY_MS) {
    lastMqttCheck = now;
    connectMQTT();
  }
  mqtt.loop();

  // 3) Poll SPS30 ~1 Hz
  if (now - lastSpsPoll >= SPS_POLL_MS) {
    lastSpsPoll += SPS_POLL_MS;
    pollSps30();
  }

  // 4) Poll CO2 every 15s (with soft reset recovery)
  if (now - lastCo2Poll >= CO2_POLL_MS) {
    lastCo2Poll += CO2_POLL_MS;
    pollCo2();
  }

  // 5) Update pressure reference every 30s (separate from CO2 read)
  if (now - lastPressUpdate >= PRESS_UPDATE_MS) {
    lastPressUpdate += PRESS_UPDATE_MS;
    updatePressureRef();
  }

  // 6) Publish telemetry every 60s
  if (now - lastPublish >= PUBLISH_MS) {
    lastPublish += PUBLISH_MS;
    publishTelemetry();
  }
}
