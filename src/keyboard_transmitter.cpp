// // src/main.cpp
// // USB HID Host -> NimBLE Keyboard bridge (ESP32-S3, Arduino on PlatformIO)
// // Requires: NimBLE-Arduino, ESP32-BLE-Keyboard (T-vK or NimBLE variant), ESP32_USB_Host_HID
// // platformio.ini should contain -DUSE_NIMBLE and appropriate lib_deps.

// #include <Arduino.h>
// #include <BleKeyboard.h>
// #include <NimBLEDevice.h>

// #include <freertos/FreeRTOS.h>
// #include <freertos/queue.h>
// #include <freertos/task.h>

// #include "usb/usb_host.h"
// #include "hid_host.h"
// #include "hid_usage_keyboard.h"
// #include "hid_usage_mouse.h"

// #ifndef KEY_ENTER
//   #define KEY_ENTER '\r'
// #endif
// #ifndef KEY_BACKSPACE
//   #define KEY_BACKSPACE '\b'
// #endif
// #ifndef KEY_TAB
//   #define KEY_TAB '\t'
// #endif
// #ifndef KEY_LEFT_ARROW
//   #define KEY_LEFT_ARROW  0x50
// #endif
// #ifndef KEY_RIGHT_ARROW
//   #define KEY_RIGHT_ARROW 0x4F
// #endif

// #define BLE_DEVICE_NAME   "ESP_USB2BLE"
// #define KEYQUEUE_DEPTH    256
// #define BLE_TASK_STACK    4096
// #define BLE_TASK_PRIO     6

// typedef struct {
//   uint8_t usage;
//   uint8_t mods;
//   bool    pressed;
// } KeyEvent;

// static QueueHandle_t keyQueue = NULL;
// static BleKeyboard blekbd(BLE_DEVICE_NAME);
// static TaskHandle_t bleTaskHandle = NULL;

// // fallback modifiers
// #ifndef HID_LEFT_CTRL
//   #define HID_LEFT_CTRL   0x01
//   #define HID_LEFT_SHIFT  0x02
//   #define HID_LEFT_ALT    0x04
//   #define HID_LEFT_GUI    0x08
//   #define HID_RIGHT_CTRL  0x10
//   #define HID_RIGHT_SHIFT 0x20
//   #define HID_RIGHT_ALT   0x40
//   #define HID_RIGHT_GUI   0x80
// #endif

// static inline bool is_shift(uint8_t mod) {
//   return (mod & (HID_LEFT_SHIFT | HID_RIGHT_SHIFT)) != 0;
// }

// static char usageToAscii(uint8_t usage, uint8_t mods) {
//   if (usage >= 0x04 && usage <= 0x1d) {
//     char c = 'a' + (usage - 0x04);
//     if (is_shift(mods)) c = toupper(c);
//     return c;
//   }
//   if (usage >= 0x1e && usage <= 0x27) {
//     const char base[] = "1234567890";
//     int idx = usage - 0x1e;
//     if (is_shift(mods)) {
//       const char shifted[] = "!@#$%^&*()";
//       return shifted[idx];
//     } else return base[idx];
//   }
//   if (usage == 0x2c) return ' ';
//   return 0;
// }

// // ISR-safe enqueue
// void enqueueKey(uint8_t usage, uint8_t mods, bool pressed) {
//   if (!keyQueue) return;
//   KeyEvent ev { usage, mods, pressed };
//   BaseType_t inIsr = pdFALSE;
//   #if defined(xPortIsInsideInterrupt)
//     inIsr = xPortIsInsideInterrupt();
//   #elif defined(xPortInIsrContext)
//     inIsr = xPortInIsrContext();
//   #endif

//   if (inIsr) {
//     BaseType_t woken = pdFALSE;
//     xQueueSendFromISR(keyQueue, &ev, &woken);
//     portYIELD_FROM_ISR(woken);
//   } else {
//     xQueueSend(keyQueue, &ev, 0);
//   }
// }

