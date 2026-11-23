# ADS1115 X-Y Scope (ESP32-WROOM, PlatformIO)

Sawtooth on AIN2 is the X sweep: 0 V maps to X = 0, and the saw max maps to X = Xmax.
- CH1 (AIN0) is drawn on the left half, X from 0 to Xmax/2.
- CH2 (AIN1) is drawn on the right half, X from Xmax/2 to Xmax.

Y is fixed at 0 to 3.3 V, 0.5 V/div. The saw max is learned over the first 3 retraces (about 2 full ramps), then locked. Press **BOOT** to re-learn it.

The ADS1115 driver is written from scratch (`lib/ScopeCore/ADS1115.*`), with no Adafruit code.

## Two firmwares, one project

| Build | Command | Shows the plot on |
|---|---|---|
| `udp_web` | `pio run -e udp_web -t upload` | PC browser (test build) |
| `tft_st7796` | `pio run -e tft_st7796 -t upload` | 3.5" 480x320 SPI TFT, ST7796 driver (default) |
| `tft_ili9488` | `pio run -e tft_ili9488 -t upload` | same panel, ILI9488 driver. Use this if st7796 gives a white or garbled screen |

Edit `include/config.h` first (Wi-Fi, channels, Y scale, X labels). TFT pins are in `platformio.ini`.

### UDP test build
1. Set `WIFI_SSID` and `WIFI_PASS` in `config.h`. Flash with `pio run -e udp_web -t upload`.
2. On the PC (same network), run `python tools/scope_server.py` and open http://localhost:8000.
   - Browsers can't receive raw UDP. This script (Python 3.8+, standard library only) receives the packets and pushes them to the page with Server-Sent Events.
   - To try the page with no hardware, run `python tools/scope_server.py --fake`.
3. If nothing arrives:
   - Allow Python through the PC firewall (UDP 5005).
   - Some routers block broadcast. Set `UDP_TARGET_IP` to your PC's IP instead.

## Wiring (PCB1)

| ADS1115 | ESP32 | | 3.5" TFT SPI header | PCB header X8 | ESP32 |
|---|---|---|---|---|---|
| VDD | 3V3 | | 1 GND | 8 | GND |
| GND | GND | | 2 VCC | 7 | 3V3 |
| SCL | GPIO22 | | 3 SCL | 6 | GPIO18 |
| SDA | GPIO21 | | 4 SDA | 5 | GPIO23 |
| ADDR | GND (0x48) | | 5 RST | 4 | GPIO4 |
| AIN0 | peak detector 1 | | 6 DC | 3 | GPIO27 |
| AIN1 | peak detector 2 | | 7 CS | 2 | GPIO5 |
| AIN2 | sawtooth | | 8 BL | 1 | GPIO25 (driven HIGH) |
| | | | 9 SDA-O | not connected | |

- The display's IM0/IM1/IM2 jumpers must all be on "1" (4-wire SPI).
- Inputs must stay within **0 to 3.3 V**. The ADS1115 runs from 3.3 V here, and its absolute maximum input is VDD + 0.3 V.
- Wrong colours (red/blue swapped): add `-D TFT_RGB_ORDER=TFT_BGR` (or `TFT_RGB`) to the env. Negative picture: add `-D TFT_INVERSION_ON=1` (or `TFT_INVERSION_OFF=1`).

## How it works
- **Acquisition task (core 0).** Each set is three single-shot conversions at 860 SPS, in the order AIN0, AIN2, AIN1. The saw sample sits in the middle in time, so both Y channels see the same, symmetric X skew (about ±1.4 ms).
- **Polling the OS bit.** Instead of using a fixed delay, the driver polls the conversion-done (OS) bit. This means clones with a different conversion time still read correctly.
- **Start-up diagnostics.** The driver checks that the config register reads back what was written. It also flags an ADS1015 (12-bit) sold as an ADS1115; the voltage scale is still correct in that case.
- **Retrace detection.** A retrace is detected with hysteresis: the detector arms when the saw goes above 75 % of max and fires when it drops below 25 %. A slow fall edge therefore can't trigger twice.
- **Effect of timing jitter.** X comes from the saw *voltage*, not from time. Wi-Fi or SPI jitter therefore does not distort the X-Y plot.
- **TFT.** The graticule is drawn once. Traces use a scope-style erase bar: about 8 columns just ahead of the beam are blanked and their grid is repainted. Each sweep overwrites the previous one with no frame buffer and no flicker. 
- **Throughput.** Expect about 220 sets/s (serial monitor shows `[STAT]`). Keep the saw at 2 Hz or below for a smooth trace (about 100+ points per sweep).

## Tuning
- Calibration and retrace thresholds are set in `SweepParams` (`lib/ScopeCore/SweepTracker.h`).
- Erase-bar width and colours are set in `src/xyplot_tft.h`.
- Use a **source impedance of 10 kΩ or less**, or buffer the signal. The ADS1115 input is switched-capacitor, and the mux moves between channels every conversion.
