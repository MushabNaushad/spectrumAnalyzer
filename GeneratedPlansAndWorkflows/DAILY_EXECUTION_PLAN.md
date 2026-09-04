# Dual-Track Execution Plan: LTSpice Simulation & Breadboard Implementation

> **Objective:** Complete 100% of the circuit design and simulation verification in LTSpice while simultaneously building and testing modular subcircuits on the breadboard using available components.

---

## 1. Inventory & Component Mapping

Based on the components bought (`BoughtComponents/` receipts and `datasheets/`):

| Component | Quantity | Role in Spectrum Analyzer | Notes / Constraints |
|---|:---:|---|---|
| **TL072** (Dual JFET Op-Amp) | **10** (20 op-amps) | Preamp buffer, IF active filters, envelope buffer, sweep integrator | 3 MHz GBW, 13 V/µs slew rate. Standardized across all active filter and buffer stages. |
| **XR2206 / SR2206** (VCO / Func Gen IC) | **3** | Sweeping Local Oscillator (LO) | Can generate pure sine/triangle waves from 0.01 Hz to >300 kHz with voltage-controlled sweep. |
| **2N2222 NPN Transistors** | **15** | Discrete Gilbert Cell Active Mixer | Matches the schematic in `datasheets/Screenshot from 2026-08-29 16-23-28.png`. |
| **2N3904 NPN Transistors** | **10** | VCO reset switch, buffer switches | Used in `SawTooth/sawtooth_vco.asc` for capacitor discharge. |
| **ADS1115** (16-Bit I2C ADC Module) | **2** | Digitizer for Envelope Y-Axis and Sweep X-Axis | Plugs into Arduino/ESP32/Raspberry Pi for digital plotting. |
| **PAM8403** (3W Audio Amp w/ Volume) | **1** | Audio output monitor / scaling | Useful for listening to downconverted IF audio tones. |
| **1 µF / 50V Electrolytic Capacitors** | **20** | Power rail decoupling, low-freq AC coupling, integrator timing | For 25 kHz high-Q filters, smaller film/ceramic caps (1 nF to 10 nF) are needed from lab stock. |
| **Resistors (Assorted Values)** | 400+ | Biasing, feedback networks, filter resistors | 10 Ω, 2.2 kΩ, 2.8 kΩ, 3.3 kΩ, 4.7 kΩ, 10 kΩ, 15 kΩ, 47 kΩ, 68 kΩ, 100 kΩ, 10 MΩ. |
| **830-Point Breadboards** | **2** | Hardware prototyping | |

---

## 2. Track 1: LTSpice Simulation Track

> For exact component calculations, formulas, and step-by-step SPICE directives for all 6 simulation types (AC, Monte Carlo, Worst-Case, Temperature, Linearity/FFT, and Master Sweep), refer to the detailed companion document:  
> 📄 **[`LTSPICE_SIMULATION_GUIDE.md`](LTSPICE_SIMULATION_GUIDE.md)**

```
                  TRACK 1: SIMULATION WORKFLOW
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │ Task S1: Convert Op-Amp models to TL072 in all schematics                   │
 │ Task S2: Redesign 35 kHz Filter with MFB Topology (Fix Oscillation)        │
 │ Task S3: Replace Behavioral Multiplier with 2N2222 Gilbert Cell Mixer       │
 │ Task S4: Integrate Sawtooth VCO to Mixer LO port                            │
 │ Task S5: Add Video Bandwidth (VBW) Low-Pass Smoothing Filter               │
 │ Task S6: Run Master Multi-Tone Sweep & Plot Output Spectrum                │
 └─────────────────────────────────────────────────────────────────────────────┘
```

### Key Simulation Milestones:
1. **Task S1 (Op-Amp Standardization):** Update all op-amps to TL072 models. Add ±9V or ±12V power rails with 0.1 µF decoupling.
2. **Task S2 (MFB 35 kHz Filter):** Replace oscillating Sallen-Key Channel 3 with an inverting Multiple Feedback (MFB) bandpass filter (Q = 175, f0 = 35 kHz, C1 = C2 = 1 nF, R1 = 398 kΩ, R2 = 13 Ω, R3 = 1.59 MΩ). Verify phase margin with `.ac dec 200 10k 50k`.
3. **Task S3 (2N2222 Gilbert Cell Mixer):** Build `02_Gilbert_Mixer.asc` matching the transistor schematic from `datasheets/`. Verify two-tone IMD3 and sum/diff mixing products (15 kHz and 25 kHz).
4. **Task S4 (VCO Integration):** Connect `SawTooth/sawtooth_vco.asc` ramp to LO modulator and verify clean switching of the upper quad in the Gilbert cell.
5. **Task S5 (Envelope & VBW Filter):** Add diode peak detector followed by selectable RC low-pass filter (50 Hz / 200 Hz / 1 kHz).
6. **Task S6 (Master End-to-End Simulation):** Combine into `SpectrumAnalyzer_Master.asc`. Apply multi-tone input (4 kHz + 7 kHz + 12 kHz) and verify that three distinct, resolved peaks appear in X-Y display mode.

