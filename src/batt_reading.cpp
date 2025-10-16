// batt_reading.cpp
#include <helper_keyboard_ble.h>
#include "task_CP.h"
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
        charge_status_pin(5),
        _ema_voltage(0.0f),
        _last_percentage(0),
        _prefs(),
        _mutex(NULL),
        _task_handle(NULL)
{
}

bool BatteryMonitorClass::begin()
{
    _mutex = xSemaphoreCreateMutex();
    if(!_mutex)
    {
        Serial.println("BATTERY::MUTEX::Creation failed");
        return false;
    }
    _prefs.begin(PREF_NAMESPACE,false);
    pin_adc = _prefs.getInt("pin_adc",pin_adc);
    r_top = _prefs.getFloat("r_top",r_top);
    r_bottom =  _prefs.getFloat("r_bottom",r_bottom);
    number_of_samples = _prefs.getInt("num_samp",number_of_samples);
    sample_delay_ms = _prefs.getInt("s_delayMS",sample_delay_ms);
    interval_ms = _prefs.getInt("interval_MS",interval_ms);
    ema_alpha = _prefs.getFloat("ema_alpha",ema_alpha);
    adc_ref = _prefs.getFloat("adc_ref",adc_ref);
    calibration_factor = _prefs.getFloat("calibration_factor",calibration_factor);
    charge_status_pin = _prefs.getInt("charge_status_pin",charge_status_pin);

    analogReadResolution(ATDR);
    #if defined(ARDUINO_ARCH_ESP32)
    analogSetPinAttenuation(pin_adc,ADC_11db);
    #endif


    if(charge_status_pin>=0)
    {
        pinMode(charge_status_pin,INPUT_PULLUP);
    }
    //Battery Monitor Task
    BaseType_t r = xTaskCreate(
        _taskFunctionSTATIC,
        "BATTERY_MONITOR_TASK",
        4096,this,
        BATTERY_MONITOR_TASK_PRIO,
        &_task_handle
    ); // any free core
    if (r!=pdPASS)
    {
        Serial.println("BATTERY::BATTERY-MONITOR-TASK::Creation failed");
        return false;
    }
    Serial.println("BATTERY-MONITOR-TASK::Created");
    return true;
}

void BatteryMonitorClass::saveSETTINGS()
{
    _prefs.putInt("pin_adc",pin_adc);
    _prefs.putFloat("r_top",r_top);
    _prefs.putFloat("r_bottom",r_bottom);
    _prefs.putInt("num_samp",number_of_samples);
    _prefs.putInt("s_delayMS",sample_delay_ms);
    _prefs.putInt("interval_MS",interval_ms);
    _prefs.putFloat("ema_alpha",ema_alpha);
    _prefs.putFloat("adc_ref",adc_ref);
    _prefs.putFloat("calibration_factor",calibration_factor);
    _prefs.putInt("charge_status_pin",charge_status_pin);
    Serial.println("BATTERY:Prefarance settings saved.");
}

// <- default removed here; header should keep the default if you want one
bool BatteryMonitorClass::getBatterySTATUS(float* outVoltage,int* outPercent,bool* outCharging)
{
    if (!_mutex)
    {
        return false;
    }
    if (xSemaphoreTake(_mutex,pdMS_TO_TICKS(50)) != pdTRUE)
    {
        return false;
    }
        /* code */
    if (outVoltage)
    {
        *outVoltage = _ema_voltage;
    }
    if (outPercent)
    {
        *outPercent = _last_percentage;
    }
    if (outCharging && charge_status_pin >= 0)
    {
        *outCharging = (digitalRead(charge_status_pin)==LOW);
    }
    xSemaphoreGive(_mutex);
    return true;
}

