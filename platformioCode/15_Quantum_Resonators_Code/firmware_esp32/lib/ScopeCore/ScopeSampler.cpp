#include "ScopeSampler.h"

bool ScopeSampler::begin(const ScopeSamplerConfig &cfg) {
  cfg_ = cfg;
  q_ = xQueueCreate(cfg_.queueLen, sizeof(ScopeSample));
  if (!q_) return false;
  return xTaskCreatePinnedToCore(taskEntry, "adsScope", 4096, this, cfg_.priority,
                                 nullptr, cfg_.core) == pdPASS;
}

bool ScopeSampler::pop(ScopeSample &s, TickType_t wait) {
  return q_ && xQueueReceive(q_, &s, wait) == pdTRUE;
}

void ScopeSampler::taskEntry(void *arg) {
  static_cast<ScopeSampler *>(arg)->run();
}

static inline int16_t clampPos(int16_t v) { return v < 0 ? 0 : v; }

// Free a slave that is holding SDA low (e.g. it was reset-interrupted mid-byte
// while staying powered): 9 SCL clocks with SDA released, then a STOP.
static void i2cBusRecover(int sda, int scl) {
  pinMode(sda, OUTPUT_OPEN_DRAIN);
  pinMode(scl, OUTPUT_OPEN_DRAIN);
  digitalWrite(sda, HIGH);
  digitalWrite(scl, HIGH);
  delayMicroseconds(10);
  for (int i = 0; i < 9; ++i) {
    digitalWrite(scl, LOW);  delayMicroseconds(10);
    digitalWrite(scl, HIGH); delayMicroseconds(10);
  }
  digitalWrite(sda, LOW);  delayMicroseconds(10);
  digitalWrite(scl, HIGH); delayMicroseconds(10);
  digitalWrite(sda, HIGH); delayMicroseconds(10);
  pinMode(sda, INPUT);
  pinMode(scl, INPUT);
}

void ScopeSampler::run() {
  if (cfg_.recalPin >= 0) pinMode(cfg_.recalPin, INPUT_PULLUP);

  i2cBusRecover(cfg_.sdaPin, cfg_.sclPin);
  Wire.begin(cfg_.sdaPin, cfg_.sclPin, cfg_.i2cHz);
  Wire.setTimeOut(20);  // ms; a stuck clone must not hang the task

  uint32_t hz = cfg_.i2cHz;
  int fails = 0;
  while (!adc_.begin(Wire, cfg_.i2cAddr, ADS1115::PGA_4V096, ADS1115::SPS_860)) {
    const uint8_t e = adc_.lastErr();
    Serial.printf("[ADS] 0x%02X failed at '%s' @ %lu kHz: %s (code %u), recovering bus...\n",
                  cfg_.i2cAddr, adc_.lastStage(), (unsigned long)(hz / 1000),
                  e == 2 ? "NACK on address" : e == 3 ? "NACK on data" : e == 4 ? "bus error"
                  : e == 5 ? "timeout" : e >= 10 ? "short read" : "?", e);
    Wire.end();
    i2cBusRecover(cfg_.sdaPin, cfg_.sclPin);
    if (++fails == 3 && hz > 100000) {
      hz = 100000;
      Serial.println("[ADS] dropping I2C to 100 kHz");
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    Wire.begin(cfg_.sdaPin, cfg_.sclPin, hz);
    Wire.setTimeOut(20);
  }
  adcFound_ = true;
  lsbV_ = adc_.lsbVolts();

  if (!adc_.configReadbackOk())
    Serial.println("[ADS] WARNING: config register read-back mismatch (clone?)");
  const int bits = adc_.probeResolution(cfg_.chSaw);
  Serial.printf("[ADS] found at 0x%02X, conversion+I2C = %lu us (nominal 860 SPS -> 1163 us)\n",
                cfg_.i2cAddr, (unsigned long)adc_.lastConvUs());
  Serial.printf("[ADS] resolution check: %s\n",
                bits == 16 ? "16-bit -> genuine ADS1115 behaviour"
                : bits == 12 ? "12-bit -> probably an ADS1015 clone (volts scale still correct)"
                             : "inconclusive (saw input at 0 V?)");

  tracker_.setParams(cfg_.sweep);
  tracker_.reset(millis());
  Serial.println("[SAW] learning sawtooth max (min fixed at 0 V)...");

  bool lastBtn = true;
  uint32_t nSets = 0, rateSets = 0, rateT0 = millis();

  for (;;) {
    // ---- re-calibration request (BOOT button or API) ----
    if (cfg_.recalPin >= 0) {
      const bool btn = digitalRead(cfg_.recalPin);
      if (lastBtn && !btn) recalReq_ = true;  // falling edge
      lastBtn = btn;
    }
    if (recalReq_) {
      recalReq_ = false;
      tracker_.reset(millis());
      calibrated_ = false;
      Serial.println("[SAW] re-learning sawtooth max...");
    }

    // ---- one X-Y set: SIG1 -> SAW -> SIG2 ----
    ScopeSample s{};
    bool ok = adc_.readSingleEnded(cfg_.chSig1, s.sig1);
    ok = adc_.readSingleEnded(cfg_.chSaw, s.saw) && ok;
    ok = adc_.readSingleEnded(cfg_.chSig2, s.sig2) && ok;

    if (!ok) {
      s.flags |= SAMPLE_ADC_ERROR;
      adcErrors_ = adc_.errors();
    } else {
      s.saw = clampPos(s.saw);
      s.sig1 = clampPos(s.sig1);
      s.sig2 = clampPos(s.sig2);
      if (tracker_.update(s.saw, millis())) s.flags |= SAMPLE_SWEEP_START;
    }
    runMax_ = tracker_.runningMax();

    if (tracker_.state() == SweepTracker::RUNNING) {
      s.flags |= SAMPLE_CALIBRATED;
      if (!calibrated_) {
        sawMax_ = tracker_.sawMax();
        calibrated_ = true;
        Serial.printf("[SAW] max LOCKED at %.4f V (%d counts)%s\n", sawMax_ * lsbV_, sawMax_,
                      tracker_.lockedByTimeout() ? "  [timeout: no clean retrace seen!]" : "");
      }
    }

    if (xQueueSend(q_, &s, 0) != pdTRUE) dropped_ = dropped_ + 1;

    // ---- stats + let the idle task run (task watchdog) ----
    ++rateSets;
    const uint32_t now = millis();
    if (now - rateT0 >= 1000) {
      setsPerSec_ = rateSets * 1000.0f / float(now - rateT0);
      rateSets = 0;
      rateT0 = now;
    }
    // X comes from the saw VOLTAGE, not from time, so a 1 ms gap here does
    // not distort the X-Y plot at all.
    if ((++nSets & 127) == 0) vTaskDelay(1);
  }
}
