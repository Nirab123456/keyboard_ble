#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <vector>               // used by implementation for median/sort
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class BatteryMonitorClass {
public:
    // public configuration fields 
    uint8_t pin_adc;
    float   r_top;
    float   r_bottom;
    uint8_t number_of_samples;
    uint16_t sample_delay_ms;
    uint16_t interval_ms;
    float    ema_alpha;
    float    adc_ref;
    float    calibration_factor;

    uint8_t  charge_status_pin;

    // ctor / lifecycle
    BatteryMonitorClass();
    bool begin();                  // start the monitor task and load prefs
    void saveSETTINGS();           // persist settings to flash

    bool getBatterySTATUS(float *outVoltage, int *outPercent, bool *outCharging = nullptr);

    // calibration & setters 
    void calibrateUsingMEASURED_Volt(float measured_Volt);
    void setDEVIDER(float top, float bottom);
    void setNumberOfSAMPLES(uint8_t n);
    void setINTERVAL(uint16_t a);
    void setAdcREF(float v);
    void printCONFIG();
    void setemaALPHA(float alpha);
    void processSerialLINE(String &s);
    void printHELP();
    void executeBATTERYMONITOR(char* cmnd,String &s);
private:
    // internal state
    float _ema_voltage;
    int   _last_percentage;

    Preferences _prefs;
    SemaphoreHandle_t _mutex;
    TaskHandle_t  _task_handle;

    // internal helpers 
    static void _taskFunctionSTATIC(void* p);
    void _taskFUNC();
    float _adcRawToBatteryVOLTAGE(float adcAVG);
    float _sampleMedianRAW();
    uint8_t _voltageToPERCENTAGE(float v);

    // compile-time size for the table (unchanged name)
    static const uint8_t VOLTS_TABLE_SIZE = 11;
};