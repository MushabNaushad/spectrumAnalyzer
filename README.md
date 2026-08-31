# Spectrum Analyzer — Project README

> A hardware superheterodyne spectrum analyzer built from discrete analog components, designed and simulated in LTSpice.  
> Inspired by and designed against the architecture described in the [NI Application Note: Super-Heterodyne Signal Analyzer](References/Super%20hetrodyne%20signal%20analyuzer.pdf).

---

## Table of Contents
- [Architecture Overview](#architecture-overview)
- [Project Status](#project-status)
- [Current Work — Block by Block](#current-work--block-by-block)
- [Known Issues & Simulation Findings](#known-issues--simulation-findings)
- [Implementation Plan — Remaining Work](#implementation-plan--remaining-work)
- [Design Notes & Constraints](#design-notes--constraints)

---

## Architecture Overview

The instrument follows the classic swept superheterodyne architecture. An incoming RF signal is mixed down to an intermediate frequency (IF) by a swept local oscillator (LO/VCO). A narrow bandpass filter selects the IF, an envelope detector converts it to a magnitude trace, and the LO sweep ramp is used to synchronize the horizontal (frequency) display axis.

```
RF Input
   │
   ▼
┌──────────────┐     ┌──────────────┐     ┌─────────────────────┐
│  [TODO]      │     │              │     │   IF Bandpass        │
│  RF          │────▶│    Mixer     │────▶│   Filter Bank        │
│  Preselector │     │    (×2.5)    │     │  ┌──────────────┐   │
└──────────────┘     └──────┬───────┘     │  │ 25 kHz / Q≈125│  │
                            │             │  │ (IF Channel 2)│  │
┌──────────────┐            │             │  └──────────────┘   │
│   Sawtooth   │            │             │  ┌──────────────┐   │
│   VCO        │────────────┘             │  │ 35 kHz / Q≈175│  │
│  (sweeping   │                          │  │ (IF Channel 3)│  │
│   LO)        │                          │  └──────────────┘   │
└──────┬───────┘                          └──────────┬──────────┘
       │                                             │
       │ (tuning ramp)                               ▼
       │                                  ┌─────────────────────┐
       │                                  │  Envelope Detector   │
       │                                  │  (Diode + RC + Buf)  │
       │                                  └──────────┬──────────┘
       │                                             │
       ▼                                             ▼
┌──────────────┐                          ┌─────────────────────┐
│  [TODO]      │                          │  [TODO]              │
│  X-axis      │                          │  Video BW Filter +   │
│  Display     │◀─────────────────────────│  Output / Display    │
│  Driver      │                          └─────────────────────┘
└──────────────┘
```

---

## Project Status

**Overall Completion: ~35%**

| Phase | Block | Status | Notes |
|---|---|:---:|---|
| **Frequency Generation** | Sawtooth VCO (integrator + comparator) | ⚠️ Partial | Core circuit works; LTC1540 comparator variant tested. Flyback, reset switch, temperature stability not validated. |
| **Frequency Mixing** | Analog multiplier (B2 behavioral) | ✅ Working | Behavioral model only — not yet a real hardware mixer. |
| **IF Filtering** | 25 kHz Sallen-Key bandpass (IF Ch. 2) | ⚠️ Working but saturating | Simulated. Center freq error −25 Hz (−0.1%). Output clips at ~7.4 Vpp on ±5V rails in passband. BW measured ~176 Hz vs 200 Hz designed. |
| **IF Filtering** | 35 kHz Sallen-Key bandpass (IF Ch. 3) | ❌ Oscillating | Q=175 exceeds stable limit for Sallen-Key topology with LT1124. Self-oscillates at 7.37 Vpp constantly. Requires topology change. |
| **Envelope Detection** | Diode + 10nF cap + 22kΩ (×2) | ✅ Simulated | Functional in simulation. Not hardware validated. |
| **Filter Scale Management** | AC frequency response / scale circuit | ⚠️ Partial | `FilterAndScaleManagement.asc` exists; AC simulation only. Not integrated into main chain. |
| **Drift Sensitivity** | LO drift test (±500 Hz parametric sweep) | ✅ Complete | Full 41-step sweep run. Analysis documented. |
| **RF Preselector** | Tunable/switched prefilter at RF input | ❌ Not started | Described in reference §8. Required to suppress image and harmonic responses. |
| **Video BW Filter** | Post-detection low-pass smoothing filter | ❌ Not started | Required for noise floor smoothing. Single-pole RC per reference §5. |
| **Display / Output** | X-Y amplitude vs. frequency output | ❌ Not started | Synchronization of LO ramp with envelope output is undefined. |
| **Full Integration** | End-to-end sweep with real VCO connected | ❌ Not started | VCO not yet connected to mixer; behavioral source used as placeholder. |
| **Calibration** | Amplitude flatness, frequency accuracy | ❌ Not started | Required for meaningful measurements. |

---

## Current Work — Block by Block

### 1. Sawtooth VCO — `SawTooth/sawtooth_vco.asc`
**Architecture:** LT1001 op-amp integrator + 2N3904 NPN reset switch + LT1001 (or LTC1540 in variant) comparator.

- **`sawtooth_vco.asc`** — Primary design. Uses two LT1001 op-amps (integrator + comparator). Reset via 2N3904 BJT. Charging from a −5 V reference through 100 kΩ / 1 µF.
- **`sawtooth_vco - Copy.asc`** — Variant using LTC1540 nanopower comparator and an ideal switch (`MYSW`) instead of the 2N3904. Likely exploring faster/cleaner reset.
- **`sawtooth_vco - Copy (2).asc`** — Appears to be the same as primary design (2N3904 reset), possibly a save point.

**Known issues:** Flyback non-linearity, BJT $V_{CE(sat)}$ residual charge, capacitor dielectric absorption. None of these have been simulated with temperature or supply variation. The VCO output is **not yet connected** to the mixer in the main analysis files — a behavioral `sin()` source is used as a placeholder.

---

### 2. Mixer — `Filter + Env Detector/Filter_AP.asc`
**Architecture:** Behavioral voltage source `B2`: `V=V(vco)*V(rf)*2.5`

This is a mathematical ideal multiplier — not a real hardware mixer (e.g., Gilbert cell, diode ring, or SA612). It has no:
- Conversion loss
- LO-to-RF isolation
- Port-to-port feedthrough
- Harmonic products beyond 2nd order

Suitable for simulation of filter behavior, but must be replaced for hardware design.

---

### 3. IF Filter Bank — `Filter + Env Detector/Filter_AP.asc`

Two parallel 2nd-order Sallen-Key active bandpass filters using LT1124 op-amps (±5 V supply):

| Parameter | IF Channel 2 | IF Channel 3 |
|---|---|---|
| Designed center frequency | 25 kHz | 35 kHz |
| Designed bandwidth | 200 Hz | 200 Hz |
| Design Q | 125 | 175 |
| R (timing, ×2) | 796 kΩ (tol=1%) | 796 kΩ (tol=1%) |
| R (gain) | 6.34 kΩ (tol=1%) | 4.53 kΩ (tol=0.1% on R12) |
| R (summing, ×2) | 10 kΩ (tol=1%) | 10 kΩ (tol=1%) |
| C (timing, ×2) | 1 nF | 1 nF |
| Simulated center freq | **24,975 Hz** (−25 Hz, −0.10%) | N/A (oscillating) |
| Simulated BW | **~176 Hz** | N/A (oscillating) |
| Status | ⚠️ Saturating in passband | ❌ Self-oscillating |

---

### 4. Envelope Detectors — `Filter + Env Detector/Filter_AP.asc`
Two identical half-wave envelope detectors, one per IF channel:
- **Diode** (ideal in simulation, D1/D2)
- **10 nF** hold capacitor (C1/C2)
- **22 kΩ** discharge resistor (R1/R2)
- LT1124 unity-gain buffer (U8/U9)

Time constant: $\tau = RC = 22\text{k} \times 10\text{n} = 220\,\mu\text{s}$. This limits the maximum video bandwidth to approximately $\frac{1}{2\pi\tau} \approx 720\text{ Hz}$, which is adequate for a 200 Hz resolution bandwidth filter.

---

### 5. Filter Scale Management — `Filter Management/FilterAndScaleManagement.asc`
An AC frequency response simulation (`.ac dec 100 10 100k`) of a filtering and scaling stage. Contains:
- A UniversalOpAmp2-based active filter
- Dual diode amplitude detector (D1/D2)
- RC output filter (R6=10kΩ, C2=820pF)
- Commented-out `.tran` and `.wave` directives suggesting future audio-rate output testing

**Status:** Isolated AC simulation. Not connected to the main signal chain. Its role in the final architecture is unclear — likely a VBW filter or output scaling stage.

---

## Known Issues & Simulation Findings

> [!CAUTION]
> **IF Channel 3 (35 kHz) is self-oscillating.** The Sallen-Key topology with Q=175 exceeds the stability margin of the LT1124 at 35 kHz. V(IF_out_3) = 7.37 Vpp constantly across all 41 drift steps (σ = 0.00014 V), completely independent of input. This channel is **currently unusable**.

> [!WARNING]
> **IF Channel 2 output saturates at the supply rails** for input signals within the passband. Peak Vpp ≈ 7.4 V on ±5 V supply. The amplitude response is non-linear in the passband, making amplitude measurements unreliable near the center frequency.

> [!WARNING]
> **The VCO is not connected to the mixer.** All current simulations use a behavioral `sin()` source as the LO. The non-linearity, sweep rate variation, and frequency errors of the real sawtooth VCO have not been tested in the full signal chain.

> [!NOTE]
> IF Channel 2 center frequency: 24,975 Hz (−25 Hz, −0.10% error). Within 1% component tolerance. Measured bandwidth: ~176 Hz vs. 200 Hz designed. Asymmetric 3 dB rolloff: −51 Hz on negative drift side, +125 Hz on positive side (partially an artifact of output clipping).

---

## Implementation Plan — Remaining Work

### Phase 1: Fix the 35 kHz IF Filter (Critical)

**Goal:** Replace the oscillating Sallen-Key design with a stable topology.

**Recommended approach — Multiple Feedback (MFB) Bandpass:**

The MFB topology is inherently stable at high Q values because it uses inverting feedback with a single op-amp and does not have the positive feedback sensitivity of Sallen-Key.

For $f_0 = 35\text{ kHz}$, $Q = 175$, $\text{BW} = 200\text{ Hz}$:
$$C_1 = C_2 = 1\text{ nF}, \quad R_1 = \frac{Q}{\pi f_0 C} = \frac{175}{\pi \times 35000 \times 1\text{n}} \approx 1.59\text{ M}\Omega$$
$$R_2 = \frac{1}{2Q^2 \pi f_0 C} \approx 1.3\text{ k}\Omega, \quad R_3 = 2R_1 = 3.18\text{ M}\Omega$$

**Alternative:** Cascade two lower-Q stages (Q≈87 each) using the existing Sallen-Key topology.

**Steps:**
- [ ] Design MFB schematic for 35 kHz / 200 Hz in LTSpice
- [ ] AC sweep verification: confirm Q, center frequency, gain
- [ ] Transient test with mixer output as input
- [ ] Replace U4/U5/U6 in `Filter_AP.asc`

---

### Phase 2: Fix IF Channel 2 Output Saturation

**Goal:** Reduce the signal level hitting the IF filters to keep op-amps out of saturation.

**Options:**
1. **Reduce mixer gain** from ×2.5 to ×0.5 (change behavioral source coefficient). This brings the IF signal to ~1.5 Vpp in-band, well within linear range.
2. **Add a voltage divider / attenuator** between the mixer output and the filter inputs.
3. **Reduce RF input amplitude** to be representative of actual antenna signal levels (mV range, not 1 V).

**Steps:**
- [ ] Model realistic RF input level (e.g., 10 mV instead of 1 V)
- [ ] Adjust mixer gain to match expected signal chain levels
- [ ] Re-run drift test; confirm Vpp stays below 4 V in passband

---

### Phase 3: Connect the Real VCO

**Goal:** Replace the behavioral `sin()` LO source with the actual `sawtooth_vco.asc` circuit.

**Steps:**
- [ ] Resolve `sawtooth_vco.asc` copy confusion — consolidate to single canonical design
- [ ] Validate VCO operating frequency and tuning range in standalone simulation
- [ ] Determine VCO control voltage range and sensitivity ($K_{VCO}$, Hz/V)
- [ ] Connect VCO output to mixer input (replace B1 behavioral source)
- [ ] Run full transient sweep — observe IF output vs. time
- [ ] Check for sweep non-linearity: measure $\frac{df_{LO}}{dt}$ and confirm linearity over sweep range
- [ ] Characterize VCO startup transient; ensure sweep settles before measurement window

---

### Phase 4: Video Bandwidth (VBW) Filter

**Goal:** Add the post-detection low-pass filter to smooth the noise floor (as described in reference §5).

The VBW filter is a single-pole RC low-pass placed after the envelope detector. It trades noise floor smoothing against sweep speed.

For a VBW of approximately 50 Hz:
$$\tau = \frac{1}{2\pi \times 50} \approx 3.2\text{ ms}, \quad R=32\text{ k}\Omega, \quad C=100\text{ nF}$$

**Steps:**
- [ ] Design selectable VBW filter (e.g., 50 Hz / 200 Hz / 1 kHz)
- [ ] Integrate after envelope detector buffers in `Filter_AP.asc`
- [ ] Verify smoothing effect in transient simulation with noise input

---

### Phase 5: RF Preselector (Future)

**Goal:** Add a tunable or switched bandpass prefilter at the RF input to suppress image and harmonic responses (reference §3, §8).

Per the reference, the preselector bandwidth must be **less than one octave** to prevent second-harmonic interference from appearing at the IF.

For a 0–30 kHz tuning range (audio-band spectrum analyzer):
- Low-band filter (0–10 kHz), mid-band (10–20 kHz), high-band (20–30 kHz) switched filter bank
- Or: single tunable active bandpass filter tracking the LO

**Steps:**
- [ ] Determine final RF frequency range of the analyzer
- [ ] Design 2–3 switched bandpass preselectors with < octave BW each
- [ ] Characterize image rejection improvement with and without preselector

---

### Phase 6: Display / Output

**Goal:** Produce a frequency vs. amplitude output synchronized to the LO sweep.

This requires:
1. **X-axis:** A voltage proportional to the instantaneous LO frequency (the VCO tuning ramp)
2. **Y-axis:** The envelope detector output (post-VBW filter)
3. **Display:** Oscilloscope in XY mode, or DAQ capture + Python plotting

**Steps:**
- [ ] Define the LO tuning ramp output node and its voltage-to-frequency scaling
- [ ] Connect envelope output and tuning ramp to XY display or data capture
- [ ] Write Python post-processing script to scale X axis to Hz and Y axis to dBm
- [ ] Validate frequency axis accuracy against known input tones

---

### Phase 7: Full Integration & Calibration

**Steps:**
- [ ] Connect all blocks into single `SpectrumAnalyzer_Full.asc`
- [ ] Run end-to-end sweep with known input frequencies (5 kHz, 10 kHz)
- [ ] Verify signal appears at correct X position on output
- [ ] Characterize amplitude flatness across tuning range
- [ ] Monte Carlo simulation: run 50+ iterations with all component tolerances randomized
- [ ] Confirm >95% of runs show signals within ±1 channel bandwidth of correct position
- [ ] Document final frequency range, RBW, noise floor, and dynamic range

---

## Design Notes & Constraints

| Parameter | Current Design Value | Reference Ideal |
|---|---|---|
| RF input range | 0–~30 kHz (audio band) | Application-specific |
| IF center frequencies | 25 kHz, 35 kHz | Determined by LO tuning range |
| Resolution Bandwidth (RBW) | 200 Hz (both channels) | Selectable: 10–10,000 Hz typical |
| Filter topology | Sallen-Key bandpass | MFB recommended for Q > 50 |
| Op-amp | LT1124 (GBW = 12.5 MHz) | Requires GBW >> Q × f₀ |
| Supply voltage | ±5 V | Limits max Vpp to ~9 V |
| Mixer | Behavioral (ideal) | Real: Gilbert cell or diode ring |
| LO | Behavioral `sin()` / discrete sawtooth | Discrete relaxation oscillator |
| Display | Not implemented | XY oscilloscope or DAQ |

---

*Simulation data: `Filter + Env Detector/` directory. Large `.raw`, `.log`, `.net` files are gitignored.*
