#include <Arduino.h>
#include <Preferences.h>

#define DEFAULT_ADC_PIN         4
#define DEFAULT_R_TOP           100000.0f
#define DEFAULT_R_BOTTOM        100000.0f
#define DEFAULT_SAMPALES        20
#define DEFAULT_SAMPLE_DELAY    5
#define DEFAULT_INTERVAL_MS     1500
#define DEFAULT_EMA_ALPHA       0.2f 
#define DEFAULT_ADC_MAX         4095.0f
#define DEFAULT_ADC_REF         3.3f

#define PREF_NAMESPACE          "BATTERY-MONITOR:V1"


