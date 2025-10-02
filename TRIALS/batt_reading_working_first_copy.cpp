#include <Arduino.h>

/*
  Battery monitor running as a FreeRTOS task on ESP32-S3 (Arduino core).
  - ADC pin reads through a divider (R_TOP / R_BOTTOM).
  - The monitor task samples the ADC, computes battery voltage and percent,
    and stores them into mutex-protected globals for other tasks to read.
  - loop() does nothing (it sleeps). Adjust sample interval with SAMPLE_MS.
*/

//
// --- CONFIG ---
//
const int PIN_BATT_ADC = 35; // <-- set to an ADC-capable pin on your board
const float R_TOP = 100000.0;   // ohms (top resistor of divider)
const float R_BOTTOM = 100000.0; // ohms (bottom resistor)
const int ADC_MAX = 4095; // 12-bit ADC
const float ADC_REF = 3.3; // nominal ADC reference (will calibrate)
float CALIB_FACTOR = 1.0; // set after calibration with a DMM

// sample and smoothing
const int NUM_SAMPLES = 20;
const TickType_t SAMPLE_MS = 1500; // sampling interval (ms)

//
// Lookup table (voltage -> percentage), descending voltages
//
const int TABLE_SIZE = 11;
float v_table[TABLE_SIZE] = {4.20, 4.05, 3.92, 3.86, 3.80, 3.75, 3.70, 3.65, 3.60, 3.55, 3.30};
int   p_table[TABLE_SIZE] = {100, 90, 80, 70, 60, 50, 40, 30, 20, 10, 0};

//
// --- Globals protected by mutex ---
//
static float g_lastVoltage = 0.0f;
static int   g_lastPercent = 0;
static SemaphoreHandle_t g_battMutex = NULL;

//
// Helper: convert read value -> battery voltage
//
static float adcToBatteryVoltage(float adc_raw) {
  // voltage at ADC pin
  float v_adc = (adc_raw / (float)ADC_MAX) * ADC_REF * CALIB_FACTOR;
  // convert to battery using divider
  float divider = (R_TOP + R_BOTTOM) / R_BOTTOM; // 2.0 for equal resistors
  return v_adc * divider;
}

//
// Helper: voltage -> percent (linear interpolation across table)
//
static int voltageToPercent(float v) {
  if (v >= v_table[0]) return 100;
  if (v <= v_table[TABLE_SIZE - 1]) return 0;
  for (int i = 0; i < TABLE_SIZE - 1; ++i) {
    if (v <= v_table[i] && v >= v_table[i+1]) {
      float v1 = v_table[i], v2 = v_table[i+1];
      int   p1 = p_table[i], p2 = p_table[i+1];
      float frac = (v - v2) / (v1 - v2); // 0..1
      int pct = round(p2 + frac * (p1 - p2));
      return pct;
    }
  }
  return 0;
}

//
// Task: battery monitor
//
void batteryMonitorTask(void *pvParameters) {
  (void) pvParameters;

  // ADC setup
  analogReadResolution(12); // 0..4095
  #if defined(ARDUINO_ARCH_ESP32)
  // allow more input range on ADC pin (this call exists on ESP32 cores)
  analogSetPinAttenuation(PIN_BATT_ADC, ADC_11db); // allows up to ~3.3V at pin
  #endif

  for (;;) {
    // sample average
    long sum = 0;
    for (int i = 0; i < NUM_SAMPLES; ++i) {
      sum += analogRead(PIN_BATT_ADC);
      // small delay to allow ADC to settle, keep short
      vTaskDelay(pdMS_TO_TICKS(5));
    }
    float adc_avg = (float)sum / (float)NUM_SAMPLES;

    // compute
    float vbat = adcToBatteryVoltage(adc_avg);
    int pct = voltageToPercent(vbat);

    // store under mutex
    if (g_battMutex) {
      if (xSemaphoreTake(g_battMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_lastVoltage = vbat;
        g_lastPercent = pct;
        xSemaphoreGive(g_battMutex);
      }
    }

    // debug print (safe inside task)
    Serial.print("Battery: ");
    Serial.print(vbat, 3);
    Serial.print(" V  -> ");
    Serial.print(pct);
    Serial.println(" %");

    // sleep until next sample
    vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
  }

  // never reaches here
  vTaskDelete(NULL);
}

//
// Public accessor for other tasks (thread-safe)
//
bool getBatteryStatus(float *outVoltage, int *outPercent) {
  if (!g_battMutex) return false;
  if (xSemaphoreTake(g_battMutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
  if (outVoltage) *outVoltage = g_lastVoltage;
  if (outPercent) *outPercent = g_lastPercent;
  xSemaphoreGive(g_battMutex);
  return true;
}

//
// ------------------- Arduino setup -------------------
//
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Starting battery monitor (RTOS task) ...");

  // create mutex
  g_battMutex = xSemaphoreCreateMutex();
  if (!g_battMutex) {
    Serial.println("ERROR: Could not create mutex");
    while (1) delay(1000);
  }

  // create the battery task
  const BaseType_t taskResult = xTaskCreatePinnedToCore(
    batteryMonitorTask,   // task function
    "BattMon",            // name
    4096,                 // stack (bytes)
    NULL,                 // parameter
    2,                    // priority
    NULL,                 // task handle
    1                     // pinned core (1 recommended on many boards; change if desired)
  );

  if (taskResult != pdPASS) {
    Serial.println("ERROR: Failed to create battery monitor task");
  }
}

//
// loop() intentionally does nothing (you requested no work in loop).
//
void loop() {
  // keep the Arduino main loop idle; give up CPU gracefully
  vTaskDelay(pdMS_TO_TICKS(1000));
}
