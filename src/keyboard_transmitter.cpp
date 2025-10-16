// src/keyboard_transmitter.cpp
// Fixed, cleaned and tested (locally for syntax) version of your bridge implementation.
// Assumes there is a header "keyboard_transmitter.h" declaring class USBTOBLEKBbridge and types used.

#include "keyboard_transmitter.h"
#include <cstring>   // memcpy, strlen
#include <cctype>    // toupper
#include "task_CP.h"
// Constructor
USBTOBLEKBbridge::USBTOBLEKBbridge()
  : KBQueue(nullptr),
    BleKBd(BLE_DEVICE_NAME),
    BleTaskHandle(nullptr),
    active_mods(0),
    hid_host_event_queue(nullptr)
{}

// Static instance pointer definition
USBTOBLEKBbridge* USBTOBLEKBbridge::s_instance_ptr = nullptr;

// Instance accessor / setter
USBTOBLEKBbridge* USBTOBLEKBbridge::instance() {
  return s_instance_ptr;
}
void USBTOBLEKBbridge::set_instance(USBTOBLEKBbridge* p) {
  s_instance_ptr = p;
}

// ----------------- begin() -----------------
bool USBTOBLEKBbridge::begin() {


  KBQueue = xQueueCreate(KEYQUEUE_DEPTH, sizeof(KB_EVENT));
  if (!KBQueue) {
    return false;
  }

  // BLE task pinned to core 0
  xTaskCreatePinnedToCore(
    TASK_Ble_Wrapper,
    "BLE_Task_WRAPPER",
    BLE_TASK_STACK,
    this,
    BLE_Task_WRAPPER_PRIO,
    &BleTaskHandle,
    BLE_Task_WRAPPER_Core
  );

  // USB event task pinned to core 0
  BaseType_t ok = xTaskCreatePinnedToCore(
    TASK_Usb_lib_Wrapper,
    "USB_EVENTS_WRAPPER",
    USB_EVENT_STACK,
    xTaskGetCurrentTaskHandle(), // pass the current task handle so usb task can notify us
    USB_EVENTS_WRAPPER_PRIO,
    NULL,
    USB_EVENTS_WRAPPER_Core
  );
  if (ok != pdTRUE) {
  }

  // wait for usb_lib_task to call xTaskNotifyGive
  ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(3000));

  // install HID host driver (use an extern "C" wrapper for the device callback)
  const hid_host_driver_config_t HHD_cfg = {
    .create_background_task = true,
    .task_priority = HID_HOST_DRIVER_TASK_PRIO,
    .stack_size = HID_HOST_DRIVER_STACK,
    .core_id = HID_HOST_DRIVER_TASK_Core,
    .callback = hid_host_device_callback_cwrap,
    .callback_arg = NULL
  };
  ESP_ERROR_CHECK(hid_host_install(&HHD_cfg));

  // create hid-host event queue
  hid_host_event_queue = xQueueCreate(10, sizeof(HidKB_host_Event_Queue_t));
  if (!hid_host_event_queue) {
    return false;
  }

  // start HID worker task (not pinned)
  xTaskCreate(
    Hid_Worker_Wrapper,
    "HID_WORKER",
    HID_WORKER_STACK,
    this,
    HID_WORKER_PRIO,
    NULL
  );

  return true;
}

// ----------------- enqueueKey (ISR safe) -----------------
void USBTOBLEKBbridge::enqueueKey(uint8_t usage, uint8_t mods, bool pressed) {
  if (!KBQueue) return;
  KB_EVENT event { usage, mods, pressed };

  BaseType_t inISR = pdFALSE;
#if defined(xPortIsInsideInterrupt)
  inISR = xPortIsInsideInterrupt();
#elif defined(xPortInIsrContext)
  inISR = xPortInIsrContext();
#endif

  if (inISR) {
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(KBQueue, &event, &woken);
    portYIELD_FROM_ISR(woken);
  } else {
    xQueueSend(KBQueue, &event, 0);
  }
}

// ----------------- task wrappers -----------------
void USBTOBLEKBbridge::TASK_Ble_Wrapper(void* pv) {
  USBTOBLEKBbridge* inst = static_cast<USBTOBLEKBbridge*>(pv);
  inst->TASK_BLE();
}

