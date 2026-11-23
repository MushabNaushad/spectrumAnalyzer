#pragma once
// ---------------------------------------------------------------------------
// Acquisition task: owns the I2C bus + ADS1115 and pushes X-Y sample sets
// into a FreeRTOS queue. Runs pinned to its own core, so drawing / Wi-Fi on
// the consumer side never stalls the ADC.
//
// Conversion order per set:  SIG1 (AIN0) -> SAW (AIN2) -> SIG2 (AIN1)
// The saw (X) sample sits in the middle in time, so the X error seen by both
// Y channels is symmetric (~ +-1.4 ms) instead of 0 / 2.9 ms.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "ADS1115.h"
#include "SweepTracker.h"

struct ScopeSample {
  int16_t saw;    // raw ADC counts
  int16_t sig1;
  int16_t sig2;
  uint8_t flags;  // SAMPLE_*
};

enum : uint8_t {
  SAMPLE_SWEEP_START = 0x01,  // first sample after a retrace
  SAMPLE_CALIBRATED  = 0x02,  // saw max locked -> X mapping valid
  SAMPLE_ADC_ERROR   = 0x04,  // I2C / conversion error, values invalid
};

struct ScopeSamplerConfig {
  int      sdaPin   = 21;
  int      sclPin   = 22;
  uint8_t  i2cAddr  = 0x48;
  uint32_t i2cHz    = 400000;
  uint8_t  chSig1   = 0;
  uint8_t  chSig2   = 1;
  uint8_t  chSaw    = 2;
  int      recalPin = 0;      // active-low button (BOOT = GPIO0), -1 = none
  BaseType_t  core     = 0;
  UBaseType_t priority = 2;
  uint16_t queueLen = 512;
  SweepParams sweep;
};

class ScopeSampler {
public:
  bool begin(const ScopeSamplerConfig &cfg);
  bool pop(ScopeSample &s, TickType_t wait = 0);
  void requestRecalibration() { recalReq_ = true; }

  // Live status, written by the sampler task, safe to read from anywhere.
  bool     adcFound()      const { return adcFound_; }
  bool     calibrated()    const { return calibrated_; }
  int16_t  sawMax()        const { return sawMax_; }       // locked max (counts)
  int16_t  sawRunningMax() const { return runMax_; }       // during calibration
  float    setsPerSec()    const { return setsPerSec_; }
  uint32_t dropped()       const { return dropped_; }
  uint32_t adcErrors()     const { return adcErrors_; }
  float    lsbVolts()      const { return lsbV_; }

private:
  static void taskEntry(void *arg);
  void run();

  ScopeSamplerConfig cfg_;
  QueueHandle_t q_ = nullptr;
  ADS1115 adc_;
  SweepTracker tracker_;

  volatile bool     recalReq_ = false;
  volatile bool     adcFound_ = false;
  volatile bool     calibrated_ = false;
  volatile int16_t  sawMax_ = 0;
  volatile int16_t  runMax_ = 0;
  volatile float    setsPerSec_ = 0;
  volatile uint32_t dropped_ = 0;
  volatile uint32_t adcErrors_ = 0;
  volatile float    lsbV_ = 125e-6f;
};
