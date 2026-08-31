# LTSpice Comprehensive Simulation & Circuit Implementation Guide

> **Project:** Discrete Superheterodyne Spectrum Analyzer  
> **Target Architecture:** Audio-to-Ultrasound (0 to 30 kHz RF Input, 25 kHz / 35 kHz Intermediate Frequency)  
> **Key Active Components:** TL072 Dual JFET Op-Amps, 2N2222 / 2N3904 NPN Transistors, 1N4148 Diodes

---

## 1. Complete System Architecture & Schematic Roadmap

The complete simulation is organized into five modular subcircuits that integrate into a single master schematic (`SpectrumAnalyzer_Master.asc`):

```
                                  MASTER SIGNAL CHAIN
┌──────────────┐     ┌──────────────┐     ┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│ 1. RF Input  │────▶│ 2. Discrete  │────▶│ 3. Active IF │────▶│ 4. Envelope │────▶│ 5. Video BW  │──▶ Y_OUT
│ Preamp/Buffer│     │ Gilbert Mixer│     │ Filter Bank  │     │ Detector     │     │ Low-Pass     │    (Amplitude)
└──────────────┘     └──────┬───────┘     └──────────────┘     └──────────────┘     └──────────────┘
                            │
                     ┌──────┴───────┐
                     │ 6. Sawtooth  │─────────────────────────────────────────────────────────────────▶ X_OUT
                     │ VCO (LO Gen) │                                                                     (Frequency)
                     └──────────────┘
```

---

## 2. Block-by-Block Circuit Specifications & Implementation Details

### Block 1: RF Input Preamp & Attenuator (`01_RF_Frontend.asc`)
* **Function:** Buffer high-impedance input sources, scale weak audio signals (10 mV to 1 Vrms), and prevent mixer overdrive.
* **Topology:** Non-inverting TL072 op-amp amplifier with selectable gain (1x, 10x) and input protection.
* **Component Values:**
  * **Op-Amp:** U1 = TL072 (powered from ±12V or ±9V split supplies).
  * **Input Coupling:** Cin = 1 µF in series with 100 Ω protection resistor.
  * **Input Bias Resistor:** Rbias = 1 MΩ to ground.
  * **Feedback Resistors:** Rf = 9 kΩ (or 10 kΩ), Rg = 1 kΩ (Gain = 1 + Rf/Rg = 10x / +20 dB).
  * **Output DC Blocking:** Cout = 1 µF connecting to the mixer RF input.

---

### Block 2: Discrete Gilbert Cell Four-Quadrant Mixer (`02_Gilbert_Mixer.asc`)
* **Function:** Linear multiplication of incoming RF tone (f_RF) with the sweeping local oscillator (f_LO) to generate sum (f_LO + f_RF) and difference (|f_LO - f_RF|) products.
* **Topology:** 6-transistor active Gilbert cell using 2N2222 NPN transistors.

```
                    +12V (or +9V)
                      │
           ┌──────────┴──────────┐
           │ R12 (2.2k)          │ R7 (2.2k)
           ├───────────────┐     ├───────────────┐
           │               │     │               │
        ┌──┴──┐         ┌──┴──┐┌──┴──┐         ┌──┴──┐
        │Q_LO1│         │Q_LO2││Q_LO3│         │Q_LO4│  <-- Upper Switching Quad
        └──┬──┘         └──┬──┘└──┬──┘         └──┬──┘      (Driven by LO differential)
           │               │      │               │
           └───────┬───────┘      └───────┬───────┘
                   │                      │
                ┌──┴──┐                ┌──┴──┐
                │Q_RF1│                │Q_RF2│          <-- Lower Differential Pair
                └──┬──┘                └──┬──┘              (Driven by RF input)
                   │                      │
                   └──────────┬───────────┘
                              │
                           ┌──┴──┐
                           │Q_Tail│                     <-- Tail Current Source
                           └──┬──┘
                              │ R5 (390 Ω)
                             GND
```

* **Component Values & Biasing Ladder:**
  * **Supply:** +9V (or +12V) single-supply (with virtual ground) or dual ±9V.
  * **Bias String (top to bottom):** R1 = 3.3 kΩ, R2 = 2.2 kΩ, R4 = 2.2 kΩ, R3 = 1.5 kΩ.
  * **Base Decoupling Resistors:** R8, R9, R10, R11, R6 = 10 kΩ.
  * **Bypass Capacitors:** C1, C2, C3 = 10 µF electrolytic.
  * **LO Coupling Capacitors:** C4, C5 = 100 nF.
  * **Collector Load Resistors:** R12 = 2.2 kΩ, R7 = 2.2 kΩ.
  * **Single-ended Output:** Taken from collector of Q_LO3 / Q_LO4 through a 1 µF coupling capacitor.

