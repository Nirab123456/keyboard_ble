#include <Arduino.h>
#include<Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_I2C_ADDR 0x3C

const char* OLED_TASK_NAME = "OLED_DEBUGGER";
const size_t OLED_DEBUGGE = 4096;
const uint8_t PIN_SDA = 8;
const uint8_t PIN_SCL = 9;

typedef struct oled_debug
{
    char txt[64];
};
static QueueHandle_t oledQueue = NULL;
static TaskHandle_t oledTaskHandle = NULL;
static Adafruit_SSD1306 display(OLED_WIDTH,OLED_HEIGHT,&Wire,-1);

bool oled_INIT()
{
    Wire.begin(PIN_SDA,PIN_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC,OLED_I2C_ADDR))
    {
        Serial.println("OLED INIT FAILED");
        return false;
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0,0);
    display.display();
    oledQueue = xQueueCreate(16,sizeof(oled_debug)); 
    if (!oledQueue)
    {
        Serial.println("OLED QUEUE creation failed ");
        return false;
    }
    xTaskCreatePinnedToCore(
        [](void*)
        {
            const uint8_t LINES =4;
            String lines[LINES];
            for (size_t i = 0; i < LINES;i++)
            {
                lines [i] = lines[i+1];
            }
            oled_debug msg;
            for (;;)
            {
                if (xQueueReceive(oledQueue,&msg,portMAX_DELAY==pdTRUE))
                {
                    for (size_t i = 0; i < LINES; i++)
                    {
                        lines[LINES-1] = String((const char*)msg.txt);
                    }
                    display.clearDisplay();
                    display.setCursor(0,0);
                    for (size_t i = 0; i < LINES; i++)
                    {
                        display.setCursor(0,i*10);
                        display.println(lines[i]);
                    }
                    display.display();
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        },
        OLED_TASK_NAME,
        OLED_DEBUGGE,
        NULL,
        1,
        &oledTaskHandle,
        1
    );
    return true;
}


void oled_LOGF(const char* fmt,...)
{
    if (!oledQueue)
    {
        return;
    }
    oled_debug msg;
    va_list ap;
    va_start(ap,fmt);
    vsnprintf(msg.txt,sizeof(msg.txt),fmt,ap);
    va_end(ap);
    if (xQueueSend(oledQueue,&msg,0)!= pdTRUE)
    {
        oled_debug dummy;
        xQueueReceive(oledQueue,&dummy,0);
        xQueueSend(oledQueue,&msg,0);
    }
        
}


void oled_log_from_ISR(const char* utf8msg)
{
    if (!oledQueue)
    {
        BaseType_t woken = pdFALSE;
        oled_debug msg;
        strncpy(msg.txt,utf8msg,sizeof(msg.txt)-1);
        msg.txt[sizeof(msg.txt)-1] = '\0';
        xQueueSendFromISR(oledQueue,&msg,&woken);
        portYIELD_FROM_ISR(woken);
    }
    
}
