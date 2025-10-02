/* 
  Modular Battery Monitor (ESP32-S3, Arduino core)
  - FreeRTOS task based
  - Median + averaging + EMA smoothing for stable readings
  - Runtime configuration via Serial commands, persisted with Preferences
  - No hard-coded calibration: use the CAL command with your DMM reading
  - Provides getBatteryStatus() accessor for other tasks
*/

#include <Arduino.h>
#include <Preferences.h>

// ---------------- CONFIG DEFAULTS ----------------
#define DEFAULT_ADC_PIN        35        // default ADC pin (change if you like)
#define DEFAULT_R_TOP         100000.0f  // top resistor of divider (ohms)
#define DEFAULT_R_BOTTOM      100000.0f  // bottom resistor of divider (ohms)
#define DEFAULT_SAMPLES       20         // number of raw ADC samples for median window
#define DEFAULT_SAMPLE_DELAY  5          // ms between raw samples while building median window
#define DEFAULT_INTERVAL_MS   1500       // monitor interval in ms
#define DEFAULT_EMA_ALPHA     0.2f       // smoothing Alpha for EMA (0..1)
#define DEFAULT_ADC_MAX       4095.0f    // 12-bit ADC
#define DEFAULT_ADC_REF       3.3f       // nominal ADC reference before calibration
#define PREF_NAMESPACE        "batmonv1" // preferences namespace in flash

// ----------------- MODULE / STATE -----------------
class BatteryMonitor {
public:
  // public settings (modifiable at runtime)
  int   pin_adc;
  float r_top;
  float r_bottom;
  int   num_samples;
  int   sample_delay_ms;
  int   interval_ms;
  float ema_alpha;
  float adc_ref;      // nominal reference, used before calibration
  float calib_factor; // multiplier computed by calibration (default 1.0)

  // optional: STAT pin for charge detect (TP4056 STAT -> active low)
  int   pin_stat;     // -1 = unused

  // ctor
  BatteryMonitor()
  : pin_adc(DEFAULT_ADC_PIN),
    r_top(DEFAULT_R_TOP),
    r_bottom(DEFAULT_R_BOTTOM),
    num_samples(DEFAULT_SAMPLES),
    sample_delay_ms(DEFAULT_SAMPLE_DELAY),
    interval_ms(DEFAULT_INTERVAL_MS),
    ema_alpha(DEFAULT_EMA_ALPHA),
    adc_ref(DEFAULT_ADC_REF),
    calib_factor(1.0f),
    pin_stat(-1),
    _ema_voltage(0.0f),
    _last_percent(0),
    _prefs(),
    _mutex(NULL),
    _task_handle(NULL)
  {}

  // begin: restore settings from preferences and start RTOS task
  bool begin() {
    // create mutex
    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) {
      Serial.println("ERROR: could not create batt mutex");
      return false;
    }

    // load prefs
    _prefs.begin(PREF_NAMESPACE, false);
    pin_adc      = _prefs.getInt("pin_adc", pin_adc);
    r_top        = _prefs.getFloat("r_top", r_top);
    r_bottom     = _prefs.getFloat("r_bottom", r_bottom);
    num_samples  = _prefs.getInt("num_samp", num_samples);
    sample_delay_ms = _prefs.getInt("sdel", sample_delay_ms);
    interval_ms  = _prefs.getInt("int_ms", interval_ms);
    ema_alpha    = _prefs.getFloat("alpha", ema_alpha);
    adc_ref      = _prefs.getFloat("adc_ref", adc_ref);
    calib_factor = _prefs.getFloat("calib", calib_factor);
    pin_stat     = _prefs.getInt("pin_stat", pin_stat);

    // ADC config
    analogReadResolution(12);
    #if defined(ARDUINO_ARCH_ESP32)
    analogSetPinAttenuation(pin_adc, ADC_11db);
    #endif

    // setup stat pin if used
    if (pin_stat >= 0) {
      pinMode(pin_stat, INPUT_PULLUP); // TP4056 STAT is often active low
    }