---

### Block 3: IF Active Bandpass Filter Bank (`03_IF_Filter_Bank.asc`)
* **Function:** Ultra-narrowband selection of the intermediate frequency (f_IF). Only signals converting to this exact frequency pass through.

#### Channel A: 25 kHz Sallen-Key Bandpass Filter
* **Specifications:** Center Frequency f0 = 25,000 Hz, Bandwidth BW = 200 Hz, Q = 125.
* **Component Values:**
  * **Op-Amps:** U1, U2, U3 = TL072 (±9V or ±12V rails).
  * **Timing Resistors:** R3 = R5 = 796 kΩ (tol = 1%).
  * **Gain Resistor:** R4 = 6.34 kΩ (tol = 1%).
  * **Tuning Capacitors:** C3 = C4 = 1.0 nF (C0G/NP0 ceramic or film).
  * **Input Attenuation Network:** Voltage divider with R_div1 = 10 kΩ, R_div2 = 2.2 kΩ (-15 dB reduction to prevent passband clipping).

#### Channel B: 35 kHz Multiple Feedback (MFB) Bandpass Filter (Redesigned)
* **Specifications:** Center Frequency f0 = 35,000 Hz, Bandwidth BW = 200 Hz, Q = 175, Inverting gain Av = 2.0 (+6 dB).
* **Why MFB:** MFB uses inverting negative feedback around a single TL072 op-amp, completely eliminating the positive-feedback phase-shift oscillation found in the Sallen-Key version.
* **Component Values Calculation:**
  ```text
  C1 = C2 = 1.0 nF

  R3 (feedback) = Q / (pi * f0 * C)
                = 175 / (pi * 35000 * 1e-9)
                = 1.591 MΩ  (Use standard 1.58 MΩ or 1.6 MΩ)

  R1 (input)    = R3 / (2 * Av)
                = 1.591 MΩ / (2 * 2.0)
                = 397.8 kΩ  (Use standard 392 kΩ or 402 kΩ)

  R2 (ground)   = R1 / (2 * Q^2 * Av - 1)
                ≈ R3 / (4 * Q^2)
                = 1.591 MΩ / (4 * 175^2)
                = 12.98 Ω   (Use standard 13 Ω or 10 Ω + 3 Ω in series)
  ```

---

### Block 4: Precision Diode Envelope Detector (`04_Envelope_Detector.asc`)
* **Function:** Demodulate the 25 kHz / 35 kHz IF AC burst into a unipolar DC baseband voltage.
* **Topology:** Active/passive half-wave diode rectifier with RC peak-hold and high-impedance JFET buffer.
* **Component Values:**
  * **Rectifier Diode:** D1 = 1N4148 (or 1N5711 Schottky for lower forward drop ≈ 0.25V).
  * **Reservoir Capacitor:** C1 = 10 nF.
  * **Bleed / Discharge Resistor:** R1 = 22 kΩ (Decay time constant tau = R1 * C1 = 220 µs).
  * **Output Buffer:** U1 = TL072 configured as unity-gain voltage follower (Vout = Vin+).

---

### Block 5: Video Bandwidth (VBW) Smoothing Low-Pass Filter (`05_VBW_Filter.asc`)
* **Function:** Strip residual 25 kHz mixer ripple and smooth noise floor fluctuations.
* **Topology:** Selectable 1st-order active RC low-pass filter.
* **Component Values:**
  * **Low VBW (fc ≈ 50 Hz):** R = 33 kΩ, C = 100 nF (Maximum noise smoothing; requires slow sweep >= 200 ms).
  * **Medium VBW (fc ≈ 200 Hz):** R = 10 kΩ, C = 82 nF.
  * **High VBW (fc ≈ 1 kHz):** R = 10 kΩ, C = 15 nF (Fast sweep response).

---

