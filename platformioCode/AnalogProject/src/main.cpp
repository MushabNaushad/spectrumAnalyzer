#include <Arduino.h>
#include <Wire.h>

// ============================================================================
// ADS1115 PIN & ADDRESS DEFINITIONS
// ============================================================================
#define ADS1115_ADDR        0x48  // Default I2C address (ADDR connected to GND)
#define I2C_SDA_PIN         21    // ESP32 SDA pin
#define I2C_SCL_PIN         22    // ESP32 SCL pin

// ADS1115 Register Pointers
#define ADS_REG_CONVERSION  0x00
#define ADS_REG_CONFIG      0x01

// ============================================================================
// DYNAMIC ADS1115 SINGLE-ENDED CONFIGURATION (REFERENCED TO INTERNAL GND):
// Base masks (OS=1, PGA=001 [+/-4.096V], MODE=1 [single-shot], COMP_QUE=11 [disable])
// DR (Data Rate) bits are dynamically OR'ed into bits [7:5]:
//   860 SPS -> 0x00E0 (1165 us nominal conversion time)
//   475 SPS -> 0x00C0 (2110 us)
//   250 SPS -> 0x00A0 (4010 us)
//   128 SPS -> 0x0080 (7820 us)
// ============================================================================
#define BASE_A0_SINGLE_ENDED  0xC303  // MUX = 100 (AIN0 vs GND)
#define BASE_A1_SINGLE_ENDED  0xD303  // MUX = 101 (AIN1 vs GND)
#define BASE_A2_SINGLE_ENDED  0xE303  // MUX = 110 (AIN2 vs GND)

// 4.096V / 32768 = 0.000125V per LSB (125 uV resolution)
const float VOLTS_PER_LSB = 4.096f / 32768.0f;

// 3-point median filter: completely removes single-sample glitches/spikes
// without adding phase delay or blunting waveform edges
float med3(float a, float b, float c) {
  if ((a <= b && b <= c) || (c <= b && b <= a)) return b;
  if ((b <= a && a <= c) || (c <= a && a <= b)) return a;
  return c;
}

float h0[3] = {0, 0, 0};
float h1[3] = {0, 0, 0};
float h2[3] = {0, 0, 0};

// Hardware Rate & Timing
uint16_t currentDrBits = 0x00E0; // Default 860 SPS (fastest hardware limit of ADS1115)
unsigned long convWaitUs = 1165; // Nominal conversion time in us
unsigned long throttleUs = 0;    // Optional user throttle delay between transmissions

enum ChannelMode {
  MODE_3CH,  // Full 3-channel: A0, A1, A2 (~286 SPS maximum)
  MODE_XY1,  // Fast 2-channel: A0 & A2 only (~430 SPS maximum, 50% faster!)
  MODE_XY2,  // Fast 2-channel: A1 & A2 only (~430 SPS maximum, 50% faster!)
  MODE_1CH   // Ultra 1-channel: A0 only (~860 SPS maximum)
};
ChannelMode currentMode = MODE_3CH;

inline uint16_t getConfigA0() { return BASE_A0_SINGLE_ENDED | currentDrBits; }
inline uint16_t getConfigA1() { return BASE_A1_SINGLE_ENDED | currentDrBits; }
inline uint16_t getConfigA2() { return BASE_A2_SINGLE_ENDED | currentDrBits; }

void triggerChannel(uint16_t config) {
  Wire.beginTransmission(ADS1115_ADDR);
  Wire.write(ADS_REG_CONFIG);
  Wire.write((uint8_t)(config >> 8));
  Wire.write((uint8_t)(config & 0xFF));
  Wire.endTransmission();
}

int16_t readConversionRegister() {
  Wire.beginTransmission(ADS1115_ADDR);
  Wire.write(ADS_REG_CONVERSION);
  Wire.endTransmission(false);

  if (Wire.requestFrom((uint8_t)ADS1115_ADDR, (uint8_t)2) == 2) {
    uint8_t msb = Wire.read();
    uint8_t lsb = Wire.read();
    return (int16_t)((msb << 8) | lsb);
  }
  return 0;
}

enum SamplingStep {
  STEP_CONVERT_A0,
  STEP_CONVERT_A1,
  STEP_CONVERT_A2
};

SamplingStep currentStep = STEP_CONVERT_A0;
unsigned long convStartTime = 0;
float voltageA0 = 0.0f;
float voltageA1 = 0.0f;
float voltageA2 = 0.0f;

// Serial command buffer for live adjustor
String cmdBuffer = "";

void setRate(int sps) {
  if (sps >= 860) {
    currentDrBits = 0x00E0; // 860 SPS
    convWaitUs = 1165;
  } else if (sps >= 475) {
    currentDrBits = 0x00C0; // 475 SPS
    convWaitUs = 2110;
  } else if (sps >= 250) {
    currentDrBits = 0x00A0; // 250 SPS
    convWaitUs = 4010;
  } else if (sps >= 128) {
    currentDrBits = 0x0080; // 128 SPS
    convWaitUs = 7820;
  } else if (sps >= 64) {
    currentDrBits = 0x0060; // 64 SPS
    convWaitUs = 15630;
  } else if (sps >= 32) {
    currentDrBits = 0x0040; // 32 SPS
    convWaitUs = 31260;
  } else if (sps >= 16) {
    currentDrBits = 0x0020; // 16 SPS
    convWaitUs = 62510;
  } else {
    currentDrBits = 0x0000; // 8 SPS
    convWaitUs = 125020;
  }
}

