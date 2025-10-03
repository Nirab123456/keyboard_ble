#include <Arduino.h>
#include <Preferences.h>
#include <helper_keyboard_ble.h>

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

BatteryMonitorClass::BatteryMonitorClass()
    :   pin_adc(DEFAULT_ADC_PIN),
        r_top(DEFAULT_R_TOP),
        r_bottom(DEFAULT_R_BOTTOM),
        number_of_samples(DEFAULT_SAMPALES),
        sample_delay_ms(DEFAULT_SAMPLE_DELAY),
        interval_ms(DEFAULT_INTERVAL_MS),
        ema_alpha(DEFAULT_EMA_ALPHA),
        adc_ref(DEFAULT_ADC_REF),
        calibration_factor(1.0f),
        charge_status_pin(-1),
        _ema_voltage(0.0f),
        _last_percentage(0),
        _prefs(),
        _mutex(NULL),
        _task_handle(NULL)
{

}


