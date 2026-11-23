// ===========================================================================
//  ESP32 + ADS1115 X-Y scope  --  SPI TFT build (3.5" 480x320 ST7796 / ILI9488, TFT_eSPI)
//
//  AIN0 = signal 1 -> left half, AIN1 = signal 2 -> right half,
//  AIN2 = sawtooth -> X sweep. Saw min fixed at 0 V, saw max learned during
//  the first sweeps then locked. Y fixed 0..3.3 V.
//
//  Core 0 : ScopeSampler task (I2C / ADS1115 only)
//  Core 1 : this loop (SPI / TFT only) - the two buses never block each other
// ===========================================================================
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"
#include "ScopeSampler.h"
#include "xyplot_tft.h"
#include "noise_gate.h"

static TFT_eSPI tft;
static XYPlot plot(tft, Y_MAX_VOLTS, Y_DIV_VOLTS, X_LABEL_LEFT, X_LABEL_RIGHT);
static ScopeSampler sampler;
static bool wasCalibrated = false;
static NoiseGate gate[2];   // CH1, CH2

static void drawHeader() {
  char buf[48];
  if (!sampler.adcFound()) {
    plot.header("ADS1115 not found - check I2C", true);
  } else if (!sampler.calibrated()) {
    snprintf(buf, sizeof buf, "LEARNING saw max  %.3fV", sampler.sawRunningMax() * sampler.lsbVolts());
    plot.header(buf, true);
  } else {
    snprintf(buf, sizeof buf, "%.1fV/div  saw %.3fV  %3.0f/s", Y_DIV_VOLTS,
             sampler.sawMax() * sampler.lsbVolts(), sampler.setsPerSec());
    plot.header(buf, false);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== ADS1115 X-Y scope : TFT build ===");

  for (auto &g : gate) g.configure(NOISE_ATTEN_DB, NOISE_K, NOISE_MIN_MARGIN_V);
  plot.begin();                      // static layer
  plot.message("Learning saw max...");

  ScopeSamplerConfig cfg;
  cfg.sdaPin = ADS_SDA_PIN;
  cfg.sclPin = ADS_SCL_PIN;
  cfg.i2cAddr = ADS_I2C_ADDR;
  cfg.i2cHz = ADS_I2C_HZ;
  cfg.chSig1 = CH_SIG1;
  cfg.chSig2 = CH_SIG2;
  cfg.chSaw = CH_SAW;
  cfg.recalPin = RECAL_PIN;
  cfg.core = 0;
  if (!sampler.begin(cfg)) Serial.println("[ERR] sampler task failed to start");
}

void loop() {
  ScopeSample s;
  if (sampler.pop(s, pdMS_TO_TICKS(10))) {
    tft.startWrite();  // keep CS low across the whole batch
    int n = 0;
    do {
      const bool cal = s.flags & SAMPLE_CALIBRATED;
      if (cal != wasCalibrated) {  // lock / re-learn transition
        wasCalibrated = cal;
        if (cal) plot.clearPlot();
        else     plot.message("Learning saw max...");
        for (auto &g : gate) g.reset();
      }
      if (cal && !(s.flags & SAMPLE_ADC_ERROR)) {
        const bool start = s.flags & SAMPLE_SWEEP_START;
        const float lsb = sampler.lsbVolts();
        float v1 = s.sig1 * lsb, v2 = s.sig2 * lsb;
#if NOISE_GATE_ON
        if (start) { gate[0].sweepStart(); gate[1].sweepStart(); }
        v1 = gate[0].process(v1);
        v2 = gate[1].process(v2);
#endif
        plot.addVolts(s.saw, v1, v2, sampler.sawMax(), start);
      }
    } while (++n < 64 && sampler.pop(s, 0));
    tft.endWrite();
  }

  static uint32_t tHdr = 0, tLog = 0;
  if (millis() - tHdr >= 250) {
    tHdr = millis();
    drawHeader();
  }
  if (millis() - tLog >= 2000) {
    tLog = millis();
    Serial.printf("[STAT] %s  saw max %.3f V  %.1f sets/s  dropped %lu  adc err %lu\n",
                  sampler.calibrated() ? "LOCKED" : "LEARNING",
                  (sampler.calibrated() ? sampler.sawMax() : sampler.sawRunningMax()) *
                      sampler.lsbVolts(),
                  sampler.setsPerSec(), (unsigned long)sampler.dropped(),
                  (unsigned long)sampler.adcErrors());
#if NOISE_GATE_ON
    Serial.printf("[GATE] CH1 floor %.3f V thr +%.3f V | CH2 floor %.3f V thr +%.3f V | -%.1f dB\n",
                  gate[0].floorV(), gate[0].thresholdV(), gate[1].floorV(), gate[1].thresholdV(),
                  (double)NOISE_ATTEN_DB);
#endif
  }
}
