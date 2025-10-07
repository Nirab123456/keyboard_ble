#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
void oled_log_from_isr(const char *utf8msg);
void oled_logf(const char *fmt, ...);
bool oled_init();