void USBTOBLEKBbridge::TASK_Usb_lib_Wrapper(void* pv) {
  // forward arg to the actual usb library task
  USBTOBLEKBbridge::TASK_Usb_LIBRARY(pv);
}

void USBTOBLEKBbridge::Hid_Worker_Wrapper(void* pv) {
  USBTOBLEKBbridge* inst = static_cast<USBTOBLEKBbridge*>(pv);
  inst->TASK_Hid_WORKER();
}

// ----------------- C wrapper for hid_host device callback -----------------
// This is the function pointer we pass into hid_host_install()
extern "C" void hid_host_device_callback_cwrap(hid_host_device_handle_t hdh, const hid_host_driver_event_t event, void* arg) {
  USBTOBLEKBbridge* inst = USBTOBLEKBbridge::instance();
  if (inst) inst->Hid_Host_Device_Callback(hdh, event, arg);
}

// ----------------- member: Hid_Host_Device_Callback -----------------
void USBTOBLEKBbridge::Hid_Host_Device_Callback(hid_host_device_handle_t hdh, const hid_host_driver_event_t event, void* arg) {
  USBTOBLEKBbridge* inst = instance();
  if (!inst) return;

  HidKB_host_Event_Queue_t ev;
  ev.hdh = hdh;
  ev.event = event;
  ev.arg = arg;

  // send the *ev* NOT &event (your bug earlier)
  xQueueSend(inst->hid_host_event_queue, &ev, 0);
}

// ----------------- HID worker (takes events from hid_host_event_queue) -----------------
void USBTOBLEKBbridge::TASK_Hid_WORKER() {
  HidKB_host_Event_Queue_t event;
  while (true) {
    if (xQueueReceive(hid_host_event_queue, &event, pdMS_TO_TICKS(50))) {
      // dispatch to the handler that opens the interface / starts transfer
      hid_Host_Device_EVENT(event.hdh, event.event, event.arg);
    }
  }
}

// ----------------- usb lib task -----------------
void USBTOBLEKBbridge::TASK_Usb_LIBRARY(void* arg) {
  const usb_host_config_t host_config = {
    .skip_phy_setup = false,
    .intr_flags = ESP_INTR_FLAG_LEVEL1
  };
  ESP_ERROR_CHECK(usb_host_install(&host_config));

  // notify the caller that USB host is installed
  xTaskNotifyGive((TaskHandle_t)arg);

  while (true) {
    uint32_t flags;
    usb_host_lib_handle_events(portMAX_DELAY, &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
      usb_host_device_free_all();
    }
    if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
    }
  }
}

// ----------------- small helpers -----------------
bool USBTOBLEKBbridge::is_SHIFT(uint8_t mods) {
  return (mods & (HID_LEFT_SHIFT | HID_RIGHT_SHIFT)) != 0;
}

char USBTOBLEKBbridge::usage_TO_ASCII(uint8_t usage, uint8_t mods) {
  bool shift = is_SHIFT(mods);

  // Alphabet (HID_KEY_A..Z = 0x04..0x1D)
  if (usage >= HID_KEY_A && usage <= HID_KEY_Z) {
    char c = 'a' + (usage - HID_KEY_A);
    if (shift) c = (char)toupper((unsigned char)c);
    return c;
  }

  // Top row numbers (1..0) HID 0x1E..0x27
  if (usage >= HID_KEY_1 && usage <= HID_KEY_0) {
    const char normal[]  = "1234567890";
    const char shifted[] = "!@#$%^&*()";
    int idx = usage - 0x1E;
    return shift ? shifted[idx] : normal[idx];
  }

  switch (usage) {
    case HID_KEY_ENTER:        return MY_KEY_ENTER;
    case HID_KEY_ESC:          return KEY_ESC;
    case HID_KEY_TAB:          return KEY_TAB;
    case HID_KEY_SPACE:        return MY_KEY_SPACE;
    case HID_KEY_MINUS:        return (shift ? S_MY_KEY_MINUS : MY_KEY_MINUS);
    case HID_KEY_EQUAL:        return (shift ? S_MY_KEY_EQUAL : MY_KEY_EQUAL);
    case HID_KEY_OPEN_BRACKET: return (shift ? S_MY_KEY_OPEN_BRACES : MY_KEY_OPEN_BRACES);
    case HID_KEY_CLOSE_BRACKET:return (shift ? S_MY_KEY_CLOSE_BRACES : MY_KEY_CLOSE_BRACES);
    case HID_KEY_BACK_SLASH:   return (shift ? S_MY_KEY_BACK_SLASH : MY_KEY_BACK_SLASH);
    case HID_KEY_SHARP:        return (shift ? 0 : '#');
    case HID_KEY_COLON:        return (shift ? S_MY_KEY_COLON : MY_KEY_COLON);
    case HID_KEY_QUOTE:        return (shift ? S_MY_KEY_QUOTE : MY_KEY_QUOTE);
    case HID_KEY_TILDE:        return (shift ? S_MY_KEY_TILDE : MY_KEY_TILDE);
    case HID_KEY_LESS:         return (shift ? S_MY_KEY_LESS : MY_KEY_LESS);
    case HID_KEY_GREATER:      return (shift ? S_MY_KEY_GREATER : MY_KEY_GREATER);
    case HID_KEY_SLASH:        return (shift ? S_MY_KEY_SLASH : MY_KEY_SLASH);
    default:
      return 0;
  }
}

