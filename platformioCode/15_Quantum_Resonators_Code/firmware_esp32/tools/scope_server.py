#!/usr/bin/env python3
"""
UDP -> browser relay for the ESP32 + ADS1115 X-Y scope (test build).

Browsers cannot receive raw UDP, so this script listens for the ESP32's UDP
packets and pushes them to the web page with Server-Sent Events (SSE).
Standard library only (Python 3.8+).

    python scope_server.py                 # real hardware
    python scope_server.py --fake          # synthetic data, no ESP32 needed
    then open  http://localhost:8000

Options: --udp 5005  --http 8000
"""
import argparse
import json
import math
import queue
import random
import socket
import struct
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

HDR = struct.Struct("<4sBBHIhHf")   # must match PacketHeader in main_udp.cpp
SMP = struct.Struct("<hhhB")        # must match PacketSample
MAGIC, VERSION = b"ADSX", 1
FLAG_SWEEP, FLAG_CAL, FLAG_ERR = 0x01, 0x02, 0x04

PAGE = Path(__file__).with_name("scope_web.html")

_clients = set()
_clients_lock = threading.Lock()


def publish(msg: str) -> None:
    with _clients_lock:
        for q in list(_clients):
            try:
                q.put_nowait(msg)
            except queue.Full:      # slow browser tab: drop, don't block UDP
                pass


def udp_loop(port: int) -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", port))           # receives unicast AND subnet broadcast
    print(f"[udp] listening on 0.0.0.0:{port}")
    last_seq, lost_total, src_seen = None, 0, None
    while True:
        data, addr = sock.recvfrom(4096)
        if len(data) < HDR.size:
            continue
        magic, ver, state, count, seq, saw_max, rate10, lsb = HDR.unpack_from(data)
        if magic != MAGIC or ver != VERSION or len(data) < HDR.size + count * SMP.size:
            continue
        if src_seen != addr[0]:
            src_seen = addr[0]
            print(f"[udp] receiving from {src_seen}")
        if last_seq is not None:
            gap = (seq - last_seq - 1) & 0xFFFFFFFF
            if gap < 10000:          # bigger gap = ESP32 rebooted, not loss
                lost_total += gap
        last_seq = seq
        samples = [SMP.unpack_from(data, HDR.size + i * SMP.size) for i in range(count)]
        publish(json.dumps({
            "seq": seq, "state": state, "sawMax": saw_max, "rate": rate10 / 10.0,
            "lsb": lsb, "lost": lost_total, "src": src_seen, "s": samples,
        }, separators=(",", ":")))


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path in ("/", "/index.html"):
            body = PAGE.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif self.path == "/events":
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            q = queue.Queue(maxsize=400)
            with _clients_lock:
                _clients.add(q)
            try:
                while True:
                    try:
                        msg = q.get(timeout=5)
                        self.wfile.write(f"data: {msg}\n\n".encode())
                    except queue.Empty:
                        self.wfile.write(b": keep-alive\n\n")
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                pass
            finally:
                with _clients_lock:
                    _clients.discard(q)
        else:
            self.send_error(404)

    def log_message(self, *args):
        pass


# ------------------------------------------------------------------ fake ESP32
class FakeTracker:
    """Python twin of SweepTracker.cpp (same thresholds)."""
    def __init__(self, min_counts=1600, cal_retraces=3, timeout_s=15.0):
        self.min, self.n_cal, self.timeout = min_counts, cal_retraces, timeout_s
        self.reset(0.0)

    def reset(self, now):
        self.cal, self.run_max, self.saw_max = True, 0, 0
        self.armed, self.retraces, self.t0 = False, 0, now

    def update(self, saw, now):
        saw = max(saw, 0)
        if self.cal and saw > self.run_max:
            self.run_max = saw
        ref = self.run_max if self.cal else self.saw_max
        retrace = False
        if ref >= self.min:
            if not self.armed:
                self.armed = saw * 100 > ref * 75
            elif saw * 100 < ref * 25:
                self.armed, retrace = False, True
        if not self.cal:
            return retrace
        if retrace:
            self.retraces += 1
            if self.retraces >= self.n_cal:
                self.saw_max, self.cal = self.run_max, False
                return True
        if now - self.t0 >= self.timeout and self.run_max >= self.min:
            self.saw_max, self.cal, self.armed = self.run_max, False, False
        return False


def fake_esp32(port: int) -> None:
    """Synthesises what the ESP32 would send: 1 Hz saw 0..2.9 V, sine on CH1,
    triangle on CH2, ~225 sets/s with the real SIG1->SAW->SIG2 time skew."""
    lsb, t_conv = 125e-6, 1.45e-3
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    tr, seq, t, buf = FakeTracker(), 0, 0.0, []
    start = time.monotonic()
    to_counts = lambda v: max(0, min(32767, int(round(v / lsb + random.gauss(0, 3)))))
    while True:
        t1, ts, t2 = t, t + t_conv, t + 2 * t_conv
        saw_v = 2.9 * ((ts * 1.0) % 1.0)
        s1_v = 1.65 + 1.2 * math.sin(2 * math.pi * 3.0 * t1)
        ph = (t2 * 2.0) % 1.0
        s2_v = 0.4 + 2.4 * (2 * ph if ph < 0.5 else 2 - 2 * ph)
        saw, a, b = to_counts(saw_v), to_counts(s1_v), to_counts(s2_v)
        flags = FLAG_SWEEP if tr.update(saw, t) else 0
        if not tr.cal:
            flags |= FLAG_CAL
        buf.append((saw, a, b, flags))
        if len(buf) == 10:
            hdr = HDR.pack(MAGIC, VERSION, 0 if tr.cal else 1, len(buf), seq,
                           tr.run_max if tr.cal else tr.saw_max,
                           int(10 / (3 * t_conv)), lsb)
            sock.sendto(hdr + b"".join(SMP.pack(*x) for x in buf), ("127.0.0.1", port))
            seq, buf = seq + 1, []
        t += 3 * t_conv
        lag = t - (time.monotonic() - start)
        if lag > 0:
            time.sleep(lag)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--udp", type=int, default=5005)
    ap.add_argument("--http", type=int, default=8000)
    ap.add_argument("--fake", action="store_true", help="generate synthetic data")
    args = ap.parse_args()

    threading.Thread(target=udp_loop, args=(args.udp,), daemon=True).start()
    if args.fake:
        threading.Thread(target=fake_esp32, args=(args.udp,), daemon=True).start()
        print("[fake] synthetic ESP32 running")
    srv = ThreadingHTTPServer(("0.0.0.0", args.http), Handler)
    srv.daemon_threads = True
    print(f"[http] open http://localhost:{args.http}")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
