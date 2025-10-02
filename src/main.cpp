#include <Arduino.h>

uint16_t INTEGER ;

void setup()
{
    int i = 2;
    int j = 3;

    int k = i+j;
    INTEGER = k;

}

void loop()
{
    Serial.begin(115200);
    INTEGER++;
    Serial.printf("INTEGER IS %i",INTEGER);
}