// ----------------- HID device-level event (open/start interface) -----------------
void USBTOBLEKBbridge::hid_Host_Device_EVENT(hid_host_device_handle_t hdh, const hid_host_driver_event_t event, void* arg) {
  hid_host_dev_params_t dev_params;
  ESP_ERROR_CHECK(hid_host_device_get_params(hdh, &dev_params));

  const hid_host_device_config_t dev_config = {
    .callback = hid_host_interface_callback_cwrap,
    .callback_arg = NULL
  };

  switch (event) {
    case HID_HOST_DRIVER_EVENT_CONNECTED:
      ESP_ERROR_CHECK(hid_host_device_open(hdh, &dev_config));
      if (dev_params.sub_class == HID_SUBCLASS_BOOT_INTERFACE) {
        ESP_ERROR_CHECK(hid_class_request_set_protocol(hdh, HID_REPORT_PROTOCOL_BOOT));
        if (dev_params.proto == HID_PROTOCOL_KEYBOARD) {
          ESP_ERROR_CHECK(hid_class_request_set_idle(hdh, 0, 0));
        }
      }
      ESP_ERROR_CHECK(hid_host_device_start(hdh));
      break;
    default:
      break;
  }
}

// ----------------- hid keyboard report parser -----------------
void USBTOBLEKBbridge::hid_KB_Report_CALLBACK(const uint8_t* const data, const int len) {
  if (len < (int)sizeof(hid_keyboard_input_report_boot_t)) return;
  const hid_keyboard_input_report_boot_t* KB_report_ptr = (const hid_keyboard_input_report_boot_t*)data;

  // show raw report on OLED for debugging
  {
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "RAW : 0x%02x", KB_report_ptr->modifier.val);
    for (size_t i = 0; i < HID_KEYBOARD_KEY_MAX && n < (int)sizeof(buf)-4; ++i) {
      n += snprintf(buf + n, sizeof(buf) - n, " %02x", KB_report_ptr->key[i]);
    }
  }

  static uint8_t prev[HID_KEYBOARD_KEY_MAX] = {0};
  static uint8_t prev_mods = 0;
  uint8_t curr_mods = KB_report_ptr->modifier.val;

  // releases: present in prev[] but not in current report
  for (size_t i = 0; i < HID_KEYBOARD_KEY_MAX; ++i) {
    uint8_t pk = prev[i];
    if (pk > HID_KEY_ERROR_UNDEFINED) {
      bool still = false;
      for (size_t j = 0; j < HID_KEYBOARD_KEY_MAX; ++j) {
        if (KB_report_ptr->key[j] == pk) { still = true; break; }
      }
      if (!still) {
        if (instance()) instance()->enqueueKey(pk, prev_mods, false);
      }
    }
  }

  // presses: present now but not earlier
  for (size_t i = 0; i < HID_KEYBOARD_KEY_MAX; ++i) {
    uint8_t k = KB_report_ptr->key[i];
    if (k > HID_KEY_ERROR_UNDEFINED) {
      bool was = false;
      for (size_t j = 0; j < HID_KEYBOARD_KEY_MAX; ++j) {
        if (prev[j] == k) { was = true; break; }
      }
      if (!was) {
        if (instance()) instance()->enqueueKey(k, curr_mods, true);
      }
    }
  }

  memcpy(prev, KB_report_ptr->key, HID_KEYBOARD_KEY_MAX);
  prev_mods = curr_mods;
}

