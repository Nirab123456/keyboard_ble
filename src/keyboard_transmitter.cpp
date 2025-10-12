#include "keyboard_transmitter.h"


USBTOBLEKBbridge:: USBTOBLEKBbridge()
    :KBQueue(nullptr),
    BleKBd(BLE_DEVICE_NAME),
    BleTaskHandle(nullptr),
    active_mods(0),
    hid_host_event_queue(nullptr)
{}

USBTOBLEKBbridge* USBTOBLEKBbridge:: s_instance_ptr = nullptr;
USBTOBLEKBbridge* USBTOBLEKBbridge :: instance()
{
    return s_instance_ptr;
}
void USBTOBLEKBbridge::set_instance(USBTOBLEKBbridge* p)
{
    s_instance_ptr = p;
}

bool USBTOBLEKBbridge::begin()
{
    if (!OledLogger::begin(0x3c,128,64.8,9,16))
    {
        Serial.println("OLED : NO OLED CONNECTED ");
    }
    OledLogger::logf("OLED READY");
    KBQueue = xQueueCreate(KEYQUEUE_DEPTH,sizeof(KB_EVENT));
    if (!KBQueue)
    {
        OledLogger::logf("KB-QUeue:Creation FAILED!");
        return false;
    }

    //BLE TASK TO CORE = 1
    xTaskCreatePinnedToCore(
        TASK_Ble_Wrapper,
        "BLE-Task",
        BLE_TASK_STACK,
        this,
        BLE_TASK_PRIO,
        &BleTaskHandle,
        1
    );

    //USB EVENT TASK PINNED TO CORE = 0
    BaseType_t ok =xTaskCreatePinnedToCore(
        TASK_Usb_lib_Wrapper,
        "USB-EVENTS",
        USB_EVENT_STACK,
        xTaskGetCurrentTaskHandle(),
        USB_EVENT_PRIO,
        NULL,
        0
    );
    if (ok != pdTRUE)
    {
        OledLogger::logf("USB-EVENT: Creation FAILED!");
    }
    
    ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(3000));
    const hid_host_driver_config_t HHD_cfg = {
        .create_background_task = true,
        .task_priority = HID_HOST_DRIVER_TASK_PRIO,
        .stack_size = HID_HOST_DRIVER_STACK,
        .core_id = 0,
        .callback = hid_host_device_callback_cwrap,
        .callback_arg = NULL
    };
    ESP_ERROR_CHECK(hid_host_install(&HHD_cfg));
    hid_host_event_queue = xQueueCreate(10,sizeof(Hid_host_Event_Queue_t));

    xTaskCreate(
        Hid_Worker_Wrapper,
        "HID-WORKER",
        HID_WORKER_STACK,
        this,
        HID_WORKER_PRIO,
        NULL
    );

    OledLogger::logf("SETUP DONE : PLUG <-");
    return true;
}

void USBTOBLEKBbridge::enqueueKey(uint8_t usage,uint8_t mods,bool pressed)
{
    if (!KBQueue)
    {
        return;
    }
    KB_EVENT event {
        usage,
        mods,
        pressed
    };
    BaseType_t inISR = xPortInIsrContext();
    if (inISR)
    {
        BaseType_t woken = pdFALSE;
        xQueueSendFromISR(KBQueue,&event,&woken);
        portYIELD_FROM_ISR(woken);
    }
    else
    {
        xQueueSend(KBQueue,&event,0);
    }
}
void USBTOBLEKBbridge::TASK_Ble_Wrapper(void* pv)
{
    USBTOBLEKBbridge* instance = static_cast<USBTOBLEKBbridge*>(pv);
    instance -> TASK_BLE();
}
void USBTOBLEKBbridge::TASK_Usb_lib_Wrapper(void* pv)
{
    USBTOBLEKBbridge::TASK_Usb_LIBRARY(pv);
}
void USBTOBLEKBbridge::Hid_Worker_Wrapper(void* pv)
{
    USBTOBLEKBbridge* instance = static_cast<USBTOBLEKBbridge*>(pv);
}

void USBTOBLEKBbridge:: Hid_Host_Device_Callback(hid_host_device_handle_t hdh,const hid_host_driver_event_t event, void* arg)
{
    if(!instance)
    {
        return;
    }
    Hid_host_Event_Queue_t ev = {
        .hdh = hdh,
        .event = event,
        .arg = arg
    };
    xQueueSend(instance()->hid_host_event_queue,&event,0);
}

