#include "helper_keyboard_ble.h"
#include <keyboard_transmitter.h>
BatteryMonitorClass batmon;
static String _line;
static USBTOBLEKBbridge global_bridge;

void setup()
{
    //Initiate keyboard
    USBTOBLEKBbridge::set_instance(&global_bridge);
    if (!global_bridge.begin())
    {
      for (;;)
      {
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
      
    }
    
}

void loop()
{
  vTaskDelay(pdMS_TO_TICKS(1000));
 

}