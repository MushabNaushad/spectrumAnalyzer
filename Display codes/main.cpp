#include <Arduino.h>
#include <Wire.h>

#define SDA 1
#define SCL 2
#define INT 3
#define YA_ADC_ADDR 0x68
#define YB_ADC_ADDR 0x69
#define X_ADC_ADDR 0x70

void setup() {
    Serial.begin(115200);
    Wire.begin(SDA, SCL, 400000);
}

