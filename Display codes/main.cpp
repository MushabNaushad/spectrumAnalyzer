#include <Arduino.h>
#include <Wire.h>

#define ADS1115_ADDR        0x48
#define I2C_SDA_PIN         21
#define I2C_SCL_PIN         22
#define ADS_RDY_PIN         19    // ALERT/RDY pin for hardware sync

#define ADS_REG_CONVERSION  0x00
#define ADS_REG_CONFIG      0x01
#define ADS_REG_LO_THRESH   0x02
#define ADS_REG_HI_THRESH   0x03

// ============================================================================
// MODE SELECTOR:
// Set to 1 for Single-Ended (0V to +2.048V on A0)
// Set to 0 for Differential (-2.048V to +2.048V across A0 and A1)
// ============================================================================
#define SINGLE_ENDED_MODE 1

#if SINGLE_ENDED_MODE
  // MUX = 100 (A0 vs GND), PGA = 010 (+/-2.048V), Continuous Mode
  #define CONFIG_HI 0x44
#else
  // MUX = 000 (A0 vs A1),  PGA = 010 (+/-2.048V), Continuous Mode
  #define CONFIG_HI 0x04
#endif

// 860 SPS, ALERT/RDY pulse enabled (COMP_QUE = 00)
#define CONFIG_LO 0xE0

// 2.048V / 32768 = 0.0000625V per LSB
const float VOLTS_PER_LSB = 0.0000625f;

volatile bool sampleReady = false;

void IRAM_ATTR onConversionComplete() {
  sampleReady = true;
}

void writeRegister(uint8_t reg, uint16_t val) {
  Wire.beginTransmission(ADS1115_ADDR);
  Wire.write(reg);
  Wire.write((uint8_t)(val >> 8));
  Wire.write((uint8_t)(val & 0xFF));
  Wire.endTransmission();
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // Setup ALERT/RDY input with internal pull-up
  pinMode(ADS_RDY_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ADS_RDY_PIN), onConversionComplete, FALLING);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000); // 400kHz Fast I2C

  // Configure ALERT/RDY pin to pulse low when a conversion finishes
  writeRegister(ADS_REG_HI_THRESH, 0x8000);
  writeRegister(ADS_REG_LO_THRESH, 0x0000);

  // Start continuous conversions at 860 SPS
  uint16_t config = ((uint16_t)CONFIG_HI << 8) | CONFIG_LO;
  writeRegister(ADS_REG_CONFIG, config);

  // Point to conversion register for fast continuous read
  Wire.beginTransmission(ADS1115_ADDR);
  Wire.write(ADS_REG_CONVERSION);
  Wire.endTransmission();

  Serial.println("DATA_START");
}

void loop() {
  if (sampleReady) {
    sampleReady = false;

    // Read conversion register
    if (Wire.requestFrom((uint8_t)ADS1115_ADDR, (uint8_t)2) == 2) {
      uint8_t msb = Wire.read();
      uint8_t lsb = Wire.read();
      int16_t rawADC = (int16_t)((msb << 8) | lsb);

      #if SINGLE_ENDED_MODE
      if (rawADC < 0) rawADC = 0; // Discard negative noise on single-ended
      #endif

      float voltage = rawADC * VOLTS_PER_LSB;
      Serial.println(voltage, 4);
    }
    // If I2C failed to return 2 bytes, skip the loop without sending corrupted data
  }
}
