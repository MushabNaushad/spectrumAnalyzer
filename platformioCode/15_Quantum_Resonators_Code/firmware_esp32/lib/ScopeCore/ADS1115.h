#pragma once
// ---------------------------------------------------------------------------
// Minimal register-level ADS1115 driver (no Adafruit / third-party code).
// Written against TI SBAS444 (ADS111x datasheet).
//
// Design choices for clone modules:
//  * Single-shot mode + polling the OS bit instead of a fixed delay, so a part
//    whose real conversion time differs from the datasheet still reads right.
//  * Config write/read-back check at start-up.
//  * Resolution probe: many "ADS1115" boards carry an ADS1015 (12-bit). Its
//    result is left-justified, so the 4 LSBs are always 0. The volts scale is
//    still correct with this driver; you just get 12-bit resolution.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <Wire.h>

class ADS1115 {
public:
  // PGA field, config bits [11:9]
  enum Pga : uint16_t {
    PGA_6V144 = 0x0000,  // +-6.144 V FS, 187.5 uV/LSB
    PGA_4V096 = 0x0200,  // +-4.096 V FS, 125 uV/LSB   <- used here (0..3.3 V input)
    PGA_2V048 = 0x0400,  // +-2.048 V FS, 62.5 uV/LSB
    PGA_1V024 = 0x0600,
    PGA_0V512 = 0x0800,
    PGA_0V256 = 0x0A00,
  };
  // Data-rate field, config bits [7:5]
  enum Rate : uint16_t {
    SPS_8 = 0x0000, SPS_16 = 0x0020, SPS_32 = 0x0040, SPS_64 = 0x0060,
    SPS_128 = 0x0080, SPS_250 = 0x00A0, SPS_475 = 0x00C0, SPS_860 = 0x00E0,
  };

  // Returns false if the device does not ACK.
  bool begin(TwoWire &wire, uint8_t addr = 0x48, Pga pga = PGA_4V096, Rate rate = SPS_860);

  // Start one single-ended conversion on AIN<ch> (0..3) vs GND, wait, read.
  bool readSingleEnded(uint8_t ch, int16_t &raw);

  float lsbVolts() const { return lsbV_; }
  float toVolts(int16_t raw) const { return raw * lsbV_; }

  // ---- diagnostics ----
  bool configReadbackOk() const { return cfgReadbackOk_; }
  uint32_t errors() const { return errors_; }
  // last I2C result: 0 ok, 2 NACK addr, 3 NACK data, 4 bus error, 5 timeout,
  // 10+n = read returned n of 2 bytes. lastStage() = which step of begin() failed.
  uint8_t lastErr() const { return lastErr_; }
  const char *lastStage() const { return stage_; }
  uint32_t timeouts() const { return timeouts_; }
  uint32_t lastConvUs() const { return lastConvUs_; }  // start -> result read, incl. I2C
  // Returns 16 (ADS1115), 12 (probably ADS1015) or 0 (inconclusive: input at 0 V).
  int probeResolution(uint8_t ch, int samples = 16);

private:
  bool writeReg(uint8_t reg, uint16_t val);
  bool readReg(uint8_t reg, uint16_t &val);
  static uint32_t nominalPeriodUs(uint16_t rateBits);

  TwoWire *w_ = nullptr;
  uint8_t addr_ = 0x48;
  uint16_t pga_ = PGA_4V096;
  uint16_t rate_ = SPS_860;
  float lsbV_ = 125e-6f;
  uint32_t waitUs_ = 0;
  uint32_t timeoutUs_ = 0;
  bool cfgReadbackOk_ = false;
  uint8_t lastErr_ = 0;
  const char *stage_ = "";
  uint32_t errors_ = 0, timeouts_ = 0, lastConvUs_ = 0;
};
