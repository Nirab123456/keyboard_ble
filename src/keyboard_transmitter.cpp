#include "keyboard_transmitter.h"

USBTOBLEKBbridge:: USBTOBLEKBbridge()
    :KBQueue(nullptr),
    BleKBd(BLE_DEVICE_NAME),
    BleTaskHandle(nullptr),
    active_mods(0),
    hid_host_event_queue(nullptr)
{}
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

void USBTOBLEKBbridge::TASK_Ble_Wrapper(void* pv)
{
    USBTOBLEKBbridge* instance = static_cast<USBTOBLEKBbridge*>(pv);
    instance -> TASK_BLE();
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
