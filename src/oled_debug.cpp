// --- oled_debug.h (single-file inline helper) ---
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

static Adafruit_SSD1306 *g_display = nullptr;
static QueueHandle_t oledQueue = NULL;
static TaskHandle_t oledTaskHandle = NULL;

bool oled_INIT(uint8_t i2c_addr = 0x3C, int width = 128, int height = 64, int sda_pin = 8, int scl_pin = 9) {
  Wire.begin(sda_pin, scl_pin);            // ESP32-S3 defaults: SDA=8, SCL=9 (change if needed)
  // allocate display object dynamically to avoid static constructor issues
  if (g_display) delete g_display;
  g_display = new Adafruit_SSD1306(width, height, &Wire, -1);
  if (!g_display) return false;
  if (!g_display->begin(SSD1306_SWITCHCAPVCC, i2c_addr)) {
    // try the alternate address if you suspect 0x3D
    return false;
  }
  g_display->clearDisplay();
  g_display->setTextSize(1);
  g_display->setTextColor(SSD1306_WHITE);
  g_display->display();

  // create queue
  oledQueue = xQueueCreate(16, 64);
  if (!oledQueue) return false;

  // display task
  xTaskCreatePinnedToCore(
    [](void*) {
      const int LINES = 4;
      String buf[LINES];
      for (;;) {
        char linebuf[64] = {0};
        if (xQueueReceive(oledQueue, &linebuf, portMAX_DELAY) == pdTRUE) {
          // shift buffer
          for (int i=0; i<LINES-1; ++i) buf[i] = buf[i+1];
          buf[LINES-1] = String(linebuf);

          // redraw
          g_display->clearDisplay();
          g_display->setCursor(0,0);
          for (int i=0; i<LINES; ++i) {
            g_display->setCursor(0, i*10);
            g_display->println(buf[i]);
          }
          g_display->display();
        }
      }
    },
    "OLEDTask", 4096, NULL, 1, &oledTaskHandle, 1
  );
  return true;
}

void oled_LOGF(const char *fmt, ...) {
  if (!oledQueue) return;
  char tmp[64];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  // try non-blocking; if full, pop one then push
  if (xQueueSend(oledQueue, tmp, 0) != pdTRUE) {
    char dummy[64]; // drop oldest
    xQueueReceive(oledQueue, &dummy, 0);
    xQueueSend(oledQueue, tmp, 0);
  }
}