    // create monitor task pinned to core 1
    BaseType_t r = xTaskCreatePinnedToCore(
      _taskFuncStatic, "BattMon", 4096, this, 2, &_task_handle, 1
    );
    if (r != pdPASS) {
      Serial.println("ERROR: creating BattMon task failed");
      return false;
    }

    Serial.println("BatteryMonitor started.");
    return true;
  }

  // Save current settings to non-volatile Preferences
  void saveSettings() {
    _prefs.putInt("pin_adc", pin_adc);
    _prefs.putFloat("r_top", r_top);
    _prefs.putFloat("r_bottom", r_bottom);
    _prefs.putInt("num_samp", num_samples);
    _prefs.putInt("sdel", sample_delay_ms);
    _prefs.putInt("int_ms", interval_ms);
    _prefs.putFloat("alpha", ema_alpha);
    _prefs.putFloat("adc_ref", adc_ref);
    _prefs.putFloat("calib", calib_factor);
    _prefs.putInt("pin_stat", pin_stat);
    Serial.println("Settings SAVED to prefs.");
  }

  // Expose latest values thread-safely
  bool getBatteryStatus(float *outVoltage, int *outPercent, bool *outCharging = nullptr) {
    if (!_mutex) return false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    if (outVoltage) *outVoltage = _ema_voltage;
    if (outPercent) *outPercent = _last_percent;
    if (outCharging && pin_stat >= 0) *outCharging = (digitalRead(pin_stat) == LOW); // TP4056 STAT active low
    xSemaphoreGive(_mutex);
    return true;
  }

  // Manual calibration helper: supply measured real battery voltage (from DMM)
  // The routine computes a calib_factor so that reported V matches measured value.
  void calibrateUsingMeasuredVoltage(float measuredVoltage) {
    // take a single reading AVG
    float raw_adc = _sampleMedianRaw();
    float measuredReport = _adcRawToBatteryVoltage(raw_adc); // pre-calib
    if (measuredReport <= 0.0f) {
      Serial.println("Calibration read error.");
      return;
    }
    float newFactor = measuredVoltage / measuredReport;
    if (newFactor <= 0.0f || !isfinite(newFactor)) {
      Serial.println("Calibration failed (invalid factor).");
      return;
    }
    calib_factor = newFactor;
    _prefs.putFloat("calib", calib_factor);
    Serial.print("CALIB set to: ");
    Serial.println(calib_factor, 6);
  }

  // Runtime setters (you can call these from Serial parser or UI)
  void setDivider(float top, float bottom) { r_top = top; r_bottom = bottom; _prefs.putFloat("r_top", r_top); _prefs.putFloat("r_bottom", r_bottom); }
  void setNumSamples(int n) { if (n < 3) n = 3; num_samples = n; _prefs.putInt("num_samp", num_samples); }
  void setInterval(int ms) { if (ms < 100) ms = 100; interval_ms = ms; _prefs.putInt("int_ms", interval_ms); }
  void setEmaAlpha(float a) { if (a < 0.01f) a = 0.01f; if (a > 0.99f) a = 0.99f; ema_alpha = a; _prefs.putFloat("alpha", ema_alpha); }
  void setAdcRef(float v) { adc_ref = v; _prefs.putFloat("adc_ref", adc_ref); }

  // debug: print current settings
  void printConfig() {
    Serial.println("=== BatteryMonitor Config ===");
    Serial.printf("ADC pin: %d\n", pin_adc);
    Serial.printf("Divider: top=%.0f ohm, bottom=%.0f ohm\n", r_top, r_bottom);
    Serial.printf("samples: %d (delay %d ms)\n", num_samples, sample_delay_ms);
    Serial.printf("interval_ms: %d\n", interval_ms);
    Serial.printf("EMA alpha: %.3f\n", ema_alpha);
    Serial.printf("ADC ref: %.4f\n", adc_ref);
    Serial.printf("CALIB factor: %.6f\n", calib_factor);
    Serial.printf("STAT pin: %d\n", pin_stat);
    Serial.println("=============================");
  }

