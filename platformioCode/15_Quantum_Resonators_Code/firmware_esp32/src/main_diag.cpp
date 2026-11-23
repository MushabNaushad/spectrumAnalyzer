// I2C + ADS1115 diagnostic (SDA = GPIO21, SCL = GPIO22).
// Build/flash: set default_envs = i2c_diag (or pick env:i2c_diag), Upload, then Monitor (115200).
//
// Each cycle:
//   1. bus recovery (9 clocks + STOP) and an address scan
//   2. the exact register operations the main firmware does, with error codes:
//        read CONFIG, write CONFIG, read it back
//   3. one single-shot conversion on AIN0..AIN3, printed in volts
//        AIN0 = ch1 peak detector, AIN1 = ch2 peak detector, AIN2 = ramp
//   all at 100 kHz and again at 400 kHz.
//
// v4: at start-up it also
//   a. drives each display pin (GPIO18,23,5,27,4,25) and GPIO19 low/high in turn and
//      checks whether SDA or SCL follows -> finds a copper/solder short directly
//   b. (i2c_diag_tft only) starts the display library and finds what holds SDA low
// The start-up report is stored and reprinted at the top of every cycle, so it is
// not lost if the monitor is opened late.
#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include <driver/gpio.h>
#ifdef DIAG_WITH_TFT
#include <TFT_eSPI.h>
static TFT_eSPI tft;
#endif

static String startupReport;
static void rep(const char *fmt, ...) {
  char b[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof b, fmt, ap);
  va_end(ap);
  startupReport += b;
  Serial.print(b);
}

static const int SDA_P = ADS_SDA_PIN;
static const int SCL_P = ADS_SCL_PIN;
static const uint8_t ADDR = ADS_I2C_ADDR;

// ADS1115 config bits
static const uint16_t OS_START  = 0x8000;
static const uint16_t PGA_4V096 = 0x0200;   // +-4.096 V, 125 uV/LSB
static const uint16_t MODE_SS   = 0x0100;   // single-shot
static const uint16_t DR_860    = 0x00E0;   // 860 SPS
static const uint16_t COMP_OFF  = 0x0003;
static const uint16_t BASE = PGA_4V096 | MODE_SS | DR_860 | COMP_OFF;

static const char *errName(uint8_t e) {
  switch (e) {
    case 0: return "ok";
    case 2: return "NACK on address";
    case 3: return "NACK on data";
    case 4: return "other bus error";
    case 5: return "timeout (line stuck)";
    default: return "?";
  }
}

static void levels(const char *tag) {
  pinMode(SDA_P, INPUT);
  pinMode(SCL_P, INPUT);
  delay(5);
  Serial.printf("  %-26s SDA=%s  SCL=%s\n", tag, digitalRead(SDA_P) ? "HIGH" : "LOW ",
                digitalRead(SCL_P) ? "HIGH" : "LOW ");
}

static void busRecover() {
  pinMode(SDA_P, OUTPUT_OPEN_DRAIN);
  pinMode(SCL_P, OUTPUT_OPEN_DRAIN);
  digitalWrite(SDA_P, HIGH);
  digitalWrite(SCL_P, HIGH);
  delayMicroseconds(10);
  for (int i = 0; i < 9; ++i) {
    digitalWrite(SCL_P, LOW);  delayMicroseconds(10);
    digitalWrite(SCL_P, HIGH); delayMicroseconds(10);
  }
  digitalWrite(SDA_P, LOW);  delayMicroseconds(10);
  digitalWrite(SCL_P, HIGH); delayMicroseconds(10);
  digitalWrite(SDA_P, HIGH); delayMicroseconds(10);
  pinMode(SDA_P, INPUT);
  pinMode(SCL_P, INPUT);
}

static uint8_t wr(uint8_t reg, uint16_t v) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  Wire.write(uint8_t(v >> 8));
  Wire.write(uint8_t(v & 0xFF));
  return Wire.endTransmission();
}

// returns 0 on success; otherwise endTransmission code, or 10 + bytes received
static uint8_t rd(uint8_t reg, uint16_t &v) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  uint8_t e = Wire.endTransmission();
  if (e) return e;
  uint8_t got = Wire.requestFrom(ADDR, uint8_t(2));
  if (got != 2) return 10 + got;
  uint8_t hi = Wire.read(), lo = Wire.read();
  v = (uint16_t(hi) << 8) | lo;
  return 0;
}

static void report(const char *what, uint8_t e) {
  if (e == 0) Serial.printf("    %-18s OK\n", what);
  else if (e >= 10) Serial.printf("    %-18s FAILED: read returned %u of 2 bytes\n", what, e - 10);
  else Serial.printf("    %-18s FAILED: %s (code %u)\n", what, errName(e), e);
}

