#pragma once
// ---------------------------------------------------------------------------
// Scope-style X-Y renderer for a 480x320 SPI TFT (TFT_eSPI), ST7796 / ILI9488.
//
//  * Static layer: header, dotted graticule, centre divider, drawn once.
//  * Dynamic layer: no frame buffer. Like the "erase bar" of a sampling scope,
//    a few columns just AHEAD of the beam are blanked (and their graticule
//    repainted) before the new segment is drawn. Each sweep therefore
//    overwrites the previous one in place - no full-screen clears, no flicker,
//    and every SPI transfer is small, so drawing keeps up with the ADC.
//
//  Left half  (x   0..239): CH1, X = saw/sawMax * 239
//  Right half (x 240..479): CH2, X = 240 + saw/sawMax * 239
// ---------------------------------------------------------------------------
#include <TFT_eSPI.h>
#include <math.h>

class XYPlot {
public:
  static constexpr int W = 480, H = 320;
  static constexpr int HDR_H = 18;               // status line (font 2, 16 px)
  static constexpr int FTR_H = 18;               // X-axis end labels
  static constexpr int PY = HDR_H, PH = H - HDR_H - FTR_H;
  static constexpr int HALF = W / 2;             // 240 px per channel
  static constexpr int XDIV = HALF / 4;          // 60 px per horizontal division
  static constexpr int ERASE_GAP = 12;           // columns blanked ahead of the beam

  static constexpr uint16_t COL_BG      = 0x0000;  // black
  static constexpr uint16_t COL_GRID    = 0x31A6;  // dark grey-green
  static constexpr uint16_t COL_DIVIDER = 0x8410;  // mid grey
  static constexpr uint16_t COL_HDR_BG  = 0x10A2;
  static constexpr uint16_t COL_TEXT    = 0xDEFB;
  static constexpr uint16_t COL_WARN    = 0xFD20;  // orange
  static constexpr uint16_t COL_CH1     = 0xFFE0;  // yellow  (scope CH1 convention)
  static constexpr uint16_t COL_CH2     = 0x07FF;  // cyan    (scope CH2 convention)

  XYPlot(TFT_eSPI &tft, float yMaxV, float yDivV, const char *xLeft = "", const char *xRight = "")
      : t_(tft), yMaxV_(yMaxV), yDivV_(yDivV), xLeft_(xLeft), xRight_(xRight) {}

  void begin() {
    t_.init();
    t_.setRotation(1);  // landscape 480x320 (use 3 if the picture is upside down)
    t_.fillScreen(COL_BG);
    t_.setTextFont(2);
    t_.setTextSize(1);
    // horizontal graticule rows: every yDiv from 0 V, plus the top (yMax)
    nH_ = 0;
    for (int i = 0; nH_ < MAX_H; ++i) {
      const float v = i * yDivV_;
      if (v > yMaxV_ + 1e-4f) break;
      hRow_[nH_++] = yOf(v);
    }
    if (nH_ < MAX_H && hRow_[nH_ - 1] != yOf(yMaxV_)) hRow_[nH_++] = yOf(yMaxV_);
    t_.fillRect(0, 0, W, HDR_H, COL_HDR_BG);
    // footer: X-axis end labels (static, outside the plot so never erased)
    t_.fillRect(0, H - FTR_H, W, FTR_H, COL_BG);
    t_.setTextColor(COL_TEXT, COL_BG);
    t_.setTextDatum(TL_DATUM);
    t_.drawString(xLeft_, 3, H - FTR_H + 1);
    t_.setTextDatum(TR_DATUM);
    t_.drawString(xRight_, W - 3, H - FTR_H + 1);
    t_.setTextDatum(TL_DATUM);
    clearPlot();
  }

  // Full redraw of the static layer + reset of both traces.
  void clearPlot() {
    t_.fillRect(0, PY, W, PH, COL_BG);
    gridStrip(0, W - 1);
    for (auto &tr : tr_) { tr.cleared = HALF - 1; tr.have = false; }  // all columns already clean
  }

  void message(const char *msg) {
    clearPlot();
    t_.setTextColor(COL_WARN, COL_BG);
    t_.setTextDatum(MC_DATUM);
    t_.setTextSize(2);
    t_.drawString(msg, W / 2, PY + PH / 2);
    t_.setTextSize(1);
    t_.setTextDatum(TL_DATUM);
  }

