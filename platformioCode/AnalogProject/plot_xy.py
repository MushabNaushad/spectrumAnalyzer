#!/usr/bin/env python3
"""
ADS1115 Dual Channel X-Y Curve Tracer (A0 vs A1)
Plots A0 (DUT response / Y-axis) against A1 (Ramp sweep / X-axis).
Supports interactive DC Shift slider, 1-click Auto-Zero, and Retrace Blanking.
"""

import sys
import time
import threading
from collections import deque
import serial
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from matplotlib.widgets import Slider, Button, CheckButtons

# Serial Port Configuration
SERIAL_PORT = '/dev/ttyUSB0'
BAUD_RATE = 115200

# Buffer settings
BUFFER_SIZE = 1200
data_a0 = deque(maxlen=BUFFER_SIZE)
data_a1 = deque(maxlen=BUFFER_SIZE)
lock = threading.Lock()
running = True

# X-Y Plot Settings
a1_offset = 0.0
retrace_blanking = True
auto_track_zero = False

def serial_reader():
    global running
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        ser.reset_input_buffer()
        print(f"[SERIAL] Connected to {SERIAL_PORT} at {BAUD_RATE} baud")
        while running:
            line = ser.readline().decode('ascii', errors='ignore').strip()
            if not line or line.startswith('[') or line.startswith('DATA') or line.startswith('='):
                continue
            parts = [p.strip() for p in line.replace(' ', ',').split(',') if p.strip()]
            if len(parts) >= 2:
                try:
                    v0 = float(parts[0])
                    v1 = float(parts[1])
                    with lock:
                        data_a0.append(v0)
                        data_a1.append(v1)
                except ValueError:
                    pass
        ser.close()
    except Exception as e:
        print(f"[SERIAL ERROR] {e}")

# Start background serial thread
thread = threading.Thread(target=serial_reader, daemon=True)
thread.start()

# Setup Matplotlib Figure & Layout
plt.style.use('dark_background')
fig = plt.figure(figsize=(13, 7.5))
fig.canvas.manager.set_window_title('ADS1115 X-Y Curve Tracer: A0 vs A1')

# Create subplots: Main X-Y on left, Time traces on right
gs = fig.add_gridspec(2, 2, width_ratios=[1.3, 1], height_ratios=[1, 1],
                      left=0.07, right=0.96, top=0.92, bottom=0.18, wspace=0.25, hspace=0.32)

ax_xy = fig.add_subplot(gs[:, 0])      # X-Y Plot (A0 vs A1)
ax_a0 = fig.add_subplot(gs[0, 1])      # A0 vs Time
ax_a1 = fig.add_subplot(gs[1, 1])      # A1 vs Time

# Styling
ax_xy.set_title('X-Y Curve: Output A0 vs Ramp A1 (Shifted)', color='#38bdf8', fontsize=12, fontweight='bold')
ax_xy.set_xlabel('A1 Ramp Voltage (V) [Varying Part]', color='#f472b6', fontsize=10, fontweight='bold')
ax_xy.set_ylabel('A0 Response (V)', color='#38bdf8', fontsize=10, fontweight='bold')
ax_xy.grid(True, linestyle='--', alpha=0.3, color='#38bdf8')

ax_a0.set_title('Channel 0 (A0 Output)', color='#38bdf8', fontsize=10)
ax_a0.set_ylabel('Volts (V)', color='#888')
ax_a0.grid(True, linestyle='--', alpha=0.25)

ax_a1.set_title('Channel 1 (A1 Input Ramp)', color='#f472b6', fontsize=10)
ax_a1.set_xlabel('Recent Samples', color='#888')
ax_a1.set_ylabel('Volts (V)', color='#888')
ax_a1.grid(True, linestyle='--', alpha=0.25)

# Plot lines
xy_line, = ax_xy.plot([], [], color='#22d3a1', lw=2.2, label='A0 vs (A1 - Offset)')
xy_dot, = ax_xy.plot([], [], 'o', color='#fbbf24', ms=7, label='Live Point')
a0_line, = ax_a0.plot([], [], color='#38bdf8', lw=1.6)
a1_raw_line, = ax_a1.plot([], [], color='#6b7280', lw=1.2, linestyle=':', label='Raw A1')
a1_shift_line, = ax_a1.plot([], [], color='#f472b6', lw=1.8, label='A1 Shifted (X)')
ax_a1.legend(loc='upper left', fontsize=8)

