// src/main.cpp
// USB HID Host -> NimBLE Keyboard bridge (ESP32-S3, Arduino on PlatformIO)
// Modular class-based bridge. Uses OledLogger for feedback only.

#include <Arduino.h>
#include <BleKeyboard.h>
#include <NimBLEDevice.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "OledLogger.h"
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

// --- Compatibility: ensure modifier macros used in code exist ---
// Place this right AFTER including hid_usage_keyboard.h

#if defined(HID_LEFT_CONTROL) && !defined(HID_LEFT_CTRL)
  #define HID_LEFT_CTRL HID_LEFT_CONTROL
#endif
#if defined(HID_RIGHT_CONTROL) && !defined(HID_RIGHT_CTRL)
  #define HID_RIGHT_CTRL HID_RIGHT_CONTROL
#endif

#ifndef HID_LEFT_CTRL
  #define HID_LEFT_CTRL   (1U << 0)
#endif
#ifndef HID_LEFT_SHIFT
  #define HID_LEFT_SHIFT  (1U << 1)
#endif
#ifndef HID_LEFT_ALT
  #define HID_LEFT_ALT    (1U << 2)
#endif
#ifndef HID_LEFT_GUI
  #define HID_LEFT_GUI    (1U << 3)
#endif
#ifndef HID_RIGHT_CTRL
  #define HID_RIGHT_CTRL  (1U << 4)
#endif
#ifndef HID_RIGHT_SHIFT
  #define HID_RIGHT_SHIFT (1U << 5)
#endif
#ifndef HID_RIGHT_ALT
  #define HID_RIGHT_ALT   (1U << 6)
#endif
#ifndef HID_RIGHT_GUI
  #define HID_RIGHT_GUI   (1U << 7)
#endif

#ifndef HID_LEFT_CONTROL
  #define HID_LEFT_CONTROL HID_LEFT_CTRL
#endif
#ifndef HID_RIGHT_CONTROL
  #define HID_RIGHT_CONTROL HID_RIGHT_CTRL
#endif

#ifndef KEY_ENTER
  #if defined(HID_KEY_ENTER)
    #define KEY_ENTER HID_KEY_ENTER
  #else
    #define KEY_ENTER '\r'
  #endif
#endif

// -------------------- small types --------------------
typedef struct {
  uint8_t usage;   // HID usage id (0 if none)
  uint8_t mods;    // modifier byte from boot report
  bool    pressed; // true = key down
} KeyEvent;

// Forward declaration of C wrapper for HID driver callback (we install this as the callback)
extern "C" void hid_host_device_callback_cwrap(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg);

// -------------------- Bridge class --------------------
class USB2BLEBridge {
public:
  USB2BLEBridge()
    : keyQueue(nullptr), blekbd(BLE_DEVICE_NAME), bleTaskHandle(nullptr), active_mods(0),
      hid_host_event_queue(nullptr) {}

  bool begin() {
    // init oled first
    if (!OledLogger::begin(0x3C, 128, 64, 8, 9, 16 /*queue len*/)) {
      return false;
    }
    OledLogger::logf("Oled ready");

    keyQueue = xQueueCreate(KEYQUEUE_DEPTH, sizeof(KeyEvent));
    if (!keyQueue) { OledLogger::logf("Key queue create failed"); return false; }

    // start BLE consumer task
    xTaskCreatePinnedToCore(task_ble_wrapper, "BLETask", BLE_TASK_STACK, this, BLE_TASK_PRIO, &bleTaskHandle, 1);

    // start USB host lib task (synchronize)
    BaseType_t ok = xTaskCreatePinnedToCore(task_usb_lib_wrapper, "usb_events", 4096, xTaskGetCurrentTaskHandle(), 2, NULL, 0);
    if (ok != pdTRUE) OledLogger::logf("usb_events create failed");

    // wait for USB host init (usb_lib_task will notify)
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(3000));

    // hid host driver install - use the C wrapper as callback to avoid linkage issues
    const hid_host_driver_config_t hid_cfg = {
      .create_background_task = true,
      .task_priority = 8,
      .stack_size = 8192,
      .core_id = 0,
      .callback = hid_host_device_callback_cwrap,
      .callback_arg = NULL
    };
    ESP_ERROR_CHECK(hid_host_install(&hid_cfg));