---

## 3. Track 2: Breadboard Hardware Track

**Mission:** Breadboard and independently validate the modular hardware blocks using an oscilloscope and function generator.

```
                  TRACK 2: HARDWARE WORKFLOW
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │ Module H1: Power Rail Distribution & Decoupling Setup                       │
 │ Module H2: Sawtooth VCO / XR2206 Sweep Generator Bring-Up                   │
 │ Module H3: 2N2222 Gilbert Cell Active Mixer Assembly & Test                 │
 │ Module H4: TL072 Active Bandpass Filter Tuning & Verification               │
 │ Module H5: Diode Envelope Detector & Output Buffer                          │
 │ Module H6: Cascaded Subsystem Test (Mixer + Filter + Detector)              │
 └─────────────────────────────────────────────────────────────────────────────┘
```

### Detailed Steps for Hardware Team:

#### Module H1: Breadboard Power Supply Distribution
* Connect dual bench power supply: +12V, GND, -12V (or ±9V).
* Place **1 µF decoupling capacitors** between +VCC and GND, and between -VEE and GND near every TL072 IC (Pins 8 and 4).

#### Module H2: Local Oscillator (LO) Generator Bring-Up
* **Option A (Discrete VCO):** Build `sawtooth_vco.asc` using 1x TL072 + 1x 2N3904 + resistors (100 kΩ, 10 kΩ, 60 kΩ).
  * *Test:* Connect scope to integrator output. Verify linear ramp from 0V to 5V with fast reset. Adjust R1 to achieve a sweep rate of ≈ 10 Hz to 50 Hz.
* **Option B (XR2206 IC):** Insert XR2206 into breadboard. Connect timing cap and resistor to produce a 20 kHz to 50 kHz sine wave.

#### Module H3: 2N2222 Gilbert Cell Mixer Build
* Build the 6-transistor Gilbert cell using your 2N2222 transistors and the resistor ladder (3.3 kΩ, 2.2 kΩ, 2.2 kΩ, 1.5 kΩ, and 2.2 kΩ collector load resistors).
* **Hardware Test Procedure:**
  1. Set Bench Function Generator 1 to 5 kHz sine, 200 mVpp -> Connect to RF Input.
  2. Set Bench Function Generator 2 (or your LO from Module H2) to 20 kHz sine, 500 mVpp -> Connect to LO Input.
  3. Probe Mixer Output on the oscilloscope.
  4. Switch scope to **FFT mode** (Math -> FFT).
  5. **Verification:** Confirm peaks at **15 kHz** (20 - 5) and **25 kHz** (20 + 5).

#### Module H4: TL072 IF Bandpass Filter Build
* Build the 25 kHz Bandpass Filter using 1x TL072 IC.
* *Note on Capacitors:* If 1 nF ceramic caps are not in the bought kit, obtain 1 nF / 10 nF film or ceramic capacitors from the lab bin. (Do not use 1 µF electrolytic caps for high-frequency resonant tuning!).
* **Hardware Test Procedure:**
  1. Connect Function Generator directly to the filter input (100 mVpp sine).
  2. Sweep generator frequency slowly from 23 kHz to 27 kHz.
  3. Watch the output amplitude peak on the oscilloscope.
  4. Record the resonant peak frequency f0 and calculate -3 dB bandwidth (delta_f where amplitude drops to 70.7% of peak).

#### Module H5: Envelope Detector & Output Buffer
* Connect standard diode (1N4148 or equivalent signal diode) in series with 10 nF cap and 22 kΩ resistor to ground.
* Feed into a TL072 configured as a unity-gain voltage follower (buffer).
* **Test:** Feed a 25 kHz AM-modulated signal into the detector; observe smooth rectified envelope on the oscilloscope.

#### Module H6: Cascaded Chain Integration
* Connect **Mixer Output -> 25 kHz Filter Input -> Envelope Detector**.
* Input a 5 kHz RF tone.
* Manually tune the LO frequency across 20 kHz.
* **Grand Verification:** As LO crosses 20 kHz, watch the Envelope Detector output spike cleanly from 0V -> Peak -> 0V!
