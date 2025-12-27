#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include <pas-co2-ino.hpp>        // Infineon XENSIV PAS CO2
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <SensirionI2cSps30.h>    // Sensirion I2C SPS30 (dependency: Sensirion Core)

// ---- Objects ----
PASCO2Ino co2(&Wire);
Adafruit_BME280 bme;
SensirionI2cSps30 sps;

// ---- Settings ----
static constexpr uint32_t BAUD        = 9600;
static constexpr uint32_t I2C_HZ      = 100000;   // requested
static constexpr uint32_t PRINT_MS    = 10000;     // print combined line every 10s
static constexpr uint32_t SPS_POLL_MS = 1000;     // poll SPS30 ready flag ~1 Hz

static constexpr uint8_t  BME_ADDR = 0x77;
static constexpr float    SEALEVELPRESSURE_HPA = 1013.25f;

// PAS pressure reference range (hPa)
static constexpr int PRESS_MIN_HPA = 750;
static constexpr int PRESS_MAX_HPA = 1150;

static uint16_t clamp_u16(int v, int lo, int hi) {
  if (v < lo) return (uint16_t)lo;
  if (v > hi) return (uint16_t)hi;
  return (uint16_t)v;
}

static uint16_t hpa_from_pa(float pa) {
  float hpa = pa / 100.0f;
  int rounded = (int)lroundf(hpa);              // PAS uses 1 hPa steps
  return clamp_u16(rounded, PRESS_MIN_HPA, PRESS_MAX_HPA);
}

// Simple exponential smoothing so we don't jitter by ±1 hPa constantly
static uint16_t smooth_press_hpa(uint16_t new_hpa) {
  static bool init = false;
  static float filt = 0.0f;                     // in hPa
  if (!init) { filt = (float)new_hpa; init = true; }
  filt = 0.8f * filt + 0.2f * (float)new_hpa;   // tune if you like
  return (uint16_t)lroundf(filt);
}

// ---- SPS30 state (latest known values) ----
static bool  spsHasData = false;
static float mc1p0=0, mc2p5=0, mc4p0=0, mc10p0=0;
static float nc0p5=0, nc1p0=0, nc2p5=0, nc4p0=0, nc10p0=0;
static float typicalParticleSize=0;

// Sensirion examples use NO_ERROR=0 and guard against macro conflicts.
#ifdef NO_ERROR
#undef NO_ERROR
#endif
#define NO_ERROR 0

static char spsErrMsg[64];

static uint32_t lastPrint = 0;
static uint32_t lastSpsPoll = 0;
static uint16_t lastPressRefSent_hPa = 0;

// Poll SPS30 ~1 Hz, update globals if new data is ready.
static void pollSps30() {
  uint16_t ready = 0;
  int16_t err = sps.readDataReadyFlag(ready);
  if (err != NO_ERROR) {
    Serial.print("SPS30 readDataReadyFlag() err=");
    Serial.println(err);
    return;
  }
  if (ready == 0) return;

  err = sps.readMeasurementValuesFloat(
    mc1p0, mc2p5, mc4p0, mc10p0,
    nc0p5, nc1p0, nc2p5, nc4p0, nc10p0,
    typicalParticleSize
  );
  if (err != NO_ERROR) {
    Serial.print("SPS30 readMeasurementValuesFloat() err=");
    Serial.println(err);
    return;
  }

  spsHasData = true;
}

