#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
bool oled_INIT();
void oled_LOGF(const char* fmt,...);
void oled_log_from_ISR(const char* utf8msg);