private:
  // internal state
  float _ema_voltage;
  int   _last_percent;

  Preferences _prefs;
  SemaphoreHandle_t _mutex;
  TaskHandle_t _task_handle;
  // static wrapper for xTaskCreate
  static void _taskFuncStatic(void *p) {
    static_cast<BatteryMonitor*>(p)->_taskFunc();
  }

  // main monitor task loop
  void _taskFunc() {
    // initial tiny delay so Serial can catch up
    vTaskDelay(pdMS_TO_TICKS(200));

    // initialize EMA to first read
    {
      float raw = _sampleMedianRaw();
      float v = _adcRawToBatteryVoltage(raw);
      _ema_voltage = v;
      _last_percent = _voltageToPercent(v);
    }

    for (;;) {
      // take median window
      float raw = _sampleMedianRaw();
      float v = _adcRawToBatteryVoltage(raw);
      int pct = _voltageToPercent(v);

      // EMA smoothing
      _ema_voltage = (ema_alpha * v) + ((1.0f - ema_alpha) * _ema_voltage);

      // update protected state
      if (_mutex) {
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
          _last_percent = pct;
          xSemaphoreGive(_mutex);
        }
      }

      // debug print
      Serial.print("[BattMon] rawV=");
      Serial.print(v, 3);
      Serial.print(" EMA=");
      Serial.print(_ema_voltage, 3);
      Serial.print(" V pct=");
      Serial.print(pct);
      if (pin_stat >= 0) {
        Serial.print(" charging=");
        Serial.print(digitalRead(pin_stat) == LOW ? "YES" : "NO");
      }
      Serial.println();

      // sleep until next interval
      vTaskDelay(pdMS_TO_TICKS(interval_ms));
    }
  }

  // compute battery voltage from raw ADC average, using current ADC_REF and calib_factor
  float _adcRawToBatteryVoltage(float adcAvg) {
    // voltage at ADC pin (pre-calib)
    float v_adc = (adcAvg / DEFAULT_ADC_MAX) * adc_ref; // adc_ref is adjustable
    float divider = (r_top + r_bottom) / r_bottom;
    float vbat = v_adc * divider * calib_factor;
    return vbat;
  }

  // get a median of several analogRead samples (raw ADC counts average)
  float _sampleMedianRaw() {
    int n = num_samples;
    if (n < 3) n = 3;
    // collect samples
    std::vector<int> v;
    v.reserve(n);
    for (int i = 0; i < n; ++i) {
      int a = analogRead(pin_adc);
      v.push_back(a);
      vTaskDelay(pdMS_TO_TICKS(sample_delay_ms));
    }
    // sort and pick median
    std::sort(v.begin(), v.end());
    int mid = n / 2;
    // compute small average of middle three to reduce jitter
    int start = max(0, mid - 1);
    int end = min(n - 1, mid + 1);
    long sum = 0;
    for (int i = start; i <= end; ++i) sum += v[i];
    float avg = (float)sum / (float)(end - start + 1);
    return avg;
  }

  // map voltage to percent using table and linear interpolation
  int _voltageToPercent(float v) {
    // local copy of table for clarity
    const float table[VOLTS_TABLE_SIZE] = {
      4.20, 4.05, 3.92, 3.86, 3.80, 3.75, 3.70, 3.65, 3.60, 3.55, 3.30
    };
    const int ptab[VOLTS_TABLE_SIZE] = {100,90,80,70,60,50,40,30,20,10,0};
    if (v >= table[0]) return 100;
    if (v <= table[VOLTS_TABLE_SIZE - 1]) return 0;
    for (int i = 0; i < VOLTS_TABLE_SIZE - 1; ++i) {
      if (v <= table[i] && v >= table[i+1]) {
        float v1 = table[i], v2 = table[i+1];
        int p1 = ptab[i], p2 = ptab[i+1];
        float frac = (v - v2) / (v1 - v2);
        int pct = round(p2 + frac * (p1 - p2));
        return pct;
      }
    }
    return 0;
  }

  // compile-time constant for internal table sizing
  static const int VOLTS_TABLE_SIZE = 11;
};

// -------------- global instance -------------------------
BatteryMonitor batMon;