// ----------------- hid mouse report -----------------
void USBTOBLEKBbridge::hid_MOUSE_Report_CALLBACK(const uint8_t *const data, const int length) {
  if (length < 3) return;
  typedef struct __attribute__((packed)) { uint8_t buttons; int8_t x; int8_t y; int8_t wheel; } hid_MOUSE_REPORT_T;
  const hid_MOUSE_REPORT_T *m = (const hid_MOUSE_REPORT_T*)data;
}

// ----------------- generic report -----------------
void USBTOBLEKBbridge::hid_Host_Generic_Report_CALLBACK(const uint8_t *const data, const int len) {
  char buf[128];
  int n = snprintf(buf, sizeof(buf), "GENERIC %d:", len);
  for (int i = 0; i < min(10, len) && n < (int)sizeof(buf) - 3; ++i) {
    n += snprintf(buf + n, sizeof(buf) - n, " %02X", data[i]);
  }
}
void USBTOBLEKBbridge::hid_host_Interface_callback_FORWARD(hid_host_device_handle_t hdh, const hid_host_interface_event_t event, void* arg)
{
  USBTOBLEKBbridge* inst = USBTOBLEKBbridge::instance();
  if (inst)
  {
    hid_Host_Interface_CALLBACK(hdh,event,arg);
  }
  
}
// ----------------- C wrapper for interface callback -----------------
extern "C" void hid_host_interface_callback_cwrap(hid_host_device_handle_t hdh, const hid_host_interface_event_t event, void* arg) {
  USBTOBLEKBbridge::hid_host_Interface_callback_FORWARD(hdh,event,arg);
}

// ----------------- interface callback (parses input reports) -----------------
void USBTOBLEKBbridge::hid_Host_Interface_CALLBACK(hid_host_device_handle_t hdh, const hid_host_interface_event_t event, void* arg) {
  uint8_t data[64]; size_t data_len = 0;
  hid_host_dev_params_t marks_params;
  ESP_ERROR_CHECK(hid_host_device_get_params(hdh, &marks_params));

  switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
      ESP_ERROR_CHECK(hid_host_device_get_raw_input_report_data(hdh, data, sizeof(data), &data_len));
      if (marks_params.sub_class == HID_SUBCLASS_BOOT_INTERFACE && marks_params.proto == HID_PROTOCOL_KEYBOARD) {
        hid_KB_Report_CALLBACK(data, (int)data_len);
      } else if (marks_params.sub_class == HID_SUBCLASS_BOOT_INTERFACE && marks_params.proto == HID_PROTOCOL_MOUSE) {
        hid_MOUSE_Report_CALLBACK(data, (int)data_len);
      } else {
        hid_Host_Generic_Report_CALLBACK(data, (int)data_len);
      }
      break;

    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
      ESP_ERROR_CHECK(hid_host_device_close(hdh));
      break;

    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
      break;

    default:
      break;
  }
}

// ----------------- NimBLE prefs -----------------
void USBTOBLEKBbridge::setNimBLE_PREF() {
  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEAdvertising* p_ble_device = NimBLEDevice::getAdvertising();
  if (p_ble_device) {
    p_ble_device->setPreferredParams(PREF_MIN_INTERVAL, PREF_MAX_INTERVAL);
    NimBLEDevice::setMTU(PREFERED_MTU);
  }
}