void BatteryMonitorClass::calibrateUsingMEASURED_Volt(float measuredVoltage)
{
    float raw_adc = _sampleMedianRAW();
    float measuredReport = _adcRawToBatteryVOLTAGE(raw_adc);
    if (measuredReport <= 0.0f)
    {
        Serial.println("BATTERY MONITOR::Calibration read error");
        return;
    }
    float  newFactor = measuredVoltage/measuredReport;
    if (newFactor<=0.0f || !isfinite(newFactor))
    {
        Serial.println("BATTERY MONITOR :: CALIBRATION:: Faild(invalid factor)");
        return;
    }
    calibration_factor = newFactor;
    _prefs.putFloat("calibration_factor",calibration_factor);
    Serial.println(calibration_factor,6);
}
void BatteryMonitorClass::setDEVIDER(float top , float bottom)
{
    r_top = top;
    r_bottom = bottom;
    _prefs.putFloat("r_top",r_top);
    _prefs.putFloat("r_bottom",r_bottom);
}
void BatteryMonitorClass::setNumberOfSAMPLES(uint8_t n)
{
    if (n<3)
    {
        n = 3;
    }
    number_of_samples = n;
    _prefs.putInt("num_samp",number_of_samples);
    
}
void BatteryMonitorClass::setINTERVAL(uint16_t ms)
{
    if (ms < 100)
    {
        ms = 100;
    }
    interval_ms = ms;
    _prefs.putInt("interval_MS",interval_ms);
}
void BatteryMonitorClass::setemaALPHA(float alpha)
{
    if (alpha <0.01f)
    {
        alpha = 0.01f;
    }
    ema_alpha = alpha;
    _prefs.putFloat("ema_alpha",ema_alpha);
}
void BatteryMonitorClass::setAdcREF(float v)
{
    adc_ref = v;
    _prefs.putFloat("adc_ref",adc_ref);
}
void BatteryMonitorClass::printCONFIG()
{
    Serial.println("----Battery Monitor Configuration----");
    Serial.printf("ADC pin : %d\n",pin_adc);
    Serial.printf("Divider : top =%0.0f ohm,\tbottom = %0.0f ohm\n",r_top,r_bottom);
    Serial.printf("Number of Samples : %d & Delay %d ms\n",number_of_samples,sample_delay_ms);
    Serial.printf("Interval %d ms\n",interval_ms);
    Serial.printf("EMA ALPHA : %0.3f\n",ema_alpha);
    Serial.printf("ADC Refarance : %0.4f\n",adc_ref);
    Serial.printf("Calibration Factor %0.6f\n",calibration_factor);
    Serial.printf("Charge Status pin : %u\n",charge_status_pin);
    Serial.println("------------------DONE-----------------");

}

void BatteryMonitorClass::_taskFunctionSTATIC(void* p)
{
    static_cast<BatteryMonitorClass*>(p)->_taskFUNC();
}
float BatteryMonitorClass::_adcRawToBatteryVOLTAGE(float adcAvg)
{
    float v_adc = ((adcAvg/DEFAULT_ADC_MAX)*adc_ref);
    float divider = ((r_top+r_bottom)/r_bottom);
    float vbat = v_adc*calibration_factor*divider;
    return vbat;
}
void BatteryMonitorClass::_taskFUNC()
{
    vTaskDelay(pdMS_TO_TICKS(200));

    {
        float raw = _sampleMedianRAW();
        float v = _adcRawToBatteryVOLTAGE(raw);
        _ema_voltage = v;
        _last_percentage = _voltageToPERCENTAGE(v);
    }
    for (;;)
    {
        float raw = _sampleMedianRAW();
        float v = _adcRawToBatteryVOLTAGE(raw);
        int pct = _voltageToPERCENTAGE(v);

        _ema_voltage = (ema_alpha*v)+((1.0f-ema_alpha)*_ema_voltage);
        if(_mutex)
        {
            if (xSemaphoreTake(_mutex,pdMS_TO_TICKS(50))==pdTRUE)
            {
                _last_percentage = pct;
                xSemaphoreGive(_mutex);
            }
            
        }
        Serial.print("BATTERY MONITOR :: Raw Voltage = ");
        Serial.print(v,3);
        Serial.print(" EMA = ");
        Serial.print(_ema_voltage,3);// why 3??
        Serial.print("Voltage pct = ");
        Serial.print(pct);
        if (charge_status_pin >= 0)
        {
            Serial.print(digitalRead(charge_status_pin)==LOW ? "YES":"NO");
        }
        Serial.println();
        vTaskDelay(pdMS_TO_TICKS(interval_ms));
    }   
}

float BatteryMonitorClass:: _sampleMedianRAW()
{
    uint8_t n = number_of_samples;
    if (n<3)
    {
        n = 3;
    }
    std::vector<int>v;
    v.reserve(n);
    for (size_t i = 0; i < n; i++)
    {
        int a = analogRead(pin_adc);
        v.push_back(a);
        vTaskDelay(pdMS_TO_TICKS(sample_delay_ms));
    }

    std::sort(v.begin(),v.end());
    uint8_t mid = n/2;
    uint8_t start = max(0,mid-1);
    uint8_t end = min(n-1,mid+1);
    long sum = 0;
    for(int i = start;i<=end;i++)
    {
        sum+= v[i];
    }
    float avg = (float)sum/(float)(end-start+1);
    return avg;    
}