void USBTOBLEKBbridge::TASK_BLE()
{
    setNimBLE_PREF();
    BleKBd.begin();
    OledLogger::logf("BLE:ADVERTISING");
    KB_EVENT event;
    for(;;)
    {
        if (xQueueReceive(KBQueue,&event,portMAX_DELAY)==pdTRUE)
        {
            OledLogger::logf(
                "EVENT: USED =0x%02x,\n mods =0x%02x\nPressed:%d",
                    event.usage,
                    event.mods,
                    event.pressed
            );
            if (!BleKBd.isConnected())
            {
                continue;
            }
            uint8_t new_mods = event.mods;
            
            if (new_mods != active_mods)
            {
                uint8_t relese_mask = active_mods & ~new_mods;
                if (relese_mask&HID_LEFT_CONTROL)
                {
                    BleKBd.release(KEY_LEFT_CTRL);
                }
                else if (relese_mask & HID_RIGHT_CONTROL)
                {
                    BleKBd.release(KEY_RIGHT_CTRL);
                }
                else if (relese_mask & HID_LEFT_SHIFT)
                {
                    BleKBd.release(KEY_LEFT_SHIFT);
                }
                else if (relese_mask & HID_RIGHT_SHIFT)
                {
                    BleKBd.release(KEY_RIGHT_SHIFT);
                }
                else if (relese_mask & HID_LEFT_ALT)
                {
                    BleKBd.release(KEY_LEFT_ALT);
                }
                else if (relese_mask & HID_RIGHT_ALT)
                {
                    BleKBd.release(KEY_RIGHT_ALT);
                }
                else if (relese_mask & HID_LEFT_GUI)
                {
                    BleKBd.release(KEY_LEFT_GUI);
                }
                else if (relese_mask & HID_RIGHT_GUI)
                {
                    BleKBd.release(KEY_RIGHT_GUI);
                }

                uint8_t press_mask =  new_mods & ~active_mods;
                if (press_mask & HID_LEFT_CONTROL)
                {
                    BleKBd.press(KEY_LEFT_CTRL);
                }
                else if (press_mask & HID_RIGHT_CONTROL)
                {
                    BleKBd.press(KEY_RIGHT_SHIFT);
                }
                else if (press_mask & HID_LEFT_SHIFT)
                {
                    BleKBd.press(KEY_LEFT_SHIFT);
                }
                else if (press_mask & HID_RIGHT_SHIFT)
                {
                    BleKBd.press(KEY_RIGHT_SHIFT);
                }
                else if (press_mask & HID_LEFT_ALT)
                {
                    BleKBd.press(KEY_LEFT_ALT);
                }
                else if (press_mask & HID_RIGHT_ALT)
                {
                    BleKBd.press(KEY_RIGHT_ALT);
                }
                else if (press_mask & HID_LEFT_GUI)
                {
                    BleKBd.press(KEY_LEFT_GUI);
                }
                else if (press_mask & HID_RIGHT_GUI)
                {
                    BleKBd.press(KEY_RIGHT_GUI);
                }
                
                active_mods = new_mods;
            }
            
            if (event.usage==0)
            {
                continue;
            }
            char ch = usage_TO_ASCII(event.usage,event.mods);
            if (ch)
            {
                if (event.pressed)
                {
                    BleKBd.write(ch);
                }
                
            }
            else
            {
                if (event.pressed)
                {
                    switch(event.usage)
                    {
                        case HID_KEY_ENTER      : BleKBd.write(HID_KEY_ENTER);break;
                        case HID_KEY_ESC        : BleKBd.write(KEY_ESC);break;
                        case HID_KEY_CAPS_LOCK  : BleKBd.write(KEY_CAPS_LOCK);break;
                        case MY_HID_BACKSPACE   : BleKBd.write(KEY_BACKSPACE);break; // dbm(defined by me)
                        case HID_KEY_TAB        : BleKBd.write(KEY_TAB);break;
                        case HID_KEY_F1         : BleKBd.press(KEY_F1); BleKBd.release(KEY_F1); break;
                        case HID_KEY_F2         : BleKBd.press(KEY_F2); BleKBd.release(KEY_F2); break;
                        case HID_KEY_F3         : BleKBd.press(KEY_F2); BleKBd.release(KEY_F3); break;
                        case HID_KEY_F4         : BleKBd.press(KEY_F2); BleKBd.release(KEY_F4); break;
                        case HID_KEY_F5         : BleKBd.press(KEY_F2); BleKBd.release(KEY_F5); break;
                        case HID_KEY_F6         : BleKBd.press(KEY_F2); BleKBd.release(KEY_F6); break;
                        case HID_KEY_F7         : BleKBd.press(KEY_F2); BleKBd.release(KEY_F7); break;
                        case HID_KEY_F8         : BleKBd.press(KEY_F2); BleKBd.release(KEY_F8); break;
                        case HID_KEY_F9         : BleKBd.press(KEY_F2); BleKBd.release(KEY_F9); break;
                        case HID_KEY_F10        : BleKBd.press(KEY_F2); BleKBd.release(KEY_F10); break;
                        case HID_KEY_F11        : BleKBd.press(KEY_F2); BleKBd.release(KEY_F11); break;
                        case HID_KEY_F12        : BleKBd.press(KEY_F2); BleKBd.release(KEY_F12); break;

                    }
                }
                
            }
            
            
            
        }
        
    }
    
}
