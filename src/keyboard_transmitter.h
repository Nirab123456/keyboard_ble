#pragma once
#include <Arduino.h>
#include <BleKeyboard.h>
#include <NimBLEDevice.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <OledLogger.h>
#include "helper_keyboard_ble.h"
#include "usb/usb_host.h"
#include "hid_host.h"
#include "hid_usage_keyboard.h"
#include "hid_usage_mouse.h"

// -------------------- user config --------------------
#define BLE_DEVICE_NAME   "ESP_USB2BLE"
#define KEYQUEUE_DEPTH    256
#define BLE_TASK_STACK    4096
#define BLE_TASK_PRIO     6

class USBTOBLEKBbridge{
public:
    USBTOBLEKBbridge();
    bool begin();
    void enqueueKey(uint8_t usage,uint8_t mods,bool pressed);
    static void TASK_Ble_Wrapper(void* pv);
    static void TASK_Usb_lib_Wrapper(void* pv);
    static void Hid_Worker_Wrapper(void* pv);
    static void Hid_Host_Device_Callback(hid_host_device_handle_t hid_HDH,const hid_host_driver_event_t event,void* arg);
    static USBTOBLEKBbridge* instance();
    static void set_instance(USBTOBLEKBbridge* p);
private:
    QueueHandle_t           KBQueue;
    BleKeyboard             BleKBd;
    TaskHandle_t            BleTaskHandle;
    uint8_t                 active_mods;
    typedef struct Hid_host_Event_Queue_t;
    static USBTOBLEKBbridge* s_instance_ptr;
    void TASK_BLE();
    void TASK_Hid_WORKER();
    static void TASK_Usb_LIBRARY(void* arg);
    static bool is_SHIFT(uint8_t mods);
    static char usage_TO_ASCII(uint8_t usage, uint8_t mods);
    static void hid_Host_Interface_CALLBACK(hid_host_device_handle_t hdh,hid_host_interface_event_t event,void* arg);
    static void hid_Host_Device_EVENT(hid_host_device_handle_t hdh, const hid_host_interface_event_t event,void* arg);    
    static void hid_KB_Report_CALLBACK(const uint8_t *const data, const int length);
    static void hid_MOUSE_Report_CALLBACK(const uint8_t *const data, const int length);
    static void setNimBLE_PREF();
};