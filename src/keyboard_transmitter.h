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
#define USB_EVENT_STACK   4096
#define HID_HOST_DRIVER_STACK       8192
#define HID_WORKER_STACK            4096
// #define HID_ALPHABET_START          0x04
// #define HID_ALPHABET_ENDING         0x1D
// #define HID_TOP_ROW_NS_START        0x1E
// #define HID_TOP_ROW_NS_ENDING       0x27
//key DEFINATION
const char MY_KEY_ENTER             = '\r';
const char MY_KEY_TAB               = '\t'; 
const char MY_KEY_SPACE             = ' ';
const char MY_KEY_MINUS             = '-';
const char S_MY_KEY_MINUS           = '_';         
const char S_MY_KEY_EQUAL           = '+';
const char MY_KEY_EQUAL             = '=';
const char S_MY_KEY_OPEN_BRACES     = '{';
const char MY_KEY_OPEN_BRACES       = '[';
const char S_MY_KEY_CLOSE_BRACES    = '}';
const char MY_KEY_CLOSE_BRACES      = ']';
const char S_MY_KEY_BACK_SLASH      = '|';
const char MY_KEY_BACK_SLASH        = '\\';
const char S_MY_KEY_COLON           = ':';
const char MY_KEY_COLON             = ';';
const char S_MY_KEY_QUOTE           = '"';
const char MY_KEY_QUOTE             = '\'';
const char S_MY_KEY_TILDE           = '~';
const char MY_KEY_TILDE             = '`';
const char S_MY_KEY_LESS            = '<';
const char MY_KEY_LESS              = ',';  
const char S_MY_KEY_GREATER         = '>';
const char MY_KEY_GREATER           = '.';
const char S_MY_KEY_SLASH           = '?';
const char MY_KEY_SLASH             = '/'; 
//nimble const
const uint16_t  PREF_MIN_INTERVAL = 5;
const uint16_t  PREF_MAX_INTERVAL = 24;
const uint16_t  PREFERED_MTU      = 247;


const char TOPROW_NORMAL[] = "1234567890";
const char TOPROW_SHIFTED[] = "!@#$%^&*()";

typedef struct KB_EVENT{
    uint8_t usage;
    uint8_t mods;
    bool pressed;
};
// Forward declaration of C wrapper for HID driver callback (we install this as the callback)
extern "C" void hid_host_device_callback_cwrap(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg);

extern "C" void hid_host_interface_callback_cwrap(hid_host_device_handle_t hdh, const hid_host_interface_event_t event, void* arg);

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
    static void hid_host_Interface_callback_FORWARD(hid_host_device_handle_t hdh, const hid_host_interface_event_t event,void* arg);
private:
    QueueHandle_t           KBQueue;
    BleKeyboard             BleKBd;
    TaskHandle_t            BleTaskHandle;
    uint8_t                 active_mods;
    typedef struct HidKB_host_Event_Queue_t{
        hid_host_device_handle_t hdh;
        hid_host_driver_event_t event;
        void* arg;
    };
    QueueHandle_t hid_host_event_queue;
    static USBTOBLEKBbridge* s_instance_ptr;
    void TASK_BLE();
    void TASK_Hid_WORKER();
    static void TASK_Usb_LIBRARY(void* arg);
    static bool is_SHIFT(uint8_t mods);
    static char usage_TO_ASCII(uint8_t usage, uint8_t mods);
    static void hid_Host_Interface_CALLBACK(hid_host_device_handle_t hdh,hid_host_interface_event_t event,void* arg);
    static void hid_Host_Device_EVENT(hid_host_device_handle_t hdh, const hid_host_driver_event_t event,void* arg);    
    static void hid_KB_Report_CALLBACK(const uint8_t *const data, const int len);
    static void hid_MOUSE_Report_CALLBACK(const uint8_t *const data, const int length);
    static void setNimBLE_PREF();
    static void hid_Host_Generic_Report_CALLBACK(const uint8_t *const data, const int len);
};