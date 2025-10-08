#include "helper_keyboard_ble.h"
#include <OledLogger.h>
#define COMMAND_BATTERY_MONITOR_START   "bmon-start"
#define COMMAND_SERIAL_LOOP_CALIBRATION "_l_bserial-cal"
BatteryMonitorClass batmon;
static String _line;


void setup()
{
    Serial.begin(115200);
    delay(200);
    if (!OledLogger::begin(0x3C, 128, 64, 8, 9, 16 /*queue len*/)) {
      Serial.println("OledLogger failed");
    } else {
      OledLogger::logf("Oled ready");
    }
    OledLogger::logf("USB -> NimBLE bridge starting...");
    //battery monitor
    batmon.begin();
    batmon.printHELP();
}

void loop()
{

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      batmon.processSerialLINE(_line);
      _line = "";
    } else {
      _line += c;
      if (_line.length() > 128) _line = _line.substring(0, 128);
    }
  }

}