#pragma once
#include <Arduino.h>

const uint8_t BLE_Task_WRAPPER_PRIO  = 6;
const uint8_t USB_EVENTS_WRAPPER_PRIO  =2;
const uint8_t HID_HOST_DRIVER_TASK_PRIO =8;
const uint8_t HID_WORKER_PRIO =2;
const uint8_t BATTERY_MONITOR_TASK_PRIO = 2;








const uint8_t BLE_Task_WRAPPER_Core = 0;
const uint8_t USB_EVENTS_WRAPPER_Core = 0;
const uint8_t HID_HOST_DRIVER_TASK_Core =0;

