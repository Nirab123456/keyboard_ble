#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
bool oled_INIT(uint8_t i2c_addr = 0x3C, int width = 128, int height = 64, int sda_pin = 8, int scl_pin = 9);
void oled_LOGF(const char* fmt,...);
