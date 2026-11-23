#pragma once
// ======================= user configuration =======================

// ---- Wi-Fi (udp_web build only) ----
#define WIFI_SSID      "your-ssid"
#define WIFI_PASS      "your-password"
// "" = broadcast to the whole subnet (no need to know the PC's IP),
// or put your PC's IP here, e.g. "192.168.1.50" (more reliable on some routers)
#define UDP_TARGET_IP  ""
#define UDP_PORT       5005

// ---- ADS1115 wiring ----
#define ADS_SDA_PIN    21
#define ADS_SCL_PIN    22
#define ADS_I2C_ADDR   0x48      // ADDR pin -> GND
#define ADS_I2C_HZ     400000    // fast mode. Drop to 100000 if a clone NACKs
#define CH_SIG1        0         // AIN0 -> left half  (0 .. Xmax/2)
#define CH_SIG2        1         // AIN1 -> right half (Xmax/2 .. Xmax)
#define CH_SAW         2         // AIN2 -> X sweep (sawtooth)
#define RECAL_PIN      0         // BOOT button: press to re-learn saw max (-1 = off)

// ---- fixed Y scale ----
#define Y_MAX_VOLTS    3.3f
#define Y_DIV_VOLTS    0.5f

// ---- X-axis end labels (TFT build; web page: X_LEFT / X_RIGHT in scope_web.html) ----
#define X_LABEL_LEFT   "0 kHz"
#define X_LABEL_RIGHT  "5 kHz"

// ---- noise clean-up on the CH1 / CH2 traces (TFT build) ----
// Small wiggles near the baseline are pushed toward the noise floor; real peaks pass untouched.
#define NOISE_GATE_ON       1       // 0 = show the raw traces
#define NOISE_ATTEN_DB      3.0f    // how much noise is reduced: 3 = x0.71, 6 = x0.5, 60 = flat line
#define NOISE_K             3.0f    // threshold = K x measured noise spread above the floor
#define NOISE_MIN_MARGIN_V  0.03f   // threshold is never closer to the floor than this (volts)
