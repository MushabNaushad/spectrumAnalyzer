#include "ADS1115.h"

namespace {
constexpr uint8_t REG_CONVERSION = 0x00;
constexpr uint8_t REG_CONFIG     = 0x01;

constexpr uint16_t CFG_OS        = 0x8000;  // W: 1 = start single conversion
                                            // R: 0 = converting, 1 = idle
constexpr uint8_t  CFG_MUX_SHIFT = 12;      // MUX[14:12]: 100..111 = AIN0..3 vs GND
constexpr uint16_t CFG_MODE_SS   = 0x0100;  // single-shot / power-down
constexpr uint16_t CFG_COMP_OFF  = 0x0003;  // COMP_QUE = 11: comparator off
}  // namespace

uint32_t ADS1115::nominalPeriodUs(uint16_t rateBits) {
  static const uint16_t sps[8] = {8, 16, 32, 64, 128, 250, 475, 860};
  return 1000000UL / sps[(rateBits >> 5) & 0x7];
}

bool ADS1115::begin(TwoWire &wire, uint8_t addr, Pga pga, Rate rate) {
  w_ = &wire;
  addr_ = addr;
  pga_ = pga;
  rate_ = rate;

  static const float fullScale[6] = {6.144f, 4.096f, 2.048f, 1.024f, 0.512f, 0.256f};
  lsbV_ = fullScale[(pga_ >> 9) & 0x7] / 32768.0f;

  const uint32_t T = nominalPeriodUs(rate_);  // 1163 us @ 860 SPS
  waitUs_    = T * 85 / 100;                  // sleep through most of the conversion,
  timeoutUs_ = 4 * T + 5000;                  // then poll; generous for slow clones

  // Write a known config WITHOUT starting a conversion, read it back.
  const uint16_t cfg = (uint16_t(0x4) << CFG_MUX_SHIFT) | pga_ | CFG_MODE_SS | rate_ | CFG_COMP_OFF;
  stage_ = "write CONFIG";
  if (!writeReg(REG_CONFIG, cfg)) return false;
  uint16_t rb = 0;
  stage_ = "read CONFIG";
  if (!readReg(REG_CONFIG, rb)) return false;
  stage_ = "";
  cfgReadbackOk_ = ((rb & 0x7FFF) == (cfg & 0x7FFF));  // ignore OS bit
  return true;
}

bool ADS1115::readSingleEnded(uint8_t ch, int16_t &raw) {
  if (ch > 3 || !w_) return false;

  const uint16_t cfg = CFG_OS | (uint16_t(0x4 | ch) << CFG_MUX_SHIFT) | pga_ |
                       CFG_MODE_SS | rate_ | CFG_COMP_OFF;
  const uint32_t tStart = micros();
  if (!writeReg(REG_CONFIG, cfg)) { errors_++; return false; }

  // The delta-sigma modulator + sinc filter settle in ONE conversion after a
  // mux change (single-cycle settling), so no samples need to be discarded.
  delayMicroseconds(waitUs_);

  uint16_t c = 0;
  for (;;) {
    if (!readReg(REG_CONFIG, c)) { errors_++; return false; }
    if (c & CFG_OS) break;  // conversion finished
    if (micros() - tStart > timeoutUs_) { errors_++; timeouts_++; return false; }
    delayMicroseconds(40);
  }

  uint16_t v = 0;
  if (!readReg(REG_CONVERSION, v)) { errors_++; return false; }
  raw = int16_t(v);  // two's complement
  lastConvUs_ = micros() - tStart;
  return true;
}

int ADS1115::probeResolution(uint8_t ch, int samples) {
  uint16_t lowBits = 0;
  bool nonZero = false;
  for (int i = 0; i < samples; ++i) {
    int16_t r = 0;
    if (!readSingleEnded(ch, r)) return 0;
    if (r != 0) nonZero = true;
    lowBits |= uint16_t(r) & 0x000F;
  }
  if (!nonZero) return 0;
  return lowBits ? 16 : 12;
}

bool ADS1115::writeReg(uint8_t reg, uint16_t val) {
  w_->beginTransmission(addr_);
  w_->write(reg);                  // address pointer register
  w_->write(uint8_t(val >> 8));    // MSB first
  w_->write(uint8_t(val & 0xFF));
  lastErr_ = w_->endTransmission();
  return lastErr_ == 0;
}

bool ADS1115::readReg(uint8_t reg, uint16_t &val) {
  w_->beginTransmission(addr_);
  w_->write(reg);
  lastErr_ = w_->endTransmission();              // STOP (plays nicer with clones)
  if (lastErr_ != 0) return false;
  const uint8_t got = w_->requestFrom(addr_, uint8_t(2));
  if (got != 2) { lastErr_ = 10 + got; return false; }
  const uint8_t hi = w_->read();
  const uint8_t lo = w_->read();
  val = (uint16_t(hi) << 8) | lo;
  return true;
}