// // BLE task
// void bleTask(void* pv) {
//   (void)pv;
//   Serial.println("[BLE] init");
//   blekbd.begin();
//   Serial.println("[BLE] advertising");

//   for (;;) {
//     KeyEvent ev;
//     if (xQueueReceive(keyQueue, &ev, portMAX_DELAY) == pdTRUE) {
//       if (!blekbd.isConnected()) continue; // drop until connected (low latency)
//       // modifiers
//       if (ev.pressed) {
//         if (ev.mods & HID_LEFT_CTRL)  blekbd.press(KEY_LEFT_CTRL);
//         if (ev.mods & HID_RIGHT_CTRL) blekbd.press(KEY_RIGHT_CTRL);
//         if (ev.mods & HID_LEFT_SHIFT) blekbd.press(KEY_LEFT_SHIFT);
//         if (ev.mods & HID_RIGHT_SHIFT) blekbd.press(KEY_RIGHT_SHIFT);
//         if (ev.mods & HID_LEFT_ALT)   blekbd.press(KEY_LEFT_ALT);
//         if (ev.mods & HID_RIGHT_ALT)  blekbd.press(KEY_RIGHT_ALT);
//         if (ev.mods & HID_LEFT_GUI)   blekbd.press(KEY_LEFT_GUI);
//         if (ev.mods & HID_RIGHT_GUI)  blekbd.press(KEY_RIGHT_GUI);
//       } else {
//         if (ev.mods & HID_LEFT_CTRL)  blekbd.release(KEY_LEFT_CTRL);
//         if (ev.mods & HID_RIGHT_CTRL) blekbd.release(KEY_RIGHT_CTRL);
//         if (ev.mods & HID_LEFT_SHIFT) blekbd.release(KEY_LEFT_SHIFT);
//         if (ev.mods & HID_RIGHT_SHIFT) blekbd.release(KEY_RIGHT_SHIFT);
//         if (ev.mods & HID_LEFT_ALT)   blekbd.release(KEY_LEFT_ALT);
//         if (ev.mods & HID_RIGHT_ALT)  blekbd.release(KEY_RIGHT_ALT);
//         if (ev.mods & HID_LEFT_GUI)   blekbd.release(KEY_LEFT_GUI);
//         if (ev.mods & HID_RIGHT_GUI)  blekbd.release(KEY_RIGHT_GUI);
//       }

//       char ch = usageToAscii(ev.usage, ev.mods);
//       if (ch && ev.pressed) {
//         blekbd.write(ch);
//       } else {
//         if (ev.pressed) {
//           switch (ev.usage) {
//             case 0x28: blekbd.write(KEY_ENTER); break;
//             case 0x2a: blekbd.write(KEY_BACKSPACE); break;
//             case 0x2b: blekbd.write(KEY_TAB); break;
//             case 0x4f: blekbd.press(KEY_RIGHT_ARROW), blekbd.release(KEY_RIGHT_ARROW); break;
//             case 0x50: blekbd.press(KEY_LEFT_ARROW),  blekbd.release(KEY_LEFT_ARROW); break;
//             default: break;
//           }
//         }
//       }
//       taskYIELD();
//     }
//   }
// }

// // Print header helper
// static void hid_print_new_device_report_header(hid_protocol_t proto) {
//   static hid_protocol_t prev_proto = HID_PROTOCOL_MAX;
//   if (prev_proto != proto) {
//     prev_proto = proto;
//     Serial.println();
//     if (proto == HID_PROTOCOL_MOUSE) Serial.println("Mouse");
//     else if (proto == HID_PROTOCOL_KEYBOARD) Serial.println("Keyboard");
//     else Serial.println("Generic");
//   }
// }

// // Called from parser when new logical press/release detected
// static void key_event_callback(bool pressed, uint8_t modifier, uint8_t key_code) {
//   if (pressed) {
//     char ch = usageToAscii(key_code, modifier);
//     if (ch) {
//       Serial.print(ch);
//       if (ch == '\r') Serial.println();
//     }
//   }
//   enqueueKey(key_code, modifier, pressed);
// }

