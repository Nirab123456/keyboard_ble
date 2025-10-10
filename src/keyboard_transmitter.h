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

