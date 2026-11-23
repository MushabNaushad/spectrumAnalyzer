"""
Band-limited test tone generator for the analog heterodyne spectrum analyzer.

"Analyzer view" redraws the spectrum the way the analog analyzer sees it: a 106 Hz
resolution bandwidth (2nd-order band-pass), 70 points per band, channel 1 = 0-2.4 kHz
and channel 2 = 2.5-5 kHz, linear amplitude.

Limits:
  * the tone frequency (slider, buttons, FM) is 1 Hz - 7 kHz,
  * the spectrum display is 0 - 7 kHz (FFT view) or 0 - 5 kHz (analyzer view),
  * square / triangle / sawtooth harmonics go up to 20 kHz by default, so the oscilloscope
    shows the real wave shape. Tick 'Limit harmonics to 7 kHz' to keep every harmonic out of
    the analyzer's image band (7.6 - 12.4 kHz); the wave then looks rounder at high pitches.
All waveforms are band-limited (no aliasing).

The spectrum shown here is the FFT of the samples this program generates (the ideal
signal), not a measurement of what reaches the analyzer.

Output device: pick it in the "Output device" list (e.g. "External Headphones" or a USB
audio adapter on a Mac). Press "Refresh" after plugging a cable or adapter in while the
program is running.
"""
import tkinter as tk
from tkinter import ttk

import numpy as np

F_MAX = 7000.0        # Hz: upper limit for the tone, its harmonics and the plot
FS = 44100            # Hz: sample rate
F_FULL = 20000.0      # Hz: harmonic limit when 'Limit harmonics to 7 kHz' is off (still below Nyquist)
RBW_HZ = 106.0        # Hz: resolution bandwidth of the analyzer's IF band-pass filters (design value)
CH1_BAND = (0.0, 2400.0)     # Hz: channel 1 range on the analyzer (as built)
CH2_BAND = (2500.0, 5000.0)  # Hz: channel 2 range on the analyzer (as built)
POINTS_PER_BAND = 70         # ADC points per sweep on the analyzer (126 sets/s x 0.56 s)
BLOCK = 1024          # samples per audio callback
N_FFT = 4096          # samples per spectrum (10.8 Hz bin spacing)