static void adsTest(uint32_t hz) {
  Serial.printf("  ADS1115 test @ %lu kHz:\n", (unsigned long)(hz / 1000));
  busRecover();
  Wire.begin(SDA_P, SCL_P, hz);
  Wire.setTimeOut(20);

  uint16_t v = 0;
  uint8_t e = rd(1, v);
  report("read CONFIG", e);
  if (!e) Serial.printf("      CONFIG = 0x%04X  (0x8583 = power-on default, 0x%04X = set by firmware)\n",
                        v, (uint16_t)(BASE | (0x4 << 12)));

  const uint16_t cfg = BASE | (0x4 << 12);
  e = wr(1, cfg);
  report("write CONFIG", e);
  e = rd(1, v);
  report("read back CONFIG", e);
  if (!e) Serial.printf("      read back 0x%04X -> %s\n", v,
                        ((v & 0x7FFF) == (cfg & 0x7FFF)) ? "matches" : "MISMATCH");

  static const char *nm[4] = {"AIN0 ch1 peak", "AIN1 ch2 peak", "AIN2 ramp   ", "AIN3 unused "};
  for (uint8_t ch = 0; ch < 4; ++ch) {
    e = wr(1, OS_START | BASE | (uint16_t(0x4 | ch) << 12));
    if (e) { Serial.printf("    %s start FAILED: %s\n", nm[ch], errName(e)); continue; }
    uint32_t t0 = millis();
    bool done = false;
    while (millis() - t0 < 50) {
      delay(2);
      if (rd(1, v) == 0 && (v & OS_START)) { done = true; break; }
    }
    if (!done) { Serial.printf("    %s conversion never finished\n", nm[ch]); continue; }
    e = rd(0, v);
    if (e) { report("read CONVERSION", e); continue; }
    Serial.printf("    %s = %6.3f V\n", nm[ch], int16_t(v) * 125e-6f);
  }
  Wire.end();
  levels("bus after test:");
}

static int readIn(int pin) {
  pinMode(pin, INPUT);
  delay(3);
  return digitalRead(pin);
}

// Drive one pin low, then high, and see if SDA/SCL follow. Pull-ups R56/R55 hold
// SDA/SCL high, so a line that reads LOW only while pin X is driven low is
// connected to pin X.
static void crossTest() {
  const int pins[] = {18, 23, 5, 27, 4, 25, 19};
  const char *nm[] = {"GPIO18 SCLK", "GPIO23 MOSI", "GPIO5  CS", "GPIO27 DC", "GPIO4  RST",
                      "GPIO25 BL", "GPIO19 ALERT"};
  rep("  short test (display unplugged, pin driven LOW then HIGH):\n");
  int found = 0;
  for (int i = 0; i < 7; ++i) {
    const int p = pins[i];
    gpio_reset_pin((gpio_num_t)p);
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
    delay(3);
    const int sdaL = readIn(SDA_P), sclL = readIn(SCL_P);
    digitalWrite(p, HIGH);
    delay(3);
    const int sdaH = readIn(SDA_P), sclH = readIn(SCL_P);
    gpio_reset_pin((gpio_num_t)p);   // back to input
    const bool sdaFollows = (!sdaL && sdaH), sclFollows = (!sclL && sclH);
    if (sdaFollows || sclFollows) {
      rep("    %-13s -> %s%s   <== SHORTED\n", nm[i], sdaFollows ? "SDA follows it " : "",
          sclFollows ? "SCL follows it" : "");
      found++;
    } else {
      rep("    %-13s -> no effect on SDA/SCL\n", nm[i]);
    }
  }
  if (found) rep("  => %d short(s) found: clear them on the board before anything else\n", found);
  else       rep("  => no pin is shorted to SDA or SCL\n");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== I2C / ADS1115 diagnostic v4 ===");
  rep("  at boot: SDA=%s SCL=%s\n", readIn(SDA_P) ? "HIGH" : "LOW", readIn(SCL_P) ? "HIGH" : "LOW");
  crossTest();
#ifdef DIAG_WITH_TFT
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  rep("  display library started (tft_st7796 settings)\n");
  rep("  after display start: SDA=%s SCL=%s\n", readIn(SDA_P) ? "HIGH" : "LOW",
      readIn(SCL_P) ? "HIGH" : "LOW");
  if (!readIn(SDA_P)) {
    gpio_reset_pin((gpio_num_t)SDA_P);
    gpio_reset_pin((gpio_num_t)SCL_P);
    rep("    reset GPIO21/22 routing -> SDA=%s\n", readIn(SDA_P) ? "HIGH" : "LOW");
    const int pins[] = {19, 18, 23, 5, 27, 4, 25};
    for (int p : pins) {
      if (readIn(SDA_P)) break;
      gpio_reset_pin((gpio_num_t)p);
      delay(5);
      rep("    released GPIO%-2d -> SDA=%s\n", p, readIn(SDA_P) ? "HIGH" : "LOW");
    }
  }
#endif
  Serial.println("  starting I2C tests in 3 s...");
  delay(3000);
}

void loop() {
  Serial.println("--- cycle ---");
  Serial.println("  [start-up report]");
  Serial.print(startupReport);
  adsTest(100000);
  adsTest(400000);
  delay(2000);
}
