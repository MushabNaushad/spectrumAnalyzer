#pragma once
// ---------------------------------------------------------------------------
// NoiseGate: per-channel downward expander for the peak-detector traces.
//
//  * At the start of every sweep it looks at the previous sweep's samples and
//    estimates the noise floor (25th percentile) and the noise spread
//    ((median - p25) / 0.674, i.e. sigma for Gaussian noise). Peaks only fill a
//    few points of a sweep, so they barely move these numbers.
//  * Threshold = floor + max(K * sigma, MIN_MARGIN). Anything that rises less
//    than that above the floor is treated as noise: its distance from the floor
//    is multiplied by G = 10^(-ATTEN_DB/20) (3 dB -> x0.71; 60 dB -> flat line).
//    Dips below the floor are pulled up by the same factor.
//  * Between threshold and 2 x threshold the gain rises smoothly to 1, so real
//    peaks pass untouched and there is no visible step at the threshold.
//  * Floor and threshold are smoothed over sweeps so the baseline does not jump.
// ---------------------------------------------------------------------------
#include <math.h>
#include <algorithm>

class NoiseGate {
public:
  static constexpr int MAXN = 256;   // samples kept per sweep (~63 at 126 sets/s)

  void configure(float attenDb, float k, float minMarginV) {
    g_ = powf(10.0f, -attenDb / 20.0f);
    k_ = k;
    minMargin_ = minMarginV;
  }

  void reset() { n_ = 0; ready_ = false; }

  // Call on the first sample of each sweep, before process().
  void sweepStart() {
    if (n_ >= 16) {
      float s[MAXN];
      std::copy(buf_, buf_ + n_, s);
      std::sort(s, s + n_);
      const float p25 = s[n_ / 4], p50 = s[n_ / 2];
      const float sigma = (p50 - p25) / 0.674f;
      float thr = k_ * sigma;
      if (thr < minMargin_) thr = minMargin_;
      if (!ready_) { floor_ = p25; thr_ = thr; ready_ = true; }
      else         { floor_ += 0.3f * (p25 - floor_); thr_ += 0.3f * (thr - thr_); }
    }
    n_ = 0;
  }

  // v in volts; returns the cleaned value in volts.
  float process(float v) {
    if (n_ < MAXN) buf_[n_++] = v;
    if (!ready_) return v;
    const float e = v - floor_;
    return floor_ + e * gainFor(e);
  }

  float floorV() const { return floor_; }
  float thresholdV() const { return thr_; }
  bool ready() const { return ready_; }

private:
  float gainFor(float e) const {
    if (e <= thr_) return g_;
    if (e >= 2.0f * thr_) return 1.0f;
    float t = (e - thr_) / thr_;
    t = t * t * (3.0f - 2.0f * t);          // smoothstep
    return g_ + (1.0f - g_) * t;
  }

  float buf_[MAXN];
  int n_ = 0;
  bool ready_ = false;
  float floor_ = 0, thr_ = 0;
  float g_ = 0.708f, k_ = 3.0f, minMargin_ = 0.03f;
};
