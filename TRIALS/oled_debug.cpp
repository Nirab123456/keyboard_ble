// OLED debug helper (Adafruit SSD1306). Add to your project and call oled_logf(...)
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_I2C_ADDR 0x3C   // change to 0x3D if your module uses that

const uint8_t PIN_SDA = 8;
const uint8_t PIN_SCL = 9;


// Queue: small, fixed-length string messages
typedef struct { char txt[64]; } oled_msg_t;
static QueueHandle_t oledQueue = NULL;
static TaskHandle_t oledTaskHandle = NULL;
static Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

// Call early in setup()
bool oled_init() {
  Wire.begin(PIN_SDA,PIN_SCL); // default pins, or Wire.begin(sda,scl) if you need custom pins
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    Serial.println("OLED init failed");
    return false;
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.display();

  oledQueue = xQueueCreate(16, sizeof(oled_msg_t)); // hold ~16 messages
  if (!oledQueue) {
    Serial.println("OLED queue create failed");
    return false;
  }

  // display update task
  xTaskCreatePinnedToCore(
    [](void*){
      const int LINES = 4;                    // how many lines to show
      String lines[LINES];                    // small rolling buffer
      for (int i=0;i<LINES;i++) lines[i] = "";
      oled_msg_t m;
      for (;;) {
        // wait for next message (block)
        if (xQueueReceive(oledQueue, &m, portMAX_DELAY) == pdTRUE) {
          // roll lines up
          for (int i=0;i<LINES-1;i++) lines[i] = lines[i+1];
          lines[LINES-1] = String((const char*)m.txt);

          // draw to screen
          display.clearDisplay();
          display.setCursor(0,0);
          for (int i=0;i<LINES;i++) {
            display.setCursor(0, i*10); // 10px per line for textSize=1, adjust if needed
            display.println(lines[i]);
          }
          display.display();
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // small breathing room
      }
    },
    "OLEDTask",
    4096,
    NULL,
    1,
    &oledTaskHandle,
    1
  );

  return true;
}

// Helper: formatted log from task context
void oled_logf(const char *fmt, ...) {
  if (!oledQueue) return;
  oled_msg_t m;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(m.txt, sizeof(m.txt), fmt, ap);
  va_end(ap);
  // non-blocking send (0 timeout); if queue full drop oldest with peek/pop alternative
  if (xQueueSend(oledQueue, &m, 0) != pdTRUE) {
    // try to pop one then push (simple drop-oldest strategy)
    oled_msg_t dummy;
    xQueueReceive(oledQueue, &dummy, 0);
    xQueueSend(oledQueue, &m, 0);
  }
}

// ISR-safe variant: call from ISR (will use FromISR)
void oled_log_from_isr(const char *utf8msg) {
  if (!oledQueue) return;
  BaseType_t woken = pdFALSE;
  oled_msg_t m;
  strncpy(m.txt, utf8msg, sizeof(m.txt)-1);
  m.txt[sizeof(m.txt)-1] = '\0';
  xQueueSendFromISR(oledQueue, &m, &woken);
  portYIELD_FROM_ISR(woken);
}