    // create event queue for hid_worker
    hid_host_event_queue = xQueueCreate(10, sizeof(hid_host_event_queue_t));
    xTaskCreate(hid_worker_wrapper, "hid_worker", 4096, this, 2, NULL);

    OledLogger::logf("Setup complete. Plug keyboard into S3 host port.");
    return true;
  }

  // Called by HID parsing to enqueue a logical key event
  void enqueueKey(uint8_t usage, uint8_t mods, bool pressed) {
    if (!keyQueue) return;
    KeyEvent ev { usage, mods, pressed };

    BaseType_t inIsr = pdFALSE;
    #if defined(xPortIsInsideInterrupt)
      inIsr = xPortIsInsideInterrupt();
    #elif defined(xPortInIsrContext)
      inIsr = xPortInIsrContext();
    #endif

    if (inIsr) {
      BaseType_t woken = pdFALSE;
      xQueueSendFromISR(keyQueue, &ev, &woken);
      portYIELD_FROM_ISR(woken);
    } else {
      xQueueSend(keyQueue, &ev, 0);
    }
  }

  // -------------------- static C wrappers --------------------
  static void task_ble_wrapper(void* pv) {
    USB2BLEBridge* inst = static_cast<USB2BLEBridge*>(pv);
    inst->task_ble();
  }
  static void task_usb_lib_wrapper(void* pv) {
    // pv is the caller task handle (for notification)
    USB2BLEBridge::usb_lib_task(pv);
  }
  static void hid_worker_wrapper(void* pv) {
    USB2BLEBridge* inst = static_cast<USB2BLEBridge*>(pv);
    inst->hid_worker_task();
  }

  // HID host callback wrapper invoked by driver (this is called from C wrapper)
  static void hid_host_device_callback(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg) {
    if (!instance()) return;
    hid_host_event_queue_t ev = { .hid_device_handle = hid_device_handle, .event = event, .arg = arg };
    xQueueSend(instance()->hid_host_event_queue, &ev, 0);
  }

  // -------------------- instance helpers --------------------
  static USB2BLEBridge* instance();               // declared; defined once below
  static void set_instance(USB2BLEBridge* p) { s_inst_ptr = p; }

