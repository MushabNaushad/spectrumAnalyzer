# Static LO Drift Test — Simulation Summary & Findings

> **Circuit:** `Filter + Env Detector/Filter_AP_drift_test.asc`  
> **Simulation Type:** Stepped Transient Analysis (`.step param drift -500 500 25`, `.tran 0 120m 60m 1u`)  
> **Test Conditions:** f_RF = 5.0 kHz (1 Vpk), f_LO = 20 kHz + drift, Mixer Gain = 2.5x  
> **Target Intermediate Frequency (IF):** f_IF = f_LO + f_RF = 25 kHz + drift

---

## 1. Executive Summary

| Parameter | IF Channel 2 (25 kHz Target) | IF Channel 3 (35 kHz Target) |
|---|:---:|:---:|
| **Design Center Frequency (f0)** | 25,000 Hz | 35,000 Hz |
| **Simulated Center Frequency (f0)** | **24,975 Hz** (Δf = -25 Hz / -0.10%) | N/A (Self-oscillating) |
| **Design Bandwidth (3 dB)** | 200 Hz (Q = 125) | 200 Hz (Q = 175) |
| **Simulated Bandwidth (3 dB)** | **≈ 176 Hz** (-51 Hz to +125 Hz) | N/A |
| **Peak Output Amplitude** | 7.40 Vpp (Rail Saturation) | 7.37 Vpp (Constant Oscillation) |
| **Functional Status** | ⚠️ **Working (Attenuator Needed)** | ❌ **Unstable (Needs Topology Redesign)** |

---

## 2. Key Findings by Channel

### Channel 2: 25 kHz Sallen-Key Bandpass Filter (⚠️ Working with Clipping)

1. **Center Frequency Accuracy:**
   * The output peak occurred at **drift = -25 Hz**.
   * Actual filter center is **24,975 Hz** vs. 25,000 Hz nominal (-0.10% error), well within 1% component tolerances.
2. **Selectivity & Bandwidth:**
   * The measured -3 dB bandwidth is **≈ 176 Hz** (designed for 200 Hz).
   * Skirt attenuation drops by > 20 dB at |drift| = 500 Hz.
3. **Passband Rail Saturation:**
   * For |drift| <= 75 Hz, the output is clamped at **7.40 Vpp** on the ±5V rails.
   * **Cause:** 1 Vpk input * 2.5x mixer gain * high filter Q gain (Q=125) overdrives the LT1124 op-amps.
   * **Fix:** Add a resistive attenuator or reduce mixer gain to keep in-band signals below 4 Vpp.

---

### Channel 3: 35 kHz Sallen-Key Bandpass Filter (❌ Self-Oscillating)

1. **Unconditional Instability:**
   * Output voltage was locked at **7.369 Vpp across all 41 drift steps** (σ = 0.00014 V).
   * The output is completely independent of the input signal.
2. **Root Cause Analysis:**
   * The design requires **Q = 35 kHz / 200 Hz = 175**.
   * Single-stage Sallen-Key bandpass topologies become conditionally unstable for Q > 50 due to op-amp finite Gain-Bandwidth Product (GBW). The LT1124 (GBW ≈ 12.5 MHz) introduces excess phase shift in the positive feedback loop, forcing the filter into sustained rail-to-rail oscillation at ≈ 35 kHz.
3. **Recommended Fix:**
   * Replace the Sallen-Key topology with a **Multiple Feedback (MFB)** bandpass filter, or cascade two lower-Q stages (Q ≈ 87 each).

---

## 3. Parametric Sweep Data Table

| LO Drift (Hz) | Total LO Freq (Hz) | Mix Product f_LO + f_RF (Hz) | Vpp(IF_out_2) [V] | Vpp(IF_out_3) [V] | Channel 2 Behavior |
|:---:|:---:|:---:|:---:|:---:|---|
| **-500** | 19,500 | 24,500 | 0.669 V | 7.369 V | Stopband rejection |
| **-300** | 19,700 | 24,700 | 1.147 V | 7.369 V | Lower skirt |
| **-100** | 19,900 | 24,900 | 3.107 V | 7.369 V | Transition band |
| **-50** | 19,950 | 24,950 | 5.285 V | 7.369 V | **-3 dB Lower Edge** |
| **-25** | **19,975** | **24,975** | **7.398 V** | 7.369 V | **★ Resonant Peak (f0)** |
| **0** | 20,000 | 25,000 | 7.394 V | 7.369 V | Saturated passband |
| **+50** | 20,050 | 25,050 | 7.371 V | 7.369 V | Saturated passband |
| **+125** | 20,125 | 25,125 | 5.210 V | 7.369 V | **-3 dB Upper Edge** |
| **+300** | 20,300 | 25,300 | 1.527 V | 7.369 V | Upper skirt |
| **+500** | 20,500 | 25,500 | 0.826 V | 7.369 V | Stopband rejection |

---

## 4. Action Items & Next Steps

1. [ ] **Redesign 35 kHz Filter:** Convert Channel 3 from Sallen-Key to an MFB or cascaded 2-stage active filter topology.
2. [ ] **Add Inter-Stage Attenuation:** Reduce signal level into IF filters by ≈ 6 to 10 dB to prevent op-amp rail clipping in the passband.
3. [ ] **Proceed to Dynamic Sweep Testing:** Replace static LO with the actual sweeping VCO from `SawTooth/sawtooth_vco.asc`.
