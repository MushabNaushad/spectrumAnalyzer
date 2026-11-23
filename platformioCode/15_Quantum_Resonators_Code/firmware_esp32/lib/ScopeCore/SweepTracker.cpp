#include "SweepTracker.h"

void SweepTracker::reset(uint32_t nowMs) {
  st_ = CALIBRATING;
  runMax_ = 0;
  sawMax_ = 0;
  armed_ = false;
  byTimeout_ = false;
  retraces_ = 0;
  t0_ = nowMs;
}

bool SweepTracker::update(int16_t saw, uint32_t nowMs) {
  if (saw < 0) saw = 0;  // single-ended: tiny negative codes are just noise at 0 V

  if (st_ == CALIBRATING && saw > runMax_) runMax_ = saw;
  const int32_t ref = (st_ == CALIBRATING) ? runMax_ : sawMax_;

  // Schmitt retrace detector
  bool retrace = false;
  if (ref >= p_.minSawCounts) {
    if (!armed_) {
      if (int32_t(saw) * 100 > ref * p_.armPct) armed_ = true;
    } else if (int32_t(saw) * 100 < ref * p_.trigPct) {
      armed_ = false;
      retrace = true;
    }
  }

  if (st_ == RUNNING) return retrace;

  // ---- calibrating ----
  if (retrace && ++retraces_ >= p_.calRetraces) {
    sawMax_ = runMax_;
    st_ = RUNNING;
    byTimeout_ = false;
    return true;  // this sample starts the first locked sweep
  }
  if (nowMs - t0_ >= p_.calTimeoutMs && runMax_ >= p_.minSawCounts) {
    sawMax_ = runMax_;
    st_ = RUNNING;
    byTimeout_ = true;  // no clean retraces seen: is AIN2 really a sawtooth?
    armed_ = false;
  }
  return false;
}