// ----------------- Serial command parser ------------------
// Minimal text interface to view/set parameters and run calibration
void printHelp() {
  Serial.println(F("BATMON COMMANDS:"));
  Serial.println(F("HELP                 - show this help"));
  Serial.println(F("GET                  - print current battery state"));
  Serial.println(F("CFG                  - print current configuration"));
  Serial.println(F("CAL <voltage>        - calibrate using measured battery voltage (example: CAL 3.723)"));
  Serial.println(F("SET PIN <n>          - set ADC pin"));
  Serial.println(F("SET RTOP <ohm>       - set top resistor"));
  Serial.println(F("SET RBOT <ohm>       - set bottom resistor"));
  Serial.println(F("SET SAMP <n>         - set sample window count"));
  Serial.println(F("SET DELAY <ms>       - set ms between raw samples"));
  Serial.println(F("SET INT <ms>         - set monitor interval"));
  Serial.println(F("SET ALPHA <0..1>     - set EMA alpha"));
  Serial.println(F("SET ADCREF <v>       - set ADC ref voltage (nominal)"));
  Serial.println(F("SET STATPIN <pin>    - set optional TP4056 STAT pin (use -1 to disable)"));
  Serial.println(F("SAVE                 - persist settings to flash"));
}

void processSerialLine(String s) {
  s.trim();
  s.toUpperCase();
  if (s.length() == 0) return;

  if (s == "HELP") { printHelp(); return; }
  if (s == "GET") {
    float v; int p; bool ch;
    batMon.getBatteryStatus(&v, &p, &ch);
    Serial.printf("Battery (EMA) = %.3f V  %d%%  charging=%s\n", v, p, ch ? "YES":"NO");
    return;
  }
  if (s == "CFG") { batMon.printConfig(); return; }

  // tokenise
  std::vector<String> tokens;
  int idx = 0;
  while (idx < s.length()) {
    int sp = s.indexOf(' ', idx);
    if (sp == -1) { tokens.push_back(s.substring(idx)); break; }
    tokens.push_back(s.substring(idx, sp));
    idx = sp + 1;
  }
  if (tokens.size() == 0) return;

  if (tokens[0] == "CAL" && tokens.size() >= 2) {
    float m = tokens[1].toFloat();
    if (m > 0) {
      batMon.calibrateUsingMeasuredVoltage(m);
    } else Serial.println("CAL argument invalid");
    return;
  }

  if (tokens[0] == "SET" && tokens.size() >= 3) {
    String field = tokens[1];
    String val = tokens[2];
    if (field == "PIN") { batMon.pin_adc = val.toInt(); Serial.println("OK"); return; }
    if (field == "RTOP") { batMon.setDivider(val.toFloat(), batMon.r_bottom); Serial.println("OK"); return; }
    if (field == "RBOT") { batMon.setDivider(batMon.r_top, val.toFloat()); Serial.println("OK"); return; }
    if (field == "SAMP") { batMon.setNumSamples(val.toInt()); Serial.println("OK"); return; }
    if (field == "DELAY") { batMon.sample_delay_ms = val.toInt(); Serial.println("OK"); return; }
    if (field == "INT") { batMon.setInterval(val.toInt()); Serial.println("OK"); return; }
    if (field == "ALPHA") { batMon.setEmaAlpha(val.toFloat()); Serial.println("OK"); return; }
    if (field == "ADCREF") { batMon.setAdcRef(val.toFloat()); Serial.println("OK"); return; }
    if (field == "STATPIN") { batMon.pin_stat = val.toInt(); Serial.println("OK"); return; }
    Serial.println("UNKNOWN SET FIELD");
    return;
  }

  if (tokens[0] == "SAVE") { batMon.saveSettings(); return; }

  Serial.println("UNKNOWN COMMAND (type HELP)");
}

// ---------------- Arduino setup / loop -----------------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Modular Battery Monitor v1 ===");
  batMon.begin();
  printHelp();
}

String _line;
void loop() {
  // serial command reader
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      processSerialLine(_line);
      _line = "";
    } else {
      _line += c;
      if (_line.length() > 128) _line = _line.substring(0, 128);
    }
  }

  // keep loop light; do not perform battery work here
  vTaskDelay(pdMS_TO_TICKS(50));
}