# Interactive Sliders & Buttons in bottom area
slider_ax = fig.add_axes([0.15, 0.08, 0.45, 0.03], facecolor='#1e293b')
a1_slider = Slider(slider_ax, 'A1 DC Shift', -4.0, 4.0, valinit=0.0, valstep=0.005, color='#f472b6')

btn_zero_ax = fig.add_axes([0.65, 0.07, 0.12, 0.045])
btn_zero = Button(btn_zero_ax, '⚡ Auto-Zero', color='#0284c7', hovercolor='#38bdf8')

btn_fit_ax = fig.add_axes([0.80, 0.07, 0.12, 0.045])
btn_fit = Button(btn_fit_ax, '📐 Auto-Scale', color='#7c3aed', hovercolor='#a855f7')

def on_slider_change(val):
    global a1_offset, auto_track_zero
    a1_offset = val
    auto_track_zero = False

a1_slider.on_changed(on_slider_change)

def on_auto_zero(event):
    global a1_offset
    with lock:
        if len(data_a1) > 10:
            min_v = min(list(data_a1)[-300:])
            a1_offset = min_v
            a1_slider.set_val(a1_offset)
            print(f"[AUTO-ZERO] Set A1 DC Shift to {a1_offset:.3f} V")

btn_zero.on_clicked(on_auto_zero)

auto_scale_pending = False
def on_auto_scale(event):
    global auto_scale_pending
    auto_scale_pending = True

btn_fit.on_clicked(on_auto_scale)

def update(frame):
    global auto_scale_pending, a1_offset
    with lock:
        if len(data_a0) < 5 or len(data_a1) < 5:
            return xy_line, xy_dot, a0_line, a1_raw_line, a1_shift_line

        a0_pts = list(data_a0)
        a1_pts = list(data_a1)

    # Shift A1
    a1_shifted = [v - a1_offset for v in a1_pts]

    # Retrace Blanking: insert NaNs where A1 snaps down
    if retrace_blanking and len(a1_pts) > 10:
        a1_span = max(a1_pts[-300:]) - min(a1_pts[-300:])
        drop_thresh = max(0.05, a1_span * 0.25)
        xy_x = []
        xy_y = []
        for i in range(len(a1_shifted)):
            if i > 0 and (a1_pts[i-1] - a1_pts[i] > drop_thresh):
                xy_x.append(float('nan'))
                xy_y.append(float('nan'))
            xy_x.append(a1_shifted[i])
            xy_y.append(a0_pts[i])
    else:
        xy_x = a1_shifted
        xy_y = a0_pts

    # Update X-Y plot
    xy_line.set_data(xy_x, xy_y)
    if xy_x and not (xy_x[-1] != xy_x[-1]): # not NaN
        xy_dot.set_data([xy_x[-1]], [xy_y[-1]])

    # Update Time domain plots
    samples = range(len(a0_pts))
    a0_line.set_data(samples, a0_pts)
    a1_raw_line.set_data(samples, a1_pts)
    a1_shift_line.set_data(samples, a1_shifted)

    # Auto-scale
    if auto_scale_pending or frame < 5:
        # X-Y axes scale
        valid_x = [x for x in xy_x if x == x]
        valid_y = [y for y in xy_y if y == y]
        if valid_x and valid_y:
            x_min, x_max = min(valid_x), max(valid_x)
            y_min, y_max = min(valid_y), max(valid_y)
            x_pad = max(0.05, (x_max - x_min) * 0.08)
            y_pad = max(0.05, (y_max - y_min) * 0.08)
            ax_xy.set_xlim(x_min - x_pad, x_max + x_pad)
            ax_xy.set_ylim(y_min - y_pad, y_max + y_pad)

        # Time axes scale
        ax_a0.set_xlim(0, len(a0_pts))
        ax_a0.set_ylim(min(a0_pts) - 0.1, max(a0_pts) + 0.1)
        ax_a1.set_xlim(0, len(a1_pts))
        ax_a1.set_ylim(min(min(a1_pts), min(a1_shifted)) - 0.1, max(max(a1_pts), max(a1_shifted)) + 0.1)
        auto_scale_pending = False

    return xy_line, xy_dot, a0_line, a1_raw_line, a1_shift_line

ani = FuncAnimation(fig, update, interval=40, blit=False)

try:
    plt.show()
finally:
    running = False
