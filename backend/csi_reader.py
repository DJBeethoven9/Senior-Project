"""CSI ingestion: serial (USB) or UDP (Wi-Fi).

Both transports parse the same one-line CSV format the firmware emits:

    CSI,<ts_us>,<rssi>,<rate>,<sig_mode>,<mcs>,<cwb>,<channel>,<len>,[i0 r0 i1 r1 ...]

The serial path reads it from a COM port; the UDP path reads it from a
datagram socket (each packet is treated as one or more lines).
"""

from __future__ import annotations

import math
import re
import socket
import threading
import time
from collections import deque
from typing import Deque, Optional

import serial

_LINE = re.compile(
    r"^CSI,(-?\d+),(-?\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),\[([-\d ]+)\]\s*$"
)


class BaseCSIReader(threading.Thread):
    """Shared buffers + parser. Subclasses implement the I/O loop."""

    def __init__(self, port: str, buffer_size: int = 600):
        super().__init__(daemon=True, name=f"CSIReader[{port}]")
        self._port = port
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

    @property
    def transport(self) -> str:
        return "usb"

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

    def _ingest_line(self, line: str) -> None:
        if not line.startswith("CSI,"):
            return
        parsed = self._parse(line)
        if parsed is None:
            self.parse_errors += 1
            return
        rssi, rms, amps, phases = parsed
        with self.lock:
            self.amplitudes.append(rms)
            self.csi_frames.append(amps)
            self.phase_frames.append(phases)
            self.rssi.append(rssi)
            self.last_packet_ts = time.time()
            self.frames_seen += 1


class CSIReader(BaseCSIReader):
    """Reads CSI lines from a serial (USB) port."""

    def __init__(self, port: str, baudrate: int = 115200, buffer_size: int = 600):
        super().__init__(port=port, buffer_size=buffer_size)
        self._baudrate = baudrate

    @property
    def transport(self) -> str:
        return "usb"

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
                        self._ingest_line(line)
            except serial.SerialException as e:
                self.last_error = str(e)
                print(f"[csi_reader:{self._port}] serial error: {e}; retrying in 2s")
                time.sleep(2.0)


class UDPCSIReader(BaseCSIReader):
    """Reads CSI lines from a UDP datagram socket.

    Each datagram may contain one or more newline-separated CSI lines.
    Label appears in the UI as e.g. ``UDP:5005``.
    """

    def __init__(self, udp_port: int, bind_host: str = "0.0.0.0",
                 buffer_size: int = 600):
        super().__init__(port=f"UDP:{udp_port}", buffer_size=buffer_size)
        self._udp_port = udp_port
        self._bind_host = bind_host

    @property
    def transport(self) -> str:
        return "udp"

    def run(self) -> None:
        while not self._stop.is_set():
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                sock.settimeout(1.0)
                sock.bind((self._bind_host, self._udp_port))
                self.last_error = None
                print(f"[csi_reader:{self._port}] listening on "
                      f"{self._bind_host}:{self._udp_port}")
                with sock:
                    while not self._stop.is_set():
                        try:
                            data, _ = sock.recvfrom(4096)
                        except socket.timeout:
                            continue
                        try:
                            text = data.decode("ascii", errors="ignore")
                        except Exception:
                            continue
                        for line in text.splitlines():
                            self._ingest_line(line.strip())
            except OSError as e:
                self.last_error = str(e)
                print(f"[csi_reader:{self._port}] socket error: {e}; retrying in 2s")
                time.sleep(2.0)