void setup() {
  Serial.begin(BAUD);
  delay(200);

  Wire.begin();
  Wire.setClock(I2C_HZ);

  Serial.println();
  Serial.println("Starting PAS CO2 (0x28) + BME280 (0x77) + SPS30 (0x69) ...");

  // --- BME280 init ---
  if (!bme.begin(BME_ADDR, &Wire)) {
    Serial.println("ERROR: BME280 begin() failed. Check wiring/address (0x77).");
    while (true) delay(1000);
  }
  Serial.println("BME280 OK");

  // --- PAS CO2 init ---
  int32_t err32 = co2.begin();
  if (err32 != XENSIV_PASCO2_OK) {
    Serial.print("ERROR: PAS CO2 begin() failed, err=");
    Serial.println(err32);
    while (true) delay(1000);
  }
  Serial.println("PAS CO2 OK");

  // Seed pressure reference BEFORE starting measurements
  float pPa0 = bme.readPressure();
  uint16_t pressRef0 = smooth_press_hpa(hpa_from_pa(pPa0));
  err32 = co2.setPressRef(pressRef0);
  if (err32 == XENSIV_PASCO2_OK) {
    lastPressRefSent_hPa = pressRef0;
    Serial.print("Initial PAS pressure ref set to ");
    Serial.print(lastPressRefSent_hPa);
    Serial.println(" hPa");
  } else {
    Serial.print("WARNING: PAS setPressRef failed, err=");
    Serial.println(err32);
  }

  // Start PAS continuous measurements every 5 seconds
  err32 = co2.startMeasure(5);
  if (err32 != XENSIV_PASCO2_OK) {
    Serial.print("ERROR: PAS CO2 startMeasure(5) failed, err=");
    Serial.println(err32);
    while (true) delay(1000);
  }

  // --- SPS30 init ---
  sps.begin(Wire, SPS30_I2C_ADDR_69);
  sps.stopMeasurement(); // safe to call even if not running

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

  Serial.println("----");
  Serial.println("Format:");
  Serial.println("CO2ppm | T_C | RH_% | P_hPa(Pa) | Alt_m | PM(mc ug/m3) | NC(#/cm3) | TPS_um");
  Serial.println("----");

  lastPrint = millis();
  lastSpsPoll = millis();
}

void loop() {
  const uint32_t now = millis();

  // 1) Keep SPS30 updated in the background
  if (now - lastSpsPoll >= SPS_POLL_MS) {
    lastSpsPoll += SPS_POLL_MS;
    pollSps30();
  }

  // 2) Print combined line every 5 seconds
  if (now - lastPrint < PRINT_MS) return;
  lastPrint += PRINT_MS;

  // Read CO2 (this sample was compensated using the *previous* pressure ref)
  int16_t co2ppm = 0;
  int32_t co2err = XENSIV_PASCO2_OK;

  const uint32_t t0 = millis();
  while (co2ppm == 0 && (millis() - t0) < 1500) {
    co2err = co2.getCO2(co2ppm);
    if (co2err != XENSIV_PASCO2_OK) break;
    if (co2ppm == 0) delay(50);
  }

  // Read BME280
  const float tempC       = bme.readTemperature();
  const float humidity    = bme.readHumidity();
  const float pressurePa  = bme.readPressure();
  const float pressurehPa = pressurePa / 100.0f;
  const float altitudeM   = bme.readAltitude(SEALEVELPRESSURE_HPA);

  // Compute next pressure ref for PAS (applied to *next* interval)
  const uint16_t nextPressRef = smooth_press_hpa(hpa_from_pa(pressurePa));

  // Print line
  Serial.print("CO2=");
  if (co2err == XENSIV_PASCO2_OK && co2ppm > 0) Serial.print(co2ppm);
  else Serial.print("NA");

  Serial.print(" | T=");  Serial.print(tempC, 2);
  Serial.print(" | RH="); Serial.print(humidity, 2);
  Serial.print(" | P=");  Serial.print(pressurehPa, 2);
  Serial.print(" (");      Serial.print(pressurePa, 0);
  Serial.print(") | Alt=");Serial.print(altitudeM, 2);

  Serial.print(" | MC[ug/m3]=");
  if (spsHasData) {
    Serial.print(mc1p0, 1); Serial.print(",");
    Serial.print(mc2p5, 1); Serial.print(",");
    Serial.print(mc4p0, 1); Serial.print(",");
    Serial.print(mc10p0, 1);
  } else {
    Serial.print("NA");
  }

  Serial.print(" | NC[#/cm3]=");
  if (spsHasData) {
    Serial.print(nc0p5, 1); Serial.print(",");
    Serial.print(nc1p0, 1); Serial.print(",");
    Serial.print(nc2p5, 1); Serial.print(",");
    Serial.print(nc4p0, 1); Serial.print(",");
    Serial.print(nc10p0, 1);
  } else {
    Serial.print("NA");
  }

  Serial.print(" | TPS_um=");
  if (spsHasData) Serial.print(typicalParticleSize, 3);
  else Serial.print("NA");

  Serial.print(" | PAS_Pref(next)=");
  Serial.print(nextPressRef);
  Serial.println(" hPa");

  // Update PAS pressure reference only if changed
  if (nextPressRef != lastPressRefSent_hPa) {
    int32_t e2 = co2.setPressRef(nextPressRef);
    if (e2 == XENSIV_PASCO2_OK) {
      lastPressRefSent_hPa = nextPressRef;
    } else {
      Serial.print("WARNING: PAS setPressRef failed, err=");
      Serial.println(e2);
    }
  }
}