// // parse boot keyboard report
// static void hid_host_keyboard_report_callback(const uint8_t *const data, const int length) {
//   if (length < sizeof(hid_keyboard_input_report_boot_t)) return;
//   const hid_keyboard_input_report_boot_t *kb = (const hid_keyboard_input_report_boot_t *)data;
//   static uint8_t prev[HID_KEYBOARD_KEY_MAX] = {0};

//   for (int i=0;i<HID_KEYBOARD_KEY_MAX;i++) {
//     uint8_t pk = prev[i];
//     if (pk > HID_KEY_ERROR_UNDEFINED) {
//       bool still = false;
//       for (int j=0;j<HID_KEYBOARD_KEY_MAX;j++) if (kb->key[j] == pk) { still=true; break; }
//       if (!still) enqueueKey(pk, 0, false);
//     }
//   }

//   for (int i=0;i<HID_KEYBOARD_KEY_MAX;i++) {
//     uint8_t k = kb->key[i];
//     if (k > HID_KEY_ERROR_UNDEFINED) {
//       bool was = false;
//       for (int j=0;j<HID_KEYBOARD_KEY_MAX;j++) if (prev[j] == k) { was=true; break; }
//       if (!was) enqueueKey(k, kb->modifier.val, true);
//     }
//   }

//   memcpy(prev, kb->key, HID_KEYBOARD_KEY_MAX);
// }

// static void hid_host_mouse_report_callback(const uint8_t *const data, const int length) {
//   if (length < 3) return;
//   typedef struct __attribute__((packed)) { uint8_t buttons; int8_t x; int8_t y; int8_t wheel; } hid_mouse_report_t;
//   const hid_mouse_report_t *m = (const hid_mouse_report_t*)data;
//   hid_print_new_device_report_header(HID_PROTOCOL_MOUSE);
//   Serial.printf("mouse b:%02x x:%d y:%d w:%d\n", m->buttons, m->x, m->y, m->wheel);
// }

// static void hid_host_generic_report_callback(const uint8_t *const data, const int length) {
//   hid_print_new_device_report_header(HID_PROTOCOL_NONE);
//   Serial.printf("GENERIC %d:", length);
//   for (int i=0;i<min(10,length);++i) Serial.printf("%02X", data[i]);
//   Serial.println();
// }

// // Interface callback (USB host library will call)
// void hid_host_interface_callback(hid_host_device_handle_t hid_device_handle, const hid_host_interface_event_t event, void *arg) {
//   uint8_t data[64]; size_t data_length = 0;
//   hid_host_dev_params_t params;
//   ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &params));

//   switch (event) {
//     case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
//       ESP_ERROR_CHECK(hid_host_device_get_raw_input_report_data(hid_device_handle, data, sizeof(data), &data_length));
//       if (params.sub_class == HID_SUBCLASS_BOOT_INTERFACE && params.proto == HID_PROTOCOL_KEYBOARD) {
//         hid_host_keyboard_report_callback(data, data_length);
//       } else if (params.sub_class == HID_SUBCLASS_BOOT_INTERFACE && params.proto == HID_PROTOCOL_MOUSE) {
//         hid_host_mouse_report_callback(data, data_length);
//       } else {
//         hid_host_generic_report_callback(data, data_length);
//       }
//       break;
//     case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
//       Serial.println("HID device disconnected");
//       ESP_ERROR_CHECK(hid_host_device_close(hid_device_handle));
//       break;
//     case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
//       Serial.println("HID transfer error");
//       break;
//     default:
//       Serial.println("Unhandled interface event");
//       break;
//   }
// }

// // ---> THIS WAS MISSING: device-level handler that opens the interface and starts it
// void hid_host_device_event(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg) {
//   hid_host_dev_params_t dev_params;
//   ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));

//   const hid_host_device_config_t dev_config = {
//     .callback = hid_host_interface_callback,
//     .callback_arg = NULL
//   };