### Block 6: Sawtooth Sweep Generator & VCO (`06_Sawtooth_VCO.asc`)
* **Function:** Generate a linear 0V to 5V sweep ramp (X-axis display driver) and convert it into the swept LO sinusoid (f_LO = 20 kHz to 50 kHz).
* **Topology:** TL072 Miller Integrator + TL072 Schmitt Trigger Comparator + 2N3904 NPN Discharge Switch.
* **Component Values:**
  * **Integrator:** U1A (TL072), C_time = 10 nF, R_charge = 100 kΩ connected to -5V reference (I_charge = 50 µA).
  * **Comparator:** U1B (TL072), Hysteresis Resistors: R_in = 10 kΩ, R_fb = 60 kΩ.
  * **Reset Switch:** Q1 (2N3904) across C_time with R_base = 10 kΩ.
  * **Ramp Period:** T_sweep = (C * delta_V) / I_charge = (10 nF * 5V) / 50 µA = 1.0 ms (Scale C_time to 100 nF -> 1 µF for 10 ms to 100 ms full display sweeps).
  * **LO Modulator:** Behavioral VCO equation `V=sin(2*pi*(20e3 + 6000*V(ramp))*time)` or XR2206 hardware model.

---

## 3. Comprehensive Simulation Suite: Types, Setup & Directives

Execute the following **six specialized simulation types** in LTSpice to fully validate the design:

---

### Simulation 1: AC Frequency Response & Stability Analysis (`.ac`)

```spice
.ac dec 200 10k 50k
```

* **Purpose:** Verify resonant center frequency (f0), -3 dB bandwidth (BW), filter gain (Q), and confirm unconditional stability.
* **Implementation:**
  1. Insert an AC source `V_in` with `AC 1` at the filter input.
  2. Add SPICE directive: `.ac dec 200 10k 50k`.
  3. Probe `V(IF_out_2)` and `V(IF_out_3)`.
  4. Measure -3 dB points using `.meas` directives:
```spice
.meas AC f_center MAX mag(V(IF_out_2))
.meas AC f_peak_freq WHEN mag(V(IF_out_2))=f_center
.meas AC f_low WHEN mag(V(IF_out_2))=f_center/sqrt(2) FALL=1
.meas AC f_high WHEN mag(V(IF_out_2))=f_center/sqrt(2) RISE=1
.meas AC bw PARAM (f_high - f_low)
```
* **Success Criteria:** Peak frequency = 25,000 ± 100 Hz, Bandwidth = 200 ± 25 Hz, phase response smoothly decreases through -90 degrees at resonance with no unexpected phase jumps.

---

### Simulation 2: Component Tolerance & Monte Carlo Analysis (`mc()`)

```spice
.step param run 1 100 1
```

* **Purpose:** Test how real-world 1% resistors and 5% capacitors cause center frequency drift, bandwidth skew, and gain variation.
* **Implementation:**
  1. Parameterize passive components using the `mc(nominal_value, tolerance)` function:
```spice
* Resistors with 1% tolerance:
.param R3_val = {mc(796k, 0.01)}
.param R4_val = {mc(6.34k, 0.01)}
.param R5_val = {mc(796k, 0.01)}

* Capacitors with 5% tolerance:
.param C3_val = {mc(1n, 0.05)}
.param C4_val = {mc(1n, 0.05)}
```
  2. Set component attributes in LTSpice to `{R3_val}`, `{C3_val}`, etc.
  3. Add the step command: `.step param run 1 100 1`.
  4. Add `.meas` statements to record f0 and V_peak across all 100 runs.
* **Analysis:** Open the Spice Error Log (`Ctrl+L` / `Cmd+L`), right-click -> `Plot .step'ed .meas data`.
* **Success Criteria:** Center frequency remains within 25,000 ± 250 Hz (1.0%) across >95% of runs.

---

### Simulation 3: Worst-Case Sensitivity Analysis (`wc()`)

```spice
.step param run 1 16 1
```

* **Purpose:** Determine the absolute worst-case detuning when all tolerance extremes align unfavorably.
* **Implementation:**
  1. Use the `wc(nominal, tol, index)` function where each binary permutation of `run` sets a component to its maximum or minimum limit:
```spice
.param R3_wc = {wc(796k, 0.01, 1)}
.param R5_wc = {wc(796k, 0.01, 2)}
.param C3_wc = {wc(1n, 0.05, 3)}
.param C4_wc = {wc(1n, 0.05, 4)}
```
  2. Run `.step param run 1 16 1` with `.ac dec 100 23k 27k`.
* **Output:** Quantifies the maximum possible frequency error window (delta_f_max), defining the required calibration trim range.

---

