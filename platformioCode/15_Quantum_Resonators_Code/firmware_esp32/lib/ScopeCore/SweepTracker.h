#pragma once
// ---------------------------------------------------------------------------
// Turns the sawtooth channel into the X time-base.
//
//  CALIBRATING : min is fixed at 0 V. The running max of the saw is tracked
//                for the first few sweeps, then FROZEN (locked) for the rest
//                of the run.
//  RUNNING     : X = saw / sawMax. Each retrace (fly-back) is detected with a
//                Schmitt-style detector: arm above 75 % of max, fire below
//                25 %. A slow fall edge therefore cannot trigger twice.
// Pure C++, no Arduino dependency (host-testable).
// ---------------------------------------------------------------------------
#include <stdint.h>

struct SweepParams {
  int16_t  minSawCounts = 1600;   // reject saws smaller than this (0.2 V @ 125 uV/LSB)
  uint8_t  calRetraces  = 3;      // retraces to observe before locking (>= 2 full ramps)
  uint32_t calTimeoutMs = 15000;  // lock anyway after this, if amplitude is sane
  uint8_t  armPct       = 75;     // arm when saw > 75 % of max
  uint8_t  trigPct      = 25;     // retrace when armed and saw < 25 % of max
};

class SweepTracker {
public:
  enum State : uint8_t { CALIBRATING = 0, RUNNING = 1 };

  void setParams(const SweepParams &p) { p_ = p; }
  void reset(uint32_t nowMs);

  // Feed one saw sample (raw counts). Returns true if this sample is the
  // first one of a new sweep (only ever true in RUNNING).
  bool update(int16_t saw, uint32_t nowMs);

  State   state()       const { return st_; }
  int16_t sawMax()      const { return sawMax_; }   // valid in RUNNING
  int16_t runningMax()  const { return runMax_; }
  bool    lockedByTimeout() const { return byTimeout_; }

private:
  SweepParams p_;
  State   st_ = CALIBRATING;
  int16_t runMax_ = 0;
  int16_t sawMax_ = 0;
  bool    armed_ = false;
  bool    byTimeout_ = false;
  uint8_t retraces_ = 0;
  uint32_t t0_ = 0;
};
