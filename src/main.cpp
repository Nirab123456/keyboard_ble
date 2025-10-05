#include "helper_keyboard_ble.h"
#define COMMAND_BATTERY_MONITOR_START   "bmon-start"
#define COMMAND_SERIAL_LOOP_CALIBRATION "_l_bserial-cal"
BatteryMonitorClass batmon;
static String _line;


void setup()
{
    Serial.begin(115200);
    delay(300);
    batmon.executeBATTERYMONITOR(COMMAND_BATTERY_MONITOR_START,_line);//_line not used nor available

}

void loop()
{
    batmon.executeBATTERYMONITOR(COMMAND_BATTERY_MONITOR_START,_line);

}