void handleCommand(String cmd) {
  cmd.trim();
  if (cmd.startsWith("RATE:")) {
    int r = cmd.substring(5).toInt();
    setRate(r);
    Serial.print("[CONFIG] Rate set to "); Serial.print(r); Serial.println(" SPS");
  } else if (cmd.startsWith("MODE:")) {
    String m = cmd.substring(5);
    if (m == "3CH" || m == "FULL") currentMode = MODE_3CH;
    else if (m == "XY1") currentMode = MODE_XY1;
    else if (m == "XY2") currentMode = MODE_XY2;
    else if (m == "1CH") currentMode = MODE_1CH;
    Serial.print("[CONFIG] Mode set to "); Serial.println(m);
  } else if (cmd.startsWith("THROTTLE:")) {
    throttleUs = cmd.substring(9).toInt();
    Serial.print("[CONFIG] Throttle pause: "); Serial.print(throttleUs); Serial.println(" us");
  }
}

void checkSerialCommands() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (cmdBuffer.length() > 0) {
        handleCommand(cmdBuffer);
        cmdBuffer = "";
      }
    } else {
      cmdBuffer += c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // Initialize I2C at 400kHz Fast Mode
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000);

  // Verify ADS1115 presence
  Wire.beginTransmission(ADS1115_ADDR);
  if (Wire.endTransmission() != 0) {
    Serial.println("[ADS1115] ERROR: ADS1115 not detected at address 0x48!");
  } else {
    Serial.println("[ADS1115] Connected at I2C address 0x48");
  }

  Serial.println("[ADS1115] Dynamic Rate & Multi-Channel Mode Engine Loaded");
  Serial.println("DATA_START");

  // Trigger first conversion on A0
  triggerChannel(getConfigA0());
  convStartTime = micros();
  currentStep = STEP_CONVERT_A0;
}

void loop() {
  checkSerialCommands();

  unsigned long now = micros();
  unsigned long elapsed = now - convStartTime;

  if (currentStep == STEP_CONVERT_A0) {
    if (elapsed >= convWaitUs) {
      int16_t raw0 = readConversionRegister();
      if (raw0 < 0) raw0 = 0; // True 0V ground clamp
      float v0 = raw0 * VOLTS_PER_LSB;

      // Filter spikes
      h0[2] = h0[1]; h0[1] = h0[0]; h0[0] = v0;
      voltageA0 = med3(h0[0], h0[1], h0[2]);

      if (currentMode == MODE_1CH) {
        // Output single channel at full 860 SPS!
        Serial.print(voltageA0, 4);
        Serial.print(",");
        Serial.print(voltageA1, 4);
        Serial.print(",");
        Serial.println(voltageA2, 4);
        if (throttleUs > 0) delayMicroseconds(throttleUs);

        triggerChannel(getConfigA0());
        convStartTime = micros();
        currentStep = STEP_CONVERT_A0;
      } else if (currentMode == MODE_XY1) {
        // Fast XY1 mode (A0 vs A2): skip A1, directly trigger A2 (430 SPS!)
        triggerChannel(getConfigA2());
        convStartTime = micros();
        currentStep = STEP_CONVERT_A2;
      } else {
        // Full 3CH mode: trigger A1
        triggerChannel(getConfigA1());
        convStartTime = micros();
        currentStep = STEP_CONVERT_A1;
      }
    }
  } else if (currentStep == STEP_CONVERT_A1) {
    if (elapsed >= convWaitUs) {
      int16_t raw1 = readConversionRegister();
      if (raw1 < 0) raw1 = 0; // True 0V ground clamp
      float v1 = raw1 * VOLTS_PER_LSB;

      // Filter spikes
      h1[2] = h1[1]; h1[1] = h1[0]; h1[0] = v1;
      voltageA1 = med3(h1[0], h1[1], h1[2]);

      // Trigger conversion for Channel A2 (A2 vs GND)
      triggerChannel(getConfigA2());
      convStartTime = micros();
      currentStep = STEP_CONVERT_A2;
    }
  } else if (currentStep == STEP_CONVERT_A2) {
    if (elapsed >= convWaitUs) {
      int16_t raw2 = readConversionRegister();
      if (raw2 < 0) raw2 = 0; // True 0V ground clamp
      float v2 = raw2 * VOLTS_PER_LSB;

      // Filter spikes
      h2[2] = h2[1]; h2[1] = h2[0]; h2[0] = v2;
      voltageA2 = med3(h2[0], h2[1], h2[2]);

      // Output all three channels
      Serial.print(voltageA0, 4);
      Serial.print(",");
      Serial.print(voltageA1, 4);
      Serial.print(",");
      Serial.println(voltageA2, 4);
      if (throttleUs > 0) delayMicroseconds(throttleUs);

      if (currentMode == MODE_XY2) {
        // Fast XY2 mode (A1 vs A2): skip A0, directly trigger A1 (430 SPS!)
        triggerChannel(getConfigA1());
        convStartTime = micros();
        currentStep = STEP_CONVERT_A1;
      } else {
        // Full 3CH or XY1 mode: trigger A0
        triggerChannel(getConfigA0());
        convStartTime = micros();
        currentStep = STEP_CONVERT_A0;
      }
    }
  }
}