uint8_t BatteryMonitorClass:: _voltageToPERCENTAGE(float v)
{
    const float table[VOLTS_TABLE_SIZE] ={
        4.2,4.05,3.92,3.86,3.80,3.75,3.70,3.65,3.60,3.55,3.30
    };
    const int ptab[VOLTS_TABLE_SIZE] = {
        100,90,80,70,60,50,40,30,20,10,0
    };
    if (v>=table[0])
    {
        return 100;
    }
    if (v<=table[VOLTS_TABLE_SIZE-1])
    {
        return 0;
    }
    for (uint8_t i = 0; i < VOLTS_TABLE_SIZE-1; i++)
    {
        if (v<=table[i]&& v>= table[i+1])
        {
            float v1 = table[i];
            float v2 = table[i+1];
            uint8_t p1 = ptab[i];
            uint8_t p2 = ptab[i+1];
            float frac = (v-v2)/(v1-v2);
            uint8_t pct = round(p2+frac*(p1-p2));
            return pct;
        }
    }
    return 0;
}

void BatteryMonitorClass:: processSerialLINE(String &is)
{
    is.trim();
    is.toUpperCase();
    if (is.length()==0)
    {
        return;
    }
    if (is=="HELP")
    {
        printHELP();
        return;   
    }
    if (is == "GET")
    {
        float raw_V;
        int bat_percent;
        bool charge_indecitor;
        getBatterySTATUS(&raw_V,&bat_percent,&charge_indecitor);
        Serial.printf("BATTERY MONITOR :: Voltage :%0.3f\t BATTERY PERCENTAGE :%i\nCharging : %s",raw_V,bat_percent,charge_indecitor? "\tYES":"\tNO");
    }
    if (is=="CFG")
    {
        printCONFIG();
        return;
    }
    
    std::vector<String> tokens;
    int idx = 0;
    while (idx<is.length())
    {
        int sp = is.indexOf(' ',idx);
        if (sp == -1) {
            tokens.push_back(is.substring(idx)); // rest of string
            break;
        } else {
            tokens.push_back(is.substring(idx, sp));
            idx = sp + 1;
        }
    }
    if (tokens.size()==0)
    {return;
    }
    if (tokens[0]=="CAL"&&tokens.size()>=2)
    {
        float m =tokens[1].toFloat();
        if (m>0)
        {
            calibrateUsingMEASURED_Volt(m);
        }
        else
        {
            Serial.println("BATTERY:CALIBRATION::Argument Invalid");
        }
        return;
    }

    if (tokens[0]=="SET")
    {
        String field = tokens[1];
        String val = tokens[2];
        if (field=="PIN")
        {
            pin_adc = val.toInt();
        }
        else if (field =="RTOP")
        {
            setDEVIDER(val.toFloat(),r_bottom);
        }
        else if (field == "RBOTTOM")
        {
            setDEVIDER(r_top,val.toFloat());
        }
        else if (field == "NUMSAMP")
        {
            setNumberOfSAMPLES(val.toInt());
        }
        else if (field == "DELAY")
        {
            sample_delay_ms = val.toInt();
        }
        else if (field=="INTERVAL")
        {
            setINTERVAL(val.toInt());
            return;
        }
        else if (field == "ALPHA")
        {
            setemaALPHA(val.toFloat());
        }
        else if (field=="ADCREF")
        {
            setAdcREF(val.toFloat());
        }
        else if (field=="CHARGEINDICATOR")
        {
            charge_status_pin = val.toInt();
        }
        else
        {
            Serial.println("UNKNOWN SET FIELD");
        }
        return;
    }
    else if (tokens[0]=="SAVE")
    {
        saveSETTINGS();
        return;
    }
    else
    {
        Serial.println("UNKNOWN -- COMMAND use:HELP");
    }
}

void BatteryMonitorClass:: printHELP()
{
    Serial.println(F("BATMON COMMAND: "));
    Serial.println(F("HELP  --------    Show all commands"));
    Serial.println(F("CFG   --------    Print current configuration"));
    Serial.println(F("GET   --------    Print current battery state"));
    Serial.println(F("CAL<voltage>     -Calibrate using measured battery voltage"));
    Serial.println(F("SET PIN <n>      -set ADC pin"));
    Serial.println(F("SET RTOP <ohm>       - set top resistor"));
    Serial.println(F("SET RBOT <ohm>       - set bottom resistor"));
    Serial.println(F("SET SAMP <n>         - set sample window count"));
    Serial.println(F("SET DELAY <ms>       - set ms between raw samples"));
    Serial.println(F("SET INTERVAL <ms>         - set monitor interval"));
    Serial.println(F("SET ALPHA <0..1>     - set EMA alpha"));
    Serial.println(F("SET ADCREF <v>       - set ADC ref voltage (nominal)"));
    Serial.println(F("SET CHARGEINDICATOR <pin>    - set optional TP4056 STAT pin (use -1 to disable)"));
    Serial.println(F("SAVE                 - persist settings to flash"));
        
}

