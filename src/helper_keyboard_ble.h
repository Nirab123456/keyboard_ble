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
        BatteryMonitorClass();
        bool begin();
        void saveSETTINGS();
        bool getBatterySTATUS();
        void calibrateUsingMEASURED_Volt(float measured_Volt);
        void setDEVIDER(float top, float bottom);
        void setNumberOfSAMPLES(uint8_t n);
        void setINTERVAL(float a);
        void setAdcREF(float v);
        void printCONFIG();

    private:

        float _ema_voltage;
        int   _last_percentage;

        Preferences _prefs;
        SemaphoreHandle_t _mutex;
        TaskHandle_t  _task_handle;

        static void _taskFunctionSTATIC(void* p);
        void _taskFUNC();
        float _adcRawToBatteryVOLTAGE(float adcAVG);
        float _sampleMedianRAW();
        int _voltageToPERCENTAGE(float v);
        static const uint8_t VOLTS_TABLE_SIZE =11;
};