  // One X-Y sample set (raw counts). sawMax > 0 (locked value).
  void add(int16_t saw, int16_t sig1, int16_t sig2, int16_t sawMax, float lsbV, bool sweepStart) {
    addVolts(saw, sig1 * lsbV, sig2 * lsbV, sawMax, sweepStart);
  }

  // Same, with the two Y values already in volts (e.g. after noise clean-up).
  void addVolts(int16_t saw, float v1, float v2, int16_t sawMax, bool sweepStart) {
    if (sawMax <= 0) return;
    if (sweepStart) startSweep();
    const int32_t s = saw < 0 ? 0 : saw;
    int xr = int((s * (HALF - 1) + sawMax / 2) / sawMax);  // 0..239, rounded
    if (xr > HALF - 1) xr = HALF - 1;                       // saw may overshoot locked max
    point(0, xr, yOf(v1), COL_CH1);
    point(1, xr, yOf(v2), COL_CH2);
  }

  void header(const char *mid, bool warn) {
    t_.setTextDatum(TL_DATUM);
    t_.setTextColor(COL_CH1, COL_HDR_BG);
    t_.drawString("CH1 AIN0", 3, 1);
    t_.setTextDatum(TR_DATUM);
    t_.setTextColor(COL_CH2, COL_HDR_BG);
    t_.drawString("CH2 AIN1", W - 3, 1);
    t_.setTextDatum(TC_DATUM);
    t_.setTextColor(warn ? COL_WARN : COL_TEXT, COL_HDR_BG);
    t_.setTextPadding(300);  // overwrite old text, no flicker
    t_.drawString(mid, W / 2, 1);
    t_.setTextPadding(0);
    t_.setTextDatum(TL_DATUM);
  }

private:
  struct Trace {
    int cleared = -1;  // right-most column (0..239) already blanked this sweep
    int x = 0, y = 0;  // last point (screen coords)
    bool have = false;
  };

  int yOf(float v) const {
    int y = PY + PH - 1 - int(lroundf(v / yMaxV_ * float(PH - 1)));
    if (y < PY) y = PY;
    if (y > PY + PH - 1) y = PY + PH - 1;
    return y;
  }

  // Repaint the graticule inside screen columns [xa, xb].
  void gridStrip(int xa, int xb) {
    for (int x = xa; x <= xb; ++x) {
      if (x == HALF) { t_.drawFastVLine(x, PY, PH, COL_DIVIDER); continue; }
      if ((x % HALF) % XDIV == 0 || x == W - 1)
        for (int y = PY; y < PY + PH; y += 3) t_.drawPixel(x, y, COL_GRID);
      if ((x % 3) == 0)
        for (int i = 0; i < nH_; ++i) t_.drawPixel(x, hRow_[i], COL_GRID);
    }
  }

  // Blank columns [c0, c1] of one half and restore the graticule there.
  void clearCols(int half, int c0, int c1) {
    if (c0 > c1) return;
    const int xa = half * HALF + c0;
    t_.fillRect(xa, PY, c1 - c0 + 1, PH, COL_BG);
    gridStrip(xa, xa + (c1 - c0));
  }

  void startSweep() {
    for (int h = 0; h < 2; ++h) {
      clearCols(h, tr_[h].cleared + 1, HALF - 1);  // remove stale tail (2 sweeps old)
      tr_[h].cleared = -1;
      tr_[h].have = false;  // no fly-back line
    }
  }

  void point(int half, int xr, int y, uint16_t col) {
    Trace &tr = tr_[half];
    int ahead = xr + ERASE_GAP;
    if (ahead > HALF - 1) ahead = HALF - 1;
    if (ahead > tr.cleared) {
      clearCols(half, tr.cleared + 1, ahead);
      tr.cleared = ahead;
    }
    const int x = half * HALF + xr;
    if (tr.have) t_.drawLine(tr.x, tr.y, x, y, col);
    else         t_.drawPixel(x, y, col);
    tr.x = x;
    tr.y = y;
    tr.have = true;
  }

  static constexpr int MAX_H = 16;
  TFT_eSPI &t_;
  float yMaxV_, yDivV_;
  const char *xLeft_, *xRight_;
  int hRow_[MAX_H] = {0};
  int nH_ = 0;
  Trace tr_[2];
};