### Simulation 4: Thermal Drift Analysis (`.temp`)

```spice
.step temp 0 70 10
```

* **Purpose:** Evaluate frequency shift caused by op-amp input bias currents (Ib), offset voltage drift (2 µV/°C), BJT Vbe temperature coefficient (-2 mV/°C), and resistor temperature coefficients (TCR).
* **Implementation:**
  1. Add TCR parameters to critical resistors:
```spice
.model RES_PRECISION R (tc1=50e-6)  ; 50 ppm/°C metal film resistor
```
  2. Add temperature step directive: `.step temp 0 70 10`.
  3. Run `.ac dec 100 24k 26k` and observe peak frequency drift delta_f0 / delta_T.
* **Success Criteria:** Total thermal drift over delta_T = 40 °C must be < 50 Hz (< 25% of filter bandwidth).

---

### Simulation 5: Mixer Intermodulation Distortion & Linearity (`.tran` + FFT)

```spice
.tran 0 20m 10m 100n
```

* **Purpose:** Characterize mixer 3rd-order intermodulation distortion (TOI / IP3), conversion gain, and port-to-port isolation.
* **Implementation:**
  1. Two-Tone RF Input: Sum of two closely spaced tones:
     `V_RF(t) = 0.1*sin(2*pi*5000*t) + 0.1*sin(2*pi*5200*t)`
  2. LO Input: Single tone `V_LO(t) = 0.5*sin(2*pi*20000*t)`.
  3. Run transient analysis for 20 ms with tight maximum timestep (100 ns).
  4. In the waveform viewer: `View -> FFT` on `V(mix_out)`.
  5. Measure amplitudes of fundamental mixing products (25.0 kHz, 25.2 kHz) and 3rd-order intermodulation products (24.8 kHz, 25.4 kHz).
* **Success Criteria:** Spurious-Free Dynamic Range (SFDR) >= 45 dBc at 100 mVpk input.

---

### Simulation 6: Dynamic Frequency Sweep & Chirp Response (`.tran`)

```spice
.tran 0 100m 0 1u
```

* **Purpose:** Verify dynamic sweep behavior—ensuring that sweeping the LO over time does not cause filter ringing, peak amplitude drop, or frequency lag.
* **Implementation:**
  1. Set RF Input: 3 simultaneous tones at 4 kHz, 7 kHz, and 12 kHz (100 mVpk each).
  2. Drive VCO LO with a 100 ms sawtooth ramp (0V to 5V) sweeping f_LO from 20 kHz to 40 kHz.
  3. Run `.tran 0 100m 0 1u`.
  4. Plot `V(env_out)` vs. Time.
  5. In LTSpice waveform viewer: Click on horizontal axis -> change quantity plotted from `time` to `V(ramp)` (simulates X-Y oscilloscope display mode).
* **Success Criteria:** 
  * Three sharp, symmetric peaks appear at the exact voltage positions corresponding to 4 kHz, 7 kHz, and 12 kHz.
  * No asymmetrical ringing tails on the trailing edges of the peaks (confirms sweep rate is slow enough: df/dt <= BW^2).

---

## 4. Summary Table of Simulations to Complete

| # | Simulation File | Type | Key Directive | Target Deliverable |
|---|---|---|---|---|
| **1** | `03_IF_Filter_Bank.asc` | AC Frequency Response | `.ac dec 200 10k 50k` | Measure f0 = 25 kHz, BW = 200 Hz, Q = 125, verify stability |
| **2** | `03_IF_Filter_Bank.asc` | Monte Carlo Tolerance | `.step param run 1 100 1` | Verify f0 spread < ±250 Hz across 100 runs with 1% resistors |
| **3** | `03_IF_Filter_Bank.asc` | Temperature Sweep | `.step temp 0 70 10` | Quantify thermal drift < 50 Hz over operating range |
| **4** | `02_Gilbert_Mixer.asc` | Two-Tone Linearity | `.tran 0 20m 10m 100n` + FFT | Verify mixer IMD3 < -45 dBc and conversion gain |
| **5** | `06_Sawtooth_VCO.asc` | Ramp Linearity | `.tran 0 50m 0 1u` | Verify linear 0V to 5V ramp with <0.5 ms reset flyback |
| **6** | `SpectrumAnalyzer_Master.asc` | Master Dynamic Sweep | `.tran 0 100m 0 1u` | Full 3-tone spectrum resolved on X-Y display mode |