private:
  // internal state
  QueueHandle_t keyQueue;
  BleKeyboard   blekbd;
  TaskHandle_t  bleTaskHandle;
  uint8_t       active_mods; // currently applied BLE modifiers (bitmask)

  // hid host event queue type
  typedef struct {
    hid_host_device_handle_t hid_device_handle;
    hid_host_driver_event_t  event;
    void *arg;
  } hid_host_event_queue_t;

  QueueHandle_t hid_host_event_queue;

  // static pointer for wrappers (declared here, defined once below)
  static USB2BLEBridge* s_inst_ptr;

  // -------------------- BLE consumer task (instance) --------------------
  void task_ble() {
    // init nimble preferences and start BLE keyboard
    setNimBLEPref();
    blekbd.begin();
    OledLogger::logf("[BLE] advertising");

    KeyEvent ev;
    for (;;) {
      if (xQueueReceive(keyQueue, &ev, portMAX_DELAY) == pdTRUE) {
        OledLogger::logf("EVENT use=0x%02X mods=0x%02X press=%d", ev.usage, ev.mods, ev.pressed);

        if (!blekbd.isConnected()) continue;

        uint8_t new_mods = ev.mods;
        if (new_mods != active_mods) {
          uint8_t release_mask = active_mods & ~new_mods;
          if (release_mask & HID_LEFT_CTRL)  blekbd.release(KEY_LEFT_CTRL);
          if (release_mask & HID_RIGHT_CTRL) blekbd.release(KEY_RIGHT_CTRL);
          if (release_mask & HID_LEFT_SHIFT) blekbd.release(KEY_LEFT_SHIFT);
          if (release_mask & HID_RIGHT_SHIFT) blekbd.release(KEY_RIGHT_SHIFT);
          if (release_mask & HID_LEFT_ALT)   blekbd.release(KEY_LEFT_ALT);
          if (release_mask & HID_RIGHT_ALT)  blekbd.release(KEY_RIGHT_ALT);
          if (release_mask & HID_LEFT_GUI)   blekbd.release(KEY_LEFT_GUI);
          if (release_mask & HID_RIGHT_GUI)  blekbd.release(KEY_RIGHT_GUI);

          uint8_t press_mask = new_mods & ~active_mods;
          if (press_mask & HID_LEFT_CTRL)  blekbd.press(KEY_LEFT_CTRL);
          if (press_mask & HID_RIGHT_CTRL) blekbd.press(KEY_RIGHT_CTRL);
          if (press_mask & HID_LEFT_SHIFT) blekbd.press(KEY_LEFT_SHIFT);
          if (press_mask & HID_RIGHT_SHIFT) blekbd.press(KEY_RIGHT_SHIFT);
          if (press_mask & HID_LEFT_ALT)   blekbd.press(KEY_LEFT_ALT);
          if (press_mask & HID_RIGHT_ALT)  blekbd.press(KEY_RIGHT_ALT);
          if (press_mask & HID_LEFT_GUI)   blekbd.press(KEY_LEFT_GUI);
          if (press_mask & HID_RIGHT_GUI)  blekbd.press(KEY_RIGHT_GUI);

          active_mods = new_mods;
        }

        if (ev.usage == 0) continue;

        char ch = usageToAscii(ev.usage, ev.mods);
        if (ch) {
          if (ev.pressed) blekbd.write(ch);
        } else {
          if (ev.pressed) {
            switch (ev.usage) {
              case 0x28: blekbd.write(KEY_ENTER); break;
              case 0x29: blekbd.write(0x1B); break;
              case 0x2a: blekbd.write(KEY_BACKSPACE); break;
              case 0x2b: blekbd.write(KEY_TAB); break;
              case 0x3A: blekbd.press(KEY_F1); blekbd.release(KEY_F1); break;
              case 0x3B: blekbd.press(KEY_F2); blekbd.release(KEY_F2); break;
              case 0x3C: blekbd.press(KEY_F3); blekbd.release(KEY_F3); break;
              case 0x3D: blekbd.press(KEY_F4); blekbd.release(KEY_F4); break;
              case 0x3E: blekbd.press(KEY_F5); blekbd.release(KEY_F5); break;
              case 0x3F: blekbd.press(KEY_F6); blekbd.release(KEY_F6); break;
              case 0x40: blekbd.press(KEY_F7); blekbd.release(KEY_F7); break;
              case 0x41: blekbd.press(KEY_F8); blekbd.release(KEY_F8); break;
              case 0x42: blekbd.press(KEY_F9); blekbd.release(KEY_F9); break;
              case 0x43: blekbd.press(KEY_F10); blekbd.release(KEY_F10); break;
              case 0x44: blekbd.press(KEY_F11); blekbd.release(KEY_F11); break;
              case 0x45: blekbd.press(KEY_F12); blekbd.release(KEY_F12); break;
              case 0x4F: blekbd.press(KEY_RIGHT_ARROW), blekbd.release(KEY_RIGHT_ARROW); break;
              case 0x50: blekbd.press(KEY_LEFT_ARROW),  blekbd.release(KEY_LEFT_ARROW); break;
              case 0x51: blekbd.press(KEY_DOWN_ARROW),  blekbd.release(KEY_DOWN_ARROW); break;
              case 0x52: blekbd.press(KEY_UP_ARROW),    blekbd.release(KEY_UP_ARROW); break;
              default:
                OledLogger::logf("Unknown usage: 0x%02X", ev.usage);
                break;
            }
          }
        }
      }
    }
  }

  // -------------------- hid worker task (instance) --------------------
  void hid_worker_task() {
    hid_host_event_queue_t ev;
    while (true) {
      if (xQueueReceive(hid_host_event_queue, &ev, pdMS_TO_TICKS(50))) {
        // call handler that opens interface / starts transfers
        hid_host_device_event(ev.hid_device_handle, ev.event, ev.arg);
      }
    }
  }

  // -------------------- usb lib task (static function used as task) --------------------
  static void usb_lib_task(void* arg) {
    const usb_host_config_t host_config = { .skip_phy_setup = false, .intr_flags = ESP_INTR_FLAG_LEVEL1 };
    ESP_ERROR_CHECK(usb_host_install(&host_config));
    // notify caller (arg is the caller task handle)
    xTaskNotifyGive((TaskHandle_t)arg);

    while (true) {
      uint32_t flags;
      usb_host_lib_handle_events(portMAX_DELAY, &flags);
      if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
        usb_host_device_free_all();
        OledLogger::logf("USB: NO_CLIENTS");
      }
      if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
        OledLogger::logf("USB: ALL_FREE");
      }
    }
  }

  // -------------------- helper: usage -> ascii (US QWERTY) --------------------
  static inline bool is_shift(uint8_t mods) {
    return (mods & (HID_LEFT_SHIFT | HID_RIGHT_SHIFT)) != 0;
  }
  static char usageToAscii(uint8_t usage, uint8_t mods) {
    bool shift = is_shift(mods);
    if (usage >= 0x04 && usage <= 0x1d) {
      char c = 'a' + (usage - 0x04);
      if (shift) c = (char)toupper(c);
      return c;
    }
    if (usage >= 0x1e && usage <= 0x27) {
      const char normal[]  = "1234567890";
      const char shifted[] = "!@#$%^&*()";
      int idx = usage - 0x1e;
      return shift ? shifted[idx] : normal[idx];
    }
    switch (usage) {
      case 0x28: return '\r';
      case 0x29: return 0x1B;
      case 0x2b: return '\t';
      case 0x2c: return ' ';
      case 0x2d: return (shift ? '_' : '-');
      case 0x2e: return (shift ? '+' : '=');
      case 0x2f: return (shift ? '{' : '[');
      case 0x30: return (shift ? '}' : ']');
      case 0x31: return (shift ? '|' : '\\');
      case 0x32: return (shift ? 0 : '#');
      case 0x33: return (shift ? ':' : ';');
      case 0x34: return (shift ? '"' : '\'');
      case 0x35: return (shift ? '~' : '`');
      case 0x36: return (shift ? '<' : ',');
      case 0x37: return (shift ? '>' : '.');
      case 0x38: return (shift ? '?' : '/');
      default: return 0;
    }
  }

  // -------------------- HID device-level event (same as earlier) --------------------
  static void hid_host_device_event(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg) {
    hid_host_dev_params_t dev_params;
    ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));

    const hid_host_device_config_t dev_config = {
      .callback = hid_host_interface_callback,
      .callback_arg = NULL
    };

    switch (event) {
      case HID_HOST_DRIVER_EVENT_CONNECTED:
        OledLogger::logf("HID connected proto=%d", dev_params.proto);
        ESP_ERROR_CHECK(hid_host_device_open(hid_device_handle, &dev_config));
        if (dev_params.sub_class == HID_SUBCLASS_BOOT_INTERFACE) {
          ESP_ERROR_CHECK(hid_class_request_set_protocol(hid_device_handle, HID_REPORT_PROTOCOL_BOOT));
          if (dev_params.proto == HID_PROTOCOL_KEYBOARD) {
            ESP_ERROR_CHECK(hid_class_request_set_idle(hid_device_handle, 0, 0));
          }
        }
        ESP_ERROR_CHECK(hid_host_device_start(hid_device_handle));
        break;
      default:
        break;
    }
  }

  // -------------------- interface callback (parses reports) --------------------
  static void hid_host_interface_callback(hid_host_device_handle_t hid_device_handle, const hid_host_interface_event_t event, void *arg) {
    uint8_t data[64]; size_t data_length = 0;
    hid_host_dev_params_t params;
    ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &params));

    switch (event) {
      case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
        ESP_ERROR_CHECK(hid_host_device_get_raw_input_report_data(hid_device_handle, data, sizeof(data), &data_length));
        if (params.sub_class == HID_SUBCLASS_BOOT_INTERFACE && params.proto == HID_PROTOCOL_KEYBOARD) {
          hid_host_keyboard_report_callback(data, (int)data_length);
        } else if (params.sub_class == HID_SUBCLASS_BOOT_INTERFACE && params.proto == HID_PROTOCOL_MOUSE) {
          hid_host_mouse_report_callback(data, (int)data_length);
        } else {
          hid_host_generic_report_callback(data, (int)data_length);
        }
        break;
      case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        OledLogger::logf("HID device disconnected");
        ESP_ERROR_CHECK(hid_host_device_close(hid_device_handle));
        break;
      case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        OledLogger::logf("HID transfer error");
        break;
      default:
        OledLogger::logf("Unhandled interface event");
        break;
    }
  }

  // -------------------- parse boot keyboard report --------------------
  static void hid_host_keyboard_report_callback(const uint8_t *const data, const int length) {
    if (length < (int)sizeof(hid_keyboard_input_report_boot_t)) return;
    const hid_keyboard_input_report_boot_t *kb = (const hid_keyboard_input_report_boot_t *)data;

    // debug: show raw report on OLED
    {
      char buf[64];
      int n = snprintf(buf, sizeof(buf), "RAW mods=0x%02X keys:", kb->modifier.val);
      for (int i=0;i<HID_KEYBOARD_KEY_MAX && n < (int)sizeof(buf)-4;i++) n += snprintf(buf + n, sizeof(buf)-n, " %02X", kb->key[i]);
      OledLogger::logf("%s", buf);
    }

    static uint8_t prev[HID_KEYBOARD_KEY_MAX] = {0};
    static uint8_t prev_mods = 0;
    uint8_t curr_mods = kb->modifier.val;

    // Releases: present in prev but not in current
    for (int i = 0; i < HID_KEYBOARD_KEY_MAX; ++i) {
      uint8_t pk = prev[i];
      if (pk > HID_KEY_ERROR_UNDEFINED) {
        bool still = false;
        for (int j = 0; j < HID_KEYBOARD_KEY_MAX; ++j) {
          if (kb->key[j] == pk) { still = true; break; }
        }
        if (!still) {
          if (instance()) instance()->enqueueKey(pk, prev_mods, false);
        }
      }
    }

    // Presses: present in current but not in prev
    for (int i = 0; i < HID_KEYBOARD_KEY_MAX; ++i) {
      uint8_t k = kb->key[i];
      if (k > HID_KEY_ERROR_UNDEFINED) {
        bool was = false;
        for (int j = 0; j < HID_KEYBOARD_KEY_MAX; ++j) {
          if (prev[j] == k) { was = true; break; }
        }
        if (!was) {
          if (instance()) instance()->enqueueKey(k, curr_mods, true);
        }
      }
    }

    memcpy(prev, kb->key, HID_KEYBOARD_KEY_MAX);
    prev_mods = curr_mods;
  }

  static void hid_host_mouse_report_callback(const uint8_t *const data, const int length) {
    if (length < 3) return;
    typedef struct __attribute__((packed)) { uint8_t buttons; int8_t x; int8_t y; int8_t wheel; } hid_mouse_report_t;
    const hid_mouse_report_t *m = (const hid_mouse_report_t*)data;
    OledLogger::logf("Mouse b:%02x x:%d y:%d w:%d", m->buttons, m->x, m->y, m->wheel);
  }

  static void hid_host_generic_report_callback(const uint8_t *const data, const int length) {
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "GENERIC %d:", length);
    for (int i=0;i<min(10,length) && n < (int)sizeof(buf)-3;++i) n += snprintf(buf + n, sizeof(buf)-n, " %02X", data[i]);
    OledLogger::logf("%s", buf);
  }

  // -------------------- other helpers --------------------
  static void setNimBLEPref() {
    const uint16_t PREF_MIN_INTERVAL = 12;
    const uint16_t PREF_MAX_INTERVAL = 24;
    const uint16_t PREFERRED_MTU     = 247;

    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEAdvertising *pAdv = NimBLEDevice::getAdvertising();
    if (pAdv) pAdv->setPreferredParams(PREF_MIN_INTERVAL, PREF_MAX_INTERVAL);
    NimBLEDevice::setMTU(PREFERRED_MTU);
  }
};

// -------------------- static singleton pointer and instance() definition ----
USB2BLEBridge* USB2BLEBridge::s_inst_ptr = nullptr;
USB2BLEBridge* USB2BLEBridge::instance() {
  return s_inst_ptr;
}

// -------------------- C-style wrapper for hid_host_device_callback required by HID driver ----
extern "C" void hid_host_device_callback_cwrap(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg) {
  // forward to class static handler
  USB2BLEBridge::hid_host_device_callback(hid_device_handle, event, arg);
}

// -------------------- instantiate bridge singleton ----
static USB2BLEBridge g_bridge;

// -------------------- Arduino entry points --------------------
void setup() {
  delay(50);
  // initialize bridge and set instance pointer
  USB2BLEBridge::set_instance(&g_bridge);
  if (!g_bridge.begin()) {
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
