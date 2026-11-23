# Analog Heterodyne Spectrum Analyzer

> A swept superheterodyne audio spectrum analyzer built from discrete analog circuits, with an ESP32 used only to digitise and display the result.
> Designed against the architecture described in the [NI Application Note: Super-Heterodyne Signal Analyzer](References/Super%20hetrodyne%20signal%20analyuzer.pdf).

**Team Quantum Resonators (Group 15)** — EN2091 Laboratory Practice and Projects, Department of Electronic & Telecommunication Engineering, University of Moratuwa.

| Index No | Name |
|---|---|
| 240442V | Mushab M.N.A. |
| 240557D | Rathees S. |
| 240435D | Mindiya A.S.G.M. |
| 240642J | Tennekone A.D.T.M.S.H. |

---

## Table of Contents
- [Overview](#overview)
- [Architecture](#architecture)
- [Block by Block](#block-by-block)
- [Specifications](#specifications)
- [Test Results](#test-results)
- [Firmware](#firmware)
- [Test Tone Generator](#test-tone-generator)
- [Project Files](#project-files)
- [Known Limitations](#known-limitations)
- [Future Work](#future-work)
- [Project Status](#project-status)

---

## Overview

The device displays the 0 to 5 kHz spectrum of a small-amplitude audio signal on a 3.5-inch TFT display. The input is amplified and low-pass filtered, then mixed in a discrete Gilbert cell with a swept voltage-controlled oscillator (VCO). Two fixed band-pass filters at different intermediate frequencies (IFs) select one frequency component at a time, so a single sweep covers the band on two channels:

- **Channel 1:** 0 to 2.5 kHz (IF ≈ 5 kHz)
- **Channel 2:** 2.5 to 5 kHz (IF ≈ 7.4 kHz)

Envelope detectors turn each IF output into a DC level. An ADS1115 ADC samples both channels together with the sweep ramp, and an ESP32 plots amplitude against frequency.

The whole system is built on a custom 2-layer PCB inside a 3D-printed PLA enclosure, and runs from a single 9 V DC adapter.

---

## Architecture

```
 Audio in (XLR)
      │
      ▼
┌──────────────┐   ┌──────────────┐   ┌──────────────┐        ┌───────────────────┐   ┌──────────────┐
│ Preamplifier │──▶│ 4th-order LPF│──▶│ Gilbert cell │──┬────▶│ BPF 1 (≈5.04 kHz) │──▶│ Envelope     │──▶ AIN0 ─┐
│ ×28.5–×83.5  │   │ Butterworth  │   │ mixer        │  │     │ Tow-Thomas, 106 Hz│   │ detector 1   │          │
└──────────────┘   │ fc ≈ 4.9 kHz │   └──────▲───────┘  │     └───────────────────┘   └──────────────┘          │
                   └──────────────┘          │          │     ┌───────────────────┐   ┌──────────────┐          │
                                             │          └────▶│ BPF 2 (7.40 kHz)  │──▶│ Envelope     │──▶ AIN1 ─┤
                                             │                │ Tow-Thomas, 106 Hz│   │ detector 2   │          │
                                             │                └───────────────────┘   └──────────────┘          ▼
                                     ┌───────┴──────┐                                                   ┌──────────────┐
                                     │ VCO (ICL8038)│                                                   │ ADS1115 ADC  │
                                     │ ≈5 → 2.5 kHz │                                                   │ 16-bit, I²C  │
                                     └───────▲──────┘                                                   └──────┬───────┘
                                             │                                                                 │
                                     ┌───────┴──────┐                                                   ┌──────▼───────┐   ┌──────────────┐
                                     │  Sawtooth    │─────────────────── ramp (AIN2) ──────────────────▶│ ESP32-WROOM  │──▶│ 3.5" TFT     │
                                     │  generator   │                                                   │ -32D         │SPI│ ST7796S      │
                                     └──────────────┘                                                   └──────────────┘   └──────────────┘
```

Heterodyne principle: the mixer produces sum and difference products, and the band-pass filter selects the component that lands on the IF:

$$f_{IN} = f_{IF} - f_{LO}$$

As the LO sweeps down, each input frequency moves onto the IF in turn.

### Power

```
9 V DC adapter ──▶ reverse-polarity diode ──┬──▶ +9 V analog rail (VCC)
                                           ├──▶ MC34063A inverter ──▶ −9.7 V (VEE)
                                           └──▶ MC34063A step-down ──▶ 5 V ──▶ AMS1117 ──▶ 3.3 V (VDD)
```

Both MC34063A converters switch at about 40 kHz (C_T = 820 pF), well above the signal and IF band.

---

## Block by Block

### 1. Input preamplifier
AC-coupled non-inverting amplifier with clamp diodes to the rails. A front-panel potentiometer sets the gain to catch small signals:

$$A_v = 1 + \frac{25\,\text{k}\Omega + R_{pot}}{909\,\Omega}, \quad R_{pot} = 0\text{–}50\,\text{k}\Omega \;\Rightarrow\; \times28.5\text{ to }\times83.5\ (29\text{–}38\text{ dB})$$

### 2. Anti-image low-pass filter
Two cascaded unity-gain Sallen-Key stages (Q ≈ 0.55 and 1.30) give a 4th-order Butterworth response with a cutoff of about 4.9 kHz and an 80 dB/decade roll-off.

### 3. Mixer
Discrete Gilbert cell (2N2222A). For small inputs it acts as a linear multiplier:

$$V_{out} = k\tanh\!\left(\frac{V_{RF}}{2V_T}\right)\tanh\!\left(\frac{V_{LO}}{2V_T}\right) \approx k'\,V_{RF}V_{LO}$$

The LO is attenuated to about 50 mV peak-to-peak before the mixer.

### 4. IF band-pass filters
Two Tow-Thomas biquads (NE5532), chosen for stable high-Q operation and independent control of f₀, bandwidth and gain:

$$f_0 = \frac{1}{2\pi R_F C}, \quad BW = \frac{1}{2\pi R_B C}, \quad G = \frac{R_B}{R_G}$$

| | Channel 1 | Channel 2 |
|---|---|---|
| R_F | 3.16 kΩ | 2.15 kΩ |
| C | 10 nF | 10 nF |
| R_B | 150 kΩ | 150 kΩ |
| Design f₀ | 5.04 kHz | 7.40 kHz |
| Design Q | 47.5 | 69.8 |
| Bandwidth | ≈106 Hz | ≈106 Hz |

### 5. Envelope detectors
Precision half-wave rectifier (TL072C, 1N4148) followed by an RC peak-hold filter and a buffer, giving a DC level proportional to the spectral magnitude.

### 6. Sawtooth generator
TL072 integrator with a Schmitt-trigger comparator and a 2N3904 flyback transistor that discharges the integrator capacitor. The ramp runs from 0 to about 3.55 V with a period of about 0.56 s.

### 7. VCO
ICL8038 swept through its FM sweep input (pin 8). A non-inverting summing amplifier maps the sawtooth onto the allowed sweep range:

$$f = \frac{3\,V_{SWEEP}}{2\,R\,C\,V_{SUPPLY}}$$

### 8. Digitising and display
The ADS1115 reads AIN0 (channel 1), AIN2 (ramp) and AIN1 (channel 2) in sequence at 860 SPS, giving about 126 sets per second (125 µV/LSB). The ESP32 uses one core for sampling and one for drawing. The ramp voltage sets the horizontal position, so each reading is plotted at its frequency.

---

## Specifications

| Parameter | Value |
|---|---|
| Frequency range | 0 to 5 kHz, two channels |
| Resolution bandwidth | ≈106 Hz (design) |
| Sweep period / refresh | ≈0.56 s (about 2 updates per second) |
| Preamplifier gain | 29 to 38 dB |
| Recommended input | 5 to 50 mV peak-to-peak, never above 200 mV peak |
| Input connector | XLR socket |
| Display | 3.5-inch 480 × 320 colour TFT (ST7796S, SPI) |
| Supply | 9 V DC, centre positive |
| PCB | 2-layer, 105 × 145 mm, ground plane on the bottom layer |
| Enclosure | 3D-printed PLA, 175 × 135 × 67 mm |

---

## Test Results

- Tested with sine and square waveforms from a PC audio output at about 50 mV peak-to-peak. Each tone shows as a clear peak at its frequency.
- Component tolerances shift the measured values from the design:
  - Channel 1 IF centre frequency: about **4.85 kHz** (design 5.04 kHz).
  - VCO sweep: from a little above **5 kHz down to about 2.5 kHz** (calculated 5.1 → 2.8 kHz).
  - Sweep period: about **0.56 s** (calculated 0.44 s).
  - Displayed ranges: about 0 to 2.4 kHz (channel 1) and 2.5 to 5 kHz (channel 2).
- Measured rails: VEE about −9.7 V. The ADC delivers about 126 sample sets per second.
- A small amount of noise appears as low ripple on the baseline, and a tone above 5 kHz (for example 9 kHz) can show as a small image peak.

### Simulation
The Gilbert cell, the input filter and amplification stages, and the output extraction stage were simulated in LTspice. The VCO was simulated with behavioural sources, because no ICL8038 model is available in LTspice.

---

## Firmware

PlatformIO project for the ESP32-WROOM-32D (`firmware_esp32/`).

| Build | Command | Purpose |
|---|---|---|
| `tft_st7796` (default) | `pio run -e tft_st7796 -t upload` | **Firmware used on the device** |
| `tft_ili9488` | `pio run -e tft_ili9488 -t upload` | Same panel with the ILI9488 driver |
| `udp_web` | `pio run -e udp_web -t upload` | Bring-up build: plot in a PC browser over Wi-Fi |
| `i2c_diag` | `pio run -e i2c_diag -t upload` | Bring-up build: I²C and ADC diagnostics |

Key files:
- `src/main_tft.cpp` — main loop (core 1): reads samples and draws them.
- `lib/ScopeCore/ADS1115.*` — register-level ADS1115 driver (no external library).
- `lib/ScopeCore/ScopeSampler.*` — sampling task on core 0 (AIN0 → AIN2 → AIN1), with I²C bus recovery.
- `lib/ScopeCore/SweepTracker.*` — learns the ramp maximum over the first sweeps, then detects each retrace.
- `src/xyplot_tft.h` — display: channel 1 (yellow) on the left half, channel 2 (cyan) on the right half.
- `include/config.h` — pins, channel mapping, Y scale (0.5 V/div) and axis labels.

---

## Test Tone Generator

`tone_generator/tone_generator_7k.py` is a PC tool for generating test tones (sine, square, triangle, sawtooth) through the computer's audio output. It includes an output device selector and an "analyzer view" that shows what the spectrum should look like at about 106 Hz resolution.

---

## Project Files

| File | Contents |
|---|---|
| `Sheet1.SchDoc` | Altium schematic |
| `PCB1.PcbDoc` | Altium PCB layout (as fabricated) |
| `Quantum_resonators_PCB_Print_files.zip` | Gerber and drill files sent for fabrication |
| `Quantum_Resonators_Enclosure_1.STL` | Enclosure base |
| `Quantum_Resonators_Enclosure_2.STL` | Enclosure top panel with logo |
| `15_Quantum_Resonators_EN2091_Analog_Project.pdf` | Project report |
| `15_Quantum_Resonators_Datasheet.pdf` | Datasheet |
| `15_Quantum_Resonators_User_Manual.pdf` | User manual |
| `firmware_esp32/` | ESP32 firmware (PlatformIO) |
| `tone_generator/` | PC test tone generator |

> Note: several component values were changed on the board during bring-up. The schematic shows the original design values; see the report and datasheet for the fitted values.

---

## Known Limitations

- The analog rails are not separately regulated, so supply variation shifts the VCO frequency.
- The 4th-order input filter gives only about 15 dB rejection at the bottom of the image band, so strong tones above 5 kHz can appear as small false peaks.
- Component tolerances move the IF centre frequencies and the sweep range away from the design values.
- The single swept filter per channel sets a fixed trade-off between resolution, range and refresh rate.
- The bottom ground plane runs to the board edge with no pull-back.

---

## Future Work

- Regulate the positive and negative supply rails to suppress power spikes.
- Use a higher-order input filter for more rejection of frequencies above 5 kHz.
- Add an output gain control to scale the displayed signal.
- Add a timing controller to increase resolution at the cost of range.
- Use more band-pass filter paths so each covers a smaller sweep range than the current 2.5 kHz, for higher resolution or a faster refresh rate while keeping the filters' settling time.

---

## Project Status

**Complete.** The device has been designed, simulated, built on a custom PCB, assembled in its enclosure and tested.

| Block | Status |
|---|---|
| Preamplifier and anti-image LPF | ✅ Built and tested |
| Gilbert cell mixer | ✅ Built and tested |
| Sawtooth generator and ICL8038 VCO | ✅ Built and tested |
| Tow-Thomas IF filters (2 channels) | ✅ Built and tested |
| Envelope detectors | ✅ Built and tested |
| ADS1115 + ESP32 + TFT display | ✅ Built and tested |
| Power supply (inverter, step-down, LDO) | ✅ Built and tested |
| PCB and enclosure | ✅ Fabricated and assembled |
