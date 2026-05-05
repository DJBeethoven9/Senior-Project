"""Reads the ESP32-S3 CSI CSV stream over a serial port.

The reader keeps both a legacy per-frame RMS amplitude and the per-subcarrier
amplitude vector. The detector uses the vectors because collapsing a CSI frame
to one RMS value can hide motion that only affects part of the channel.
"""

from __future__ import annotations

import math
import re
import threading
import time
from collections import deque
from typing import Deque, Optional

import serial

# CSI,<ts>,<rssi>,<rate>,<sig_mode>,<mcs>,<cwb>,<channel>,<len>,[i0 r0 i1 r1 ...]
_LINE = re.compile(
    r"^CSI,(-?\d+),(-?\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),\[([-\d ]+)\]\s*$"
)


class CSIReader(threading.Thread):
    def __init__(self, port: str, baudrate: int = 115200, buffer_size: int = 600):
        super().__init__(daemon=True, name="CSIReader")
        self._port = port
        self._baudrate = baudrate
        self._stop = threading.Event()

        self.lock = threading.Lock()
        self.amplitudes: Deque[float] = deque(maxlen=buffer_size)
        self.csi_frames: Deque[list[float]] = deque(maxlen=buffer_size)
        self.phase_frames: Deque[list[float]] = deque(maxlen=buffer_size)
        self.rssi: Deque[int] = deque(maxlen=buffer_size)
        self.last_packet_ts: Optional[float] = None
        self.frames_seen: int = 0
        self.parse_errors: int = 0
        self.last_error: Optional[str] = None

    @property
    def port(self) -> str:
        return self._port

    def stop(self) -> None:
        self._stop.set()

    def reset_buffers(self) -> None:
        with self.lock:
            self.amplitudes.clear()
            self.csi_frames.clear()
            self.phase_frames.clear()
            self.rssi.clear()
            self.last_packet_ts = None
            self.frames_seen = 0
            self.parse_errors = 0

    @staticmethod
    def _parse(line: str):
        m = _LINE.match(line)
        if not m:
            return None
        rssi = int(m.group(2))
        raw = m.group(9).split()
        if len(raw) < 2:
            return None
        n = (len(raw) // 2) * 2
        ints = list(map(int, raw[:n]))
        sq = 0.0
        amps: list[float] = []
        phases: list[float] = []
        for i in range(0, n, 2):
            im = ints[i]
            re_ = ints[i + 1]
            power = im * im + re_ * re_
            sq += power
            amps.append(math.sqrt(power))
            phases.append(math.atan2(im, re_))
        rms = math.sqrt(sq / len(amps)) if amps else 0.0
        return rssi, rms, amps, phases

    def run(self) -> None:
        while not self._stop.is_set():
            try:
                with serial.Serial(self._port, self._baudrate, timeout=1) as ser:
                    self.last_error = None
                    ser.reset_input_buffer()
                    while not self._stop.is_set():
                        raw = ser.readline()
                        if not raw:
                            continue
                        try:
                            line = raw.decode("ascii", errors="ignore").strip()
                        except Exception:
                            continue
                        if not line.startswith("CSI,"):
                            continue
                        parsed = self._parse(line)
                        if parsed is None:
                            self.parse_errors += 1
                            continue
                        rssi, rms, amps, phases = parsed
                        with self.lock:
                            self.amplitudes.append(rms)
                            self.csi_frames.append(amps)
                            self.phase_frames.append(phases)
                            self.rssi.append(rssi)
                            self.last_packet_ts = time.time()
                            self.frames_seen += 1
            except serial.SerialException as e:
                self.last_error = str(e)
                print(f"[csi_reader:{self._port}] serial error: {e}; retrying in 2s")
                time.sleep(2.0)