//   switch (event) {
//     case HID_HOST_DRIVER_EVENT_CONNECTED:
//       Serial.printf("HID Device connected, proto %d\r\n", dev_params.proto);
//       ESP_ERROR_CHECK(hid_host_device_open(hid_device_handle, &dev_config));
//       if (dev_params.sub_class == HID_SUBCLASS_BOOT_INTERFACE) {
//         ESP_ERROR_CHECK(hid_class_request_set_protocol(hid_device_handle, HID_REPORT_PROTOCOL_BOOT));
//         if (dev_params.proto == HID_PROTOCOL_KEYBOARD) {
//           ESP_ERROR_CHECK(hid_class_request_set_idle(hid_device_handle, 0, 0));
//         }
//       }
//       ESP_ERROR_CHECK(hid_host_device_start(hid_device_handle));
//       break;
//     default:
//       break;
//   }
// }

// // hid_host event queue type & callback
// typedef struct {
//   hid_host_device_handle_t hid_device_handle;
//   hid_host_driver_event_t event;
//   void *arg;
// } hid_host_event_queue_t;

// static QueueHandle_t hid_host_event_queue = NULL;
// void hid_host_device_callback(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg) {
//   hid_host_event_queue_t ev = { .hid_device_handle = hid_device_handle, .event = event, .arg = arg };
//   xQueueSend(hid_host_event_queue, &ev, 0);
// }

// // USB library task
// static void usb_lib_task(void* arg) {
//   const usb_host_config_t host_config = { .skip_phy_setup = false, .intr_flags = ESP_INTR_FLAG_LEVEL1 };
//   ESP_ERROR_CHECK(usb_host_install(&host_config));
//   xTaskNotifyGive((TaskHandle_t)arg);

//   while (true) {
//     uint32_t flags;
//     usb_host_lib_handle_events(portMAX_DELAY, &flags);
//     if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
//       usb_host_device_free_all();
//       Serial.println("USB: NO_CLIENTS");
//     }
//     if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
//       Serial.println("USB: ALL_FREE");
//     }
//   }
// }

// // hid worker task - dispatches events into our device event handler
// static void hid_worker_task(void* pv) {
//   hid_host_event_queue_t ev;
//   while (true) {
//     if (xQueueReceive(hid_host_event_queue, &ev, pdMS_TO_TICKS(50))) {
//       hid_host_device_event(ev.hid_device_handle, ev.event, ev.arg);
//     }
//   }
// }

// void setup() {
//   Serial.begin(115200);
//   delay(200);
//   Serial.println("USB -> NimBLE bridge starting...");

//   keyQueue = xQueueCreate(KEYQUEUE_DEPTH, sizeof(KeyEvent));
//   if (!keyQueue) { Serial.println("Key queue create failed"); while (1) delay(1000); }

//   xTaskCreatePinnedToCore(bleTask, "BLETask", BLE_TASK_STACK, NULL, BLE_TASK_PRIO, &bleTaskHandle, 1);

//   BaseType_t ok = xTaskCreatePinnedToCore(usb_lib_task, "usb_events", 4096, xTaskGetCurrentTaskHandle(), 2, NULL, 0);
//   if (ok != pdTRUE) Serial.println("usb_events create failed");

//   // wait for usb host to be ready
//   ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(3000));

//   const hid_host_driver_config_t hid_cfg = {
//     .create_background_task = true,
//     .task_priority = 5,
//     .stack_size = 4096,
//     .core_id = 0,
//     .callback = hid_host_device_callback,
//     .callback_arg = NULL
//   };
//   ESP_ERROR_CHECK(hid_host_install(&hid_cfg));

//   hid_host_event_queue = xQueueCreate(10, sizeof(hid_host_event_queue_t));
//   xTaskCreate(hid_worker_task, "hid_worker", 4096, NULL, 2, NULL);

//   Serial.println("Setup complete. Plug in USB keyboard to S3 host port.");
// }

// void loop() {
//   vTaskDelay(pdMS_TO_TICKS(1000));
// }