// ----------------- BLE consumer task -----------------
void USBTOBLEKBbridge::TASK_BLE() {
  setNimBLE_PREF();
  BleKBd.begin();

  KB_EVENT event;
  for (;;) {
    if (xQueueReceive(KBQueue, &event, portMAX_DELAY) == pdTRUE) {

      if (!BleKBd.isConnected()) continue;

      uint8_t new_mods = event.mods;
      if (new_mods != active_mods) {
        uint8_t release_mask = active_mods & ~new_mods;
        // use plain if (not else-if) so multiple modifier bits are handled
        if (release_mask & HID_LEFT_CONTROL)  BleKBd.release(KEY_LEFT_CTRL);
        if (release_mask & HID_RIGHT_CONTROL) BleKBd.release(KEY_RIGHT_CTRL);
        if (release_mask & HID_LEFT_SHIFT)    BleKBd.release(KEY_LEFT_SHIFT);
        if (release_mask & HID_RIGHT_SHIFT)   BleKBd.release(KEY_RIGHT_SHIFT);
        if (release_mask & HID_LEFT_ALT)      BleKBd.release(KEY_LEFT_ALT);
        if (release_mask & HID_RIGHT_ALT)     BleKBd.release(KEY_RIGHT_ALT);
        if (release_mask & HID_LEFT_GUI)      BleKBd.release(KEY_LEFT_GUI);
        if (release_mask & HID_RIGHT_GUI)     BleKBd.release(KEY_RIGHT_GUI);

        uint8_t press_mask = new_mods & ~active_mods;
        if (press_mask & HID_LEFT_CONTROL)  BleKBd.press(KEY_LEFT_CTRL);
        if (press_mask & HID_RIGHT_CONTROL) BleKBd.press(KEY_RIGHT_CTRL);
        if (press_mask & HID_LEFT_SHIFT)    BleKBd.press(KEY_LEFT_SHIFT);
        if (press_mask & HID_RIGHT_SHIFT)   BleKBd.press(KEY_RIGHT_SHIFT);
        if (press_mask & HID_LEFT_ALT)      BleKBd.press(KEY_LEFT_ALT);
        if (press_mask & HID_RIGHT_ALT)     BleKBd.press(KEY_RIGHT_ALT);
        if (press_mask & HID_LEFT_GUI)      BleKBd.press(KEY_LEFT_GUI);
        if (press_mask & HID_RIGHT_GUI)     BleKBd.press(KEY_RIGHT_GUI);

        active_mods = new_mods;
      }

      if (event.usage == 0) continue;

      char ch = usage_TO_ASCII(event.usage, event.mods);
      if (ch) {
        if (event.pressed) BleKBd.write(ch);
      } else {
        if (event.pressed) {
          switch (event.usage) {
            case HID_KEY_ENTER:      BleKBd.write(MY_KEY_ENTER); break;
            case HID_KEY_ESC:        BleKBd.write(KEY_ESC); break;
            case HID_KEY_CAPS_LOCK:  BleKBd.write(KEY_CAPS_LOCK); break;
            case HID_KEY_DEL:        BleKBd.write(KEY_BACKSPACE); break;
            case HID_KEY_TAB:        BleKBd.write(KEY_TAB); break;
            case HID_KEY_F1:         BleKBd.press(KEY_F1), BleKBd.release(KEY_F1); break;
            case HID_KEY_F2:         BleKBd.press(KEY_F2), BleKBd.release(KEY_F2); break;
            case HID_KEY_F3:         BleKBd.press(KEY_F3), BleKBd.release(KEY_F3); break;
            case HID_KEY_F4:         BleKBd.press(KEY_F4), BleKBd.release(KEY_F4); break;
            case HID_KEY_F5:         BleKBd.press(KEY_F5), BleKBd.release(KEY_F5); break;
            case HID_KEY_F6:         BleKBd.press(KEY_F6), BleKBd.release(KEY_F6); break;
            case HID_KEY_F7:         BleKBd.press(KEY_F7), BleKBd.release(KEY_F7); break;
            case HID_KEY_F8:         BleKBd.press(KEY_F8), BleKBd.release(KEY_F8); break;
            case HID_KEY_F9:         BleKBd.press(KEY_F9), BleKBd.release(KEY_F9); break;
            case HID_KEY_F10:        BleKBd.press(KEY_F10), BleKBd.release(KEY_F10); break;
            case HID_KEY_F11:        BleKBd.press(KEY_F11), BleKBd.release(KEY_F11); break;
            case HID_KEY_F12:        BleKBd.press(KEY_F12), BleKBd.release(KEY_F12); break;
            case HID_KEY_LEFT:       BleKBd.press(KEY_LEFT_ARROW), BleKBd.release(KEY_LEFT_ARROW); break;
            case HID_KEY_RIGHT:      BleKBd.press(KEY_RIGHT_ARROW), BleKBd.release(KEY_RIGHT_ARROW); break;
            case HID_KEY_UP:         BleKBd.press(KEY_UP_ARROW), BleKBd.release(KEY_UP_ARROW); break;
            case HID_KEY_DOWN:       BleKBd.press(KEY_DOWN_ARROW), BleKBd.release(KEY_DOWN_ARROW); break;
            default:
              break;
          }
        }
      }
    } // xQueueReceive
  } // for
}
