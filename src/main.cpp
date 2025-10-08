#include "helper_keyboard_ble.h"
#include "oled_debug.h"
#define COMMAND_BATTERY_MONITOR_START   "bmon-start"
#define COMMAND_SERIAL_LOOP_CALIBRATION "_l_bserial-cal"
BatteryMonitorClass batmon;
static String _line;


void setup()
{
    Serial.begin(115200);
    delay(200);
    oled_INIT();
    oled_LOGF("Boot OK");
}

void loop()
{

}