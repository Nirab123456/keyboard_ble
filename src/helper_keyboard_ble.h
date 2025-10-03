#include <Arduino.h>
#include <Preferences.h>


class BatteryMonitorClass{
    public:
        uint8_t pin_adc;
        float   r_top;
        float   r_bottom;
        uint8_t number_of_samples;
        uint16_t sample_delay_ms;
        uint16_t interval_ms;
        float    adc_ref;
        float    calibration_factor;
        
        uint8_t  charge_status_pin;

};