def _partials(phases, n_max, waveform):
    """Fourier series of the waveform, harmonics 1..n_max only."""
    out = np.zeros_like(phases)
    if waveform == "Square":            # odd harmonics, 1/n
        for n in range(1, n_max + 1, 2):
            out += np.sin(n * phases) / n
    elif waveform == "Triangle":        # odd harmonics, alternating 1/n^2
        for n in range(1, n_max + 1, 2):
            out += (-1) ** ((n - 1) // 2) * np.sin(n * phases) / n ** 2
    elif waveform == "Sawtooth":        # all harmonics, alternating 1/n (rising ramp)
        for n in range(1, n_max + 1):
            out += (-1) ** (n + 1) * np.sin(n * phases) / n
    else:
        raise ValueError(waveform)
    return out


_PEAK = {}   # (waveform, n_max) -> peak of one period, so the output never exceeds +-1


def synth_block(phases, f_peak, waveform, f_max=F_MAX):
    """Band-limited waveform from an array of carrier phases (radians).

    f_peak is the highest instantaneous frequency in the block; harmonic n is
    included only while n * f_peak <= f_max, so nothing above f_max is produced.
    The result is scaled so its peak is exactly 1 (Gibbs overshoot included).
    """
    if waveform == "Sine" or f_peak <= 0:
        return np.sin(phases)
    n_max = max(1, int(f_max // f_peak))
    key = (waveform, n_max)
    if key not in _PEAK:
        cycle = np.linspace(0, 2 * np.pi, 4096, endpoint=False)
        _PEAK[key] = np.abs(_partials(cycle, n_max, waveform)).max()
    return _partials(phases, n_max, waveform) / _PEAK[key]


def spectrum_db(x, fs=FS):
    """Hann-windowed magnitude spectrum in dBFS (a full-scale sine reads 0 dB)."""
    w = np.hanning(len(x))
    mag = np.abs(np.fft.rfft(x * w)) * 2 / w.sum()
    return np.fft.rfftfreq(len(x), 1 / fs), 20 * np.log10(mag + 1e-9)


def analyzer_view(x, fs=FS, rbw=RBW_HZ):
    """What the analog analyzer would draw: the spectrum seen through a swept 2nd-order
    band-pass of bandwidth `rbw`, sampled at the analyzer's points, as linear amplitude
    (a full-scale sine peaks at 1.0). Returns [(freqs_ch1, amp_ch1), (freqs_ch2, amp_ch2)].

    A 2nd-order band-pass passes a tone that is d Hz away with power 1 / (1 + (2d / rbw)^2),
    so each displayed point is the square root of the input power weighted by that curve.
    """
    w = np.hanning(len(x))
    amp = np.abs(np.fft.rfft(x * w)) * 2 / w.sum()
    f = np.fft.rfftfreq(len(x), 1 / fs)
    power = amp ** 2 / 1.5                       # 1.5 = Hann noise bandwidth in bins, so a tone sums to A^2
    keep = power > 1e-12
    bands = []
    for lo, hi in (CH1_BAND, CH2_BAND):
        fx = np.linspace(lo, hi, POINTS_PER_BAND)
        weight = 1 / (1 + (2 * (fx[:, None] - f[None, keep]) / rbw) ** 2)
        bands.append((fx, np.sqrt(weight @ power[keep])))
    return bands


def triggered_trace(buf, fc, fs=FS, periods=4):
    """Oscilloscope trace that starts on a rising zero crossing and shows about `periods` cycles."""
    n = int(np.clip(periods * fs / max(fc, 1.0), 64, len(buf) // 2))
    search = buf[: len(buf) - n]
    rising = np.nonzero((search[:-1] < 0) & (search[1:] >= 0))[0]
    start = rising[-1] + 1 if len(rising) else len(buf) - n
    return np.arange(n) / fs * 1000.0, buf[start:start + n]


class ToneGeneratorApp:
    def __init__(self, root):
        import sounddevice as sd                       # imported here so synth_block can be tested alone
        from matplotlib.figure import Figure
        from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
        self._sd, self._Figure, self._Canvas = sd, Figure, FigureCanvasTkAgg

        self.root = root
        self.root.title("Test Tone Generator (0 - 7 kHz)")
        self.root.geometry("820x700")
        self.root.configure(padx=15, pady=15)

        self.is_playing = False
        self.carrier_phase = 0.0
        self.mod_phase = 0.0

        # Tk variables: touched only by the GUI thread
        self.freq = tk.DoubleVar(value=1000.0)
        self.volume = tk.DoubleVar(value=0.1)
        self.waveform = tk.StringVar(value="Sine")
        self.mod_type = tk.StringVar(value="None")
        self.mod_freq = tk.DoubleVar(value=5.0)
        self.mod_depth = tk.DoubleVar(value=50.0)   # AM: 0-1000 -> 0-100 %; FM: deviation in Hz
        self.limit_7k = tk.BooleanVar(value=False)  # tick to keep harmonics below 7 kHz
        self.analyzer_mode = False                  # spectrum drawn like the analog analyzer

        # Plain copy read by the audio thread (Tk is not thread-safe)
        self.params = {}
        self._copy_params()

        self.vis_buffer = np.zeros(N_FFT, dtype=np.float32)

        self._build_ui()
        self._init_audio()
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self._update_plots()

    # ------------------------------------------------------------------ UI
    def _build_ui(self):
        top = ttk.Frame(self.root)
        top.pack(fill="x", pady=(0, 10))
        self.play_btn = tk.Button(top, text="▶ PLAY", font=("Segoe UI", 12, "bold"),
                                  bg="#28a745", fg="white", width=12, command=self.toggle_play)
        self.play_btn.pack(side="left", padx=(0, 10))
        self.view_btn = tk.Button(top, text="Analyzer view (RBW 106 Hz)", font=("Segoe UI", 10, "bold"),
                                  bg="#6c757d", fg="white", command=self.toggle_view)
        self.view_btn.pack(side="left", padx=(0, 15))
        ttk.Label(top, text="Master Volume:", font=("Segoe UI", 10, "bold")).pack(side="left")
        ttk.Scale(top, from_=0.0, to=1.0, variable=self.volume, orient="horizontal",
                  command=lambda _: self._update_vol_label()).pack(side="left", fill="x", expand=True, padx=10)
        self.vol_lbl = ttk.Label(top, width=10)
        self.vol_lbl.pack(side="left")
        self._update_vol_label()

        dev_row = ttk.Frame(self.root)
        dev_row.pack(fill="x", pady=(0, 8))
        ttk.Label(dev_row, text="Output device:", font=("Segoe UI", 10, "bold")).pack(side="left")
        self.dev_combo = ttk.Combobox(dev_row, state="readonly", width=48)
        self.dev_combo.pack(side="left", padx=8)
        self.dev_combo.bind("<<ComboboxSelected>>", lambda _: self._on_device_selected())
        ttk.Button(dev_row, text="Refresh", command=self._refresh_devices).pack(side="left")
        self.dev_status = ttk.Label(dev_row, foreground="#555555")
        self.dev_status.pack(side="left", padx=10)

        ff = ttk.LabelFrame(self.root, text=f"Tone Frequency (1 Hz - {F_MAX:,.0f} Hz)", padding=10)
        ff.pack(fill="x", pady=5)
        self.freq_readout = ttk.Label(ff, font=("Consolas", 20, "bold"))
        self.freq_readout.pack(anchor="center", pady=(0, 5))
        ttk.Scale(ff, from_=1.0, to=F_MAX, variable=self.freq, orient="horizontal",
                  command=self._on_freq_change).pack(fill="x", pady=5)
        row = ttk.Frame(ff)
        row.pack(anchor="center", pady=5)
        for d in (-100, -10, -1, 1, 10, 100):
            ttk.Button(row, text=f"{d:+d} Hz", width=8,
                       command=lambda d=d: self._nudge_freq(d)).pack(side="left", padx=3)
        self._on_freq_change()

        ctrl = ttk.Frame(self.root)
        ctrl.pack(fill="x", pady=5)
        wb = ttk.LabelFrame(ctrl, text="Waveform (band-limited)", padding=10)
        wb.pack(side="left", fill="both", expand=True, padx=(0, 5))
        for wf in ("Sine", "Square", "Triangle", "Sawtooth"):
            ttk.Radiobutton(wb, text=wf, value=wf, variable=self.waveform).pack(anchor="w", pady=2)
        ttk.Checkbutton(wb, text="Limit harmonics to 7 kHz", variable=self.limit_7k).pack(anchor="w", pady=(6, 0))
        self.harm_lbl = ttk.Label(wb, foreground="#555555")
        self.harm_lbl.pack(anchor="w")

        mb = ttk.LabelFrame(ctrl, text="Modulation (AM / FM)", padding=10)
        mb.pack(side="left", fill="both", expand=True, padx=(5, 0))
        r = ttk.Frame(mb)
        r.pack(fill="x", pady=(0, 8))
        for mt in ("None", "AM", "FM"):
            ttk.Radiobutton(r, text=mt, value=mt, variable=self.mod_type,
                            command=self._update_mod_labels).pack(side="left", padx=8)
        self.mod_freq_lbl = ttk.Label(mb)
        self.mod_freq_lbl.pack(anchor="w")
        ttk.Scale(mb, from_=0.5, to=500.0, variable=self.mod_freq, orient="horizontal",
                  command=lambda _: self._update_mod_labels()).pack(fill="x", pady=(0, 6))
        self.mod_depth_lbl = ttk.Label(mb)
        self.mod_depth_lbl.pack(anchor="w")
        ttk.Scale(mb, from_=0.0, to=1000.0, variable=self.mod_depth, orient="horizontal",
                  command=lambda _: self._update_mod_labels()).pack(fill="x")
        self._update_mod_labels()

        fig = self._Figure(figsize=(7, 3.6), dpi=100)
        self.ax_time, self.ax_freq = fig.subplots(2, 1)
        fig.tight_layout(pad=2.5)
        self.line_time, = self.ax_time.plot([], [], color="#007acc", lw=1.5)
        self.ax_time.set_title("Oscilloscope (triggered, about 4 cycles)", fontsize=10)
        self.ax_time.set_ylim(-1.2, 1.2)
        self.ax_time.set_xlabel("Time (ms)", fontsize=8)
        self.ax_time.grid(True, alpha=0.3)

        f, _ = spectrum_db(np.zeros(N_FFT))
        self.keep = f <= F_MAX
        self.line_freq, = self.ax_freq.plot(f[self.keep], np.full(self.keep.sum(), -120.0),
                                            color="#d9534f", lw=1.5)
        self.line_ch1, = self.ax_freq.plot([], [], color="#d4a017", lw=1.8, label="CH1 (0 - 2.4 kHz)")
        self.line_ch2, = self.ax_freq.plot([], [], color="#17a2b8", lw=1.8, label="CH2 (2.5 - 5 kHz)")
        self._setup_freq_axes()

        self.canvas = self._Canvas(fig, master=self.root)
        self.canvas.get_tk_widget().pack(fill="both", expand=True, pady=(5, 0))

    def _setup_freq_axes(self):
        ax = self.ax_freq
        if ax.get_legend():
            ax.get_legend().remove()
        if self.analyzer_mode:
            self.line_freq.set_visible(False)
            self.line_ch1.set_visible(True)
            self.line_ch2.set_visible(True)
            ax.set_title(f"Analyzer view: RBW {RBW_HZ:.0f} Hz, {POINTS_PER_BAND} points per band (linear, like the TFT)", fontsize=10)
            ax.set_xlim(0, 5000)
            ax.set_ylim(0, 1.35)
            ax.set_ylabel("Amplitude (FS = 1)", fontsize=8)
            ax.legend(fontsize=7, loc="upper right")
        else:
            self.line_freq.set_visible(True)
            self.line_ch1.set_visible(False)
            self.line_ch2.set_visible(False)
            ax.set_title("FFT of the generated signal (0 - 7 kHz, 10.8 Hz bins)", fontsize=10)
            ax.set_xlim(0, F_MAX)
            ax.set_ylim(-90, 5)
            ax.set_ylabel("dBFS", fontsize=8)
        ax.set_xlabel("Frequency (Hz)", fontsize=8)
        ax.grid(True, alpha=0.3)

    def toggle_view(self):
        self.analyzer_mode = not self.analyzer_mode
        self.view_btn.config(text="FFT view (10.8 Hz bins)" if self.analyzer_mode else "Analyzer view (RBW 106 Hz)",
                             bg="#0d6efd" if self.analyzer_mode else "#6c757d")
        self._setup_freq_axes()

    def _on_freq_change(self, _=None):
        self.freq_readout.config(text=f"{self.freq.get():.1f} Hz")

    def _nudge_freq(self, delta):
        self.freq.set(float(np.clip(self.freq.get() + delta, 1.0, F_MAX)))
        self._on_freq_change()

    def _update_harm_label(self):
        p = self.params
        wf, fc, fmax = p["wf"], p["fc"], p["fmax"]
        if wf == "Sine":
            self.harm_lbl.config(text="Pure tone (no harmonics)")
            return
        step = 1 if wf == "Sawtooth" else 2
        harms = [n for n in range(1, int(fmax // fc) + 1, step)] if fc > 0 else [1]
        if len(harms) <= 1:
            nxt = (1 + step) * fc
            self.harm_lbl.config(text=f"Only the fundamental: next harmonic\n({nxt / 1000:.1f} kHz) is above {fmax / 1000:.0f} kHz, so it looks like a sine")
        else:
            self.harm_lbl.config(text=f"Harmonics: {', '.join(map(str, harms[:8]))}{' ...' if len(harms) > 8 else ''}\n(up to {harms[-1] * fc / 1000:.2f} kHz)")

    def _update_vol_label(self):
        v = self.volume.get()
        self.vol_lbl.config(text=f"{20 * np.log10(v):.1f} dBFS" if v > 0 else "mute")

    def _update_mod_labels(self):
        mt, mf, md = self.mod_type.get(), self.mod_freq.get(), self.mod_depth.get()
        self.mod_freq_lbl.config(text=f"Modulating Freq: {mf:.1f} Hz")
        if mt == "AM":
            self.mod_depth_lbl.config(text=f"AM Depth: {min(md / 10.0, 100.0):.1f} %")
        elif mt == "FM":
            self.mod_depth_lbl.config(text=f"FM Frequency Deviation: ±{md:.1f} Hz")
        else:
            self.mod_depth_lbl.config(text="Modulation Inactive")

    # ------------------------------------------------------------------ audio
    def _copy_params(self):
        self.params = dict(fc=self.freq.get(), fm=self.mod_freq.get(), md=self.mod_depth.get(),
                           mt=self.mod_type.get(), wf=self.waveform.get(), vol=self.volume.get(),
                           fmax=F_MAX if self.limit_7k.get() else F_FULL, play=self.is_playing)

    def _audio_callback(self, outdata, frames, time, status):
        p = self.params                      # snapshot, no Tk calls in this thread
        if not p["play"]:
            outdata.fill(0)
            return

        mod_inc = 2 * np.pi * p["fm"] / FS
        mod_wave = np.sin(self.mod_phase + np.arange(frames) * mod_inc)
        self.mod_phase = (self.mod_phase + frames * mod_inc) % (2 * np.pi)

        if p["mt"] == "FM":
            inst = np.clip(p["fc"] + p["md"] * mod_wave, 1.0, F_MAX)
        else:
            inst = np.full(frames, p["fc"])

        phases = self.carrier_phase + np.cumsum(2 * np.pi * inst / FS)
        self.carrier_phase = phases[-1] % (2 * np.pi)

        raw = synth_block(phases, inst.max(), p["wf"], p["fmax"])

        if p["mt"] == "AM":                  # m = 0..1 -> true 0..100 % modulation, peak stays <= 1
            m = min(p["md"] / 1000.0, 1.0)
            raw = raw * (1 + m * mod_wave) / (1 + m)

        sig = (raw * p["vol"]).astype(np.float32)
        outdata[:] = sig[:, None]            # same signal on every output channel

        n = min(frames, N_FFT)
        buf = np.roll(self.vis_buffer, -n)
        buf[-n:] = sig[-n:]
        self.vis_buffer = buf

    def _init_audio(self):
        self.stream = None
        self.dev_indices = []
        self._refresh_devices()

    def _list_output_devices(self):
        """[(index, label)] for every device that can play sound; also the default output index."""
        sd = self._sd
        devices, apis = sd.query_devices(), sd.query_hostapis()
        outs = [(i, f"{d['name']}  ({apis[d['hostapi']]['name']}, {d['max_output_channels']} ch)")
                for i, d in enumerate(devices) if d["max_output_channels"] > 0]
        try:
            default = sd.query_devices(kind="output")["index"]
        except Exception:
            default = outs[0][0] if outs else None
        return outs, default

    def _refresh_devices(self):
        """Re-scan audio devices (needed after plugging something in while the app is open)."""
        keep = self.dev_indices[self.dev_combo.current()] if self.dev_indices and self.dev_combo.current() >= 0 else None
        self._close_stream()
        try:                                  # PortAudio only sees new hardware after a re-init
            self._sd._terminate()
            self._sd._initialize()
        except Exception:
            pass
        outs, default = self._list_output_devices()
        self.dev_indices = [i for i, _ in outs]
        self.dev_combo["values"] = [label for _, label in outs]
        if not outs:
            self.dev_status.config(text="No output devices found")
            return
        pick = None
        if keep is not None and keep in self.dev_indices:
            pick = self.dev_indices.index(keep)
        elif default in self.dev_indices:
            pick = self.dev_indices.index(default)
        else:
            pick = 0
        self.dev_combo.current(pick)
        self._open_stream(self.dev_indices[pick])

    def _on_device_selected(self):
        i = self.dev_combo.current()
        if i >= 0:
            self._open_stream(self.dev_indices[i])

    def _close_stream(self):
        if getattr(self, "stream", None) is not None:
            try:
                self.stream.stop()
                self.stream.close()
            except Exception:
                pass
            self.stream = None

    def _open_stream(self, device):
        self._close_stream()
        info = self._sd.query_devices(device)
        ch = min(2, info["max_output_channels"])   # same signal on L and R (or mono)
        try:
            self.stream = self._sd.OutputStream(device=device, samplerate=FS, channels=ch, blocksize=BLOCK,
                                                dtype="float32", callback=self._audio_callback)
            self.stream.start()
            self.dev_status.config(text=f"Playing on: {info['name']}", foreground="#1e7e34")
        except Exception as e:
            self.stream = None
            self.dev_status.config(text=f"Cannot open {info['name']}: {e}", foreground="#c82333")

    def toggle_play(self):
        self.is_playing = not self.is_playing
        self._copy_params()
        if self.is_playing:
            self.play_btn.config(text="⏸ STOP", bg="#dc3545")
        else:
            self.play_btn.config(text="▶ PLAY", bg="#28a745")
            self.vis_buffer = np.zeros(N_FFT, dtype=np.float32)

    def _update_plots(self):
        self._copy_params()                  # GUI thread -> audio thread
        self._update_harm_label()
        buf = self.vis_buffer
        t_ms, y = triggered_trace(buf, self.params["fc"])
        self.line_time.set_data(t_ms, y)
        self.ax_time.set_xlim(0, t_ms[-1] if len(t_ms) else 1)
        if self.analyzer_mode:
            (f1, a1), (f2, a2) = analyzer_view(buf)
            self.line_ch1.set_data(f1, a1)
            self.line_ch2.set_data(f2, a2)
        else:
            _, db = spectrum_db(buf)
            self.line_freq.set_ydata(db[self.keep])
        self.canvas.draw_idle()
        self.root.after(40, self._update_plots)

    def _on_close(self):
        self.is_playing = False
        self._copy_params()
        self._close_stream()
        self.root.destroy()


if __name__ == "__main__":
    root = tk.Tk()
    ToneGeneratorApp(root)
    root.mainloop()
