"""Flask app: pairs the ESP32-S3 CSI stream with a presence detector.

Usage:
    python app.py --usb COM7                # current one-ESP USB flow
    python app.py --udp 5005                # optional wireless test listener

With no flags, the app prompts you interactively.
"""

from __future__ import annotations

import argparse
import math
import time
from dataclasses import dataclass, field
from typing import Any, Optional

import numpy as np
from flask import Flask, jsonify, render_template
from serial.tools import list_ports

from csi_reader import BaseCSIReader, CSIReader, UDPCSIReader
from detector import PresenceDetector

app = Flask(__name__)


@dataclass
class BoardRuntime:
    port: str
    reader: BaseCSIReader
    detector: PresenceDetector
    transport: str = "usb"
    presence_start: Optional[float] = field(default=None)  # Fix F: monotonic time PRESENCE began
    warmup_buffer_cleared: bool = field(default=False)


boards: list[BoardRuntime] = []


@app.route("/")
def index():
    return render_template("index.html")


@app.route("/reset", methods=["POST"])
def reset():
    for board in boards:
        board.detector.reset()
        board.reader.reset_buffers()
        board.warmup_buffer_cleared = False
    return jsonify({"ok": True})


def _board_status(board: BoardRuntime) -> dict[str, Any]:
    # Drop frames captured during the warmup countdown so the baseline is
    # collected from frames after the user has stepped away.
    if board.detector.warmup_active():
        board.reader.reset_buffers()
        board.warmup_buffer_cleared = False
    elif not board.warmup_buffer_cleared:
        board.reader.reset_buffers()
        board.warmup_buffer_cleared = True

    with board.reader.lock:
        samples = list(board.reader.amplitudes)
        frames_buf = list(board.reader.csi_frames)
        phase_buf = list(board.reader.phase_frames)
        last_ts = board.reader.last_packet_ts
        frames = board.reader.frames_seen
        errors = board.reader.parse_errors
        rssi = board.reader.rssi[-1] if board.reader.rssi else None

    state = board.detector.update(samples, frames_buf, phase_buf)

    # Fix F: track how long the board has been continuously in PRESENCE
    now_mono = time.monotonic()
    if state.status == "PRESENCE":
        if board.presence_start is None:
            board.presence_start = now_mono
        presence_sustained_s = now_mono - board.presence_start
    else:
        board.presence_start = None
        presence_sustained_s = 0.0

    age = (time.time() - last_ts) if last_ts else None
    active = age is not None and age < 3.0
    warmup_remaining_s = board.detector.warmup_remaining_s()
    return {
        "port": board.port,
        "transport": board.transport,
        "active": active,
        "status": state.status,
        "progress": state.progress,
        "warmup_remaining_s": warmup_remaining_s,
        "baseline_std": state.baseline_std,
        "current_std": state.current_std,
        "ratio": state.ratio,
        "motion_threshold": board.detector.threshold_multiplier,
        "motion_ratio": state.motion_ratio,
        "raw_motion_ratio": state.raw_motion_ratio,
        "shift_score": state.shift_score,
        "raw_shift_score": state.raw_shift_score,
        "shift_threshold": state.shift_threshold,
        "motion_exit_threshold": state.motion_exit_threshold,
        "shift_exit_threshold": state.shift_exit_threshold,
        "confidence": state.confidence,
        "hold_remaining_s": state.hold_remaining_s,
        "enter_hits": state.enter_hits,
        "enter_hits_required": state.enter_hits_required,
        "held": state.held,
        "fast_motion_ratio": state.fast_motion_ratio,
        "stable_motion_ratio": state.stable_motion_ratio,
        "fast_shift_score": state.fast_shift_score,
        "stable_shift_score": state.stable_shift_score,
        "fast_window": state.fast_window,
        "stable_window": state.stable_window,
        "selected_subcarriers": state.selected_subcarriers,
        "total_subcarriers": state.total_subcarriers,
        "auto_tuned": state.auto_tuned,
        "ai_filter_enabled": state.ai_filter_enabled,
        "ai_filter_components": state.ai_filter_components,
        "trigger": state.trigger,
        "frames_seen": frames,
        "parse_errors": errors,
        "buffer_size": len(samples),
        "last_packet_age_s": age,
        "last_rssi": rssi,
        "last_error": board.reader.last_error,
        "presence_sustained_s": presence_sustained_s,
    }


def _matrix(rows: list[list[float]], max_rows: int = 120, width: int | None = None) -> list[list[float]]:
    usable = [row for row in rows[-max_rows:] if row]
    if not usable:
        return []
    min_width = min(len(row) for row in usable)
    if width is not None:
        min_width = min(min_width, width)
    if min_width <= 0:
        return []
    return [list(row[:min_width]) for row in usable]


def _normalize_heatmap(matrix: list[list[float]], lo_pct: float = 5.0, hi_pct: float = 95.0) -> dict[str, Any]:
    if not matrix:
        return {"values": [], "min": None, "max": None}
    arr = np.asarray(matrix, dtype=np.float64)
    lo = float(np.percentile(arr, lo_pct))
    hi = float(np.percentile(arr, hi_pct))
    if not math.isfinite(lo) or not math.isfinite(hi) or hi <= lo:
        hi = lo + 1.0
    norm = np.clip((arr - lo) / (hi - lo), 0.0, 1.0)
    return {
        "values": norm.round(3).tolist(),
        "min": lo,
        "max": hi,
    }


def _phase_center(matrix: list[list[float]]) -> list[list[float]]:
    if not matrix:
        return []
    arr = np.unwrap(np.asarray(matrix, dtype=np.float64), axis=1)
    arr = arr - np.mean(arr, axis=1, keepdims=True)
    return arr.tolist()


def _delta_heatmap(matrix: list[list[float]], baseline_size: int = 30) -> dict[str, Any]:
    if len(matrix) <= baseline_size:
        return {"values": [], "min": None, "max": None}
    arr = np.asarray(matrix, dtype=np.float64)
    baseline = arr[:baseline_size]
    recent = arr[baseline_size:]
    mean = np.mean(baseline, axis=0)
    noise = np.std(baseline, axis=0)
    noise_floor = max(float(np.percentile(noise, 50)), 1e-6)
    normalized = np.abs(recent - mean) / np.maximum(noise, noise_floor)
    return _normalize_heatmap(normalized.tolist(), 5.0, 98.0)


def _board_heatmap(board: BoardRuntime) -> dict[str, Any]:
    with board.reader.lock:
        amp_rows = list(board.reader.csi_frames)
        phase_rows = list(board.reader.phase_frames)
        last_ts = board.reader.last_packet_ts
        frames = board.reader.frames_seen
        rssi = board.reader.rssi[-1] if board.reader.rssi else None

    age = (time.time() - last_ts) if last_ts else None
    amp_matrix = _matrix(amp_rows, max_rows=140)
    phase_matrix = _matrix(phase_rows, max_rows=140, width=(len(amp_matrix[0]) if amp_matrix else None))
    phase_centered = _phase_center(phase_matrix)

    return {
        "port": board.port,
        "active": age is not None and age < 3.0,
        "frames_seen": frames,
        "last_packet_age_s": age,
        "last_rssi": rssi,
        "rows": len(amp_matrix),
        "cols": len(amp_matrix[0]) if amp_matrix else 0,
        "amplitude": _normalize_heatmap(amp_matrix),
        "amplitude_delta": _delta_heatmap(amp_matrix),
        "phase": _normalize_heatmap(phase_centered, 2.0, 98.0),
    }


def _fuse_status(board_states: list[dict[str, Any]]) -> dict[str, Any]:
    active = [b for b in board_states if b["active"]]
    primary = active[0] if active else (board_states[0] if board_states else {})

    if not active:
        status = "WAITING"
        progress = 0.0
        trigger = None
        confidence = 0.0
    else:
        confidence = max(float(b["confidence"] or 0.0) for b in active)

        # Fix F: single-node status gate - drop bare OR-of-PRESENCE
        # Rule 1: any stream >=70% confidence sustained >=350 ms
        high_sustained = [
            b for b in active
            if float(b["confidence"] or 0.0) >= 70.0
            and float(b.get("presence_sustained_s", 0.0)) >= 0.35
        ]
        # Legacy fallback: two or more streams with >=55% confidence simultaneously
        medium = [b for b in active if float(b["confidence"] or 0.0) >= 55.0]

        if high_sustained or len(medium) >= 2:
            status = "PRESENCE"
            source = high_sustained or medium
            trigger = "+".join(b["port"] for b in source[:2])
            progress = 1.0
        elif all(b["status"] == "CALIBRATING" for b in active):
            status = "CALIBRATING"
            progress = min(float(b["progress"] or 0.0) for b in active)
            trigger = None
        elif any(b["status"] == "WAITING" for b in active):
            status = "WAITING"
            progress = min(float(b["progress"] or 0.0) for b in active)
            trigger = None
        else:
            status = "NO_PRESENCE"
            progress = 1.0
            trigger = None

    warmup_remaining_s = max(
        (float(b.get("warmup_remaining_s") or 0.0) for b in board_states),
        default=0.0,
    )

    return {
        "status": status,
        "progress": progress,
        "warmup_remaining_s": warmup_remaining_s,
        "confidence": round(confidence, 1),
        "trigger": trigger,
        "board_count": len(board_states),
        "active_board_count": len(active),
        "ports": [b["port"] for b in board_states],
        "boards": board_states,
        "baseline_std": primary.get("baseline_std"),
        "current_std": primary.get("current_std"),
        "ratio": primary.get("ratio"),
        "threshold_multiplier": primary.get("motion_threshold"),
        "motion_threshold": primary.get("motion_threshold"),
        "motion_ratio": primary.get("motion_ratio"),
        "raw_motion_ratio": primary.get("raw_motion_ratio"),
        "shift_score": primary.get("shift_score"),
        "raw_shift_score": primary.get("raw_shift_score"),
        "shift_threshold": primary.get("shift_threshold"),
        "motion_exit_threshold": primary.get("motion_exit_threshold"),
        "shift_exit_threshold": primary.get("shift_exit_threshold"),
        "hold_remaining_s": primary.get("hold_remaining_s", 0.0),
        "enter_hits": primary.get("enter_hits", 0),
        "enter_hits_required": primary.get("enter_hits_required", 1),
        "held": primary.get("held", False),
        "fast_motion_ratio": primary.get("fast_motion_ratio"),
        "stable_motion_ratio": primary.get("stable_motion_ratio"),
        "fast_shift_score": primary.get("fast_shift_score"),
        "stable_shift_score": primary.get("stable_shift_score"),
        "fast_window": primary.get("fast_window"),
        "stable_window": primary.get("stable_window"),
        "selected_subcarriers": primary.get("selected_subcarriers"),
        "total_subcarriers": primary.get("total_subcarriers"),
        "auto_tuned": primary.get("auto_tuned"),
        "ai_filter_enabled": primary.get("ai_filter_enabled"),
        "ai_filter_components": primary.get("ai_filter_components"),
        "frames_seen": sum(int(b["frames_seen"] or 0) for b in board_states),
        "parse_errors": sum(int(b["parse_errors"] or 0) for b in board_states),
        "buffer_size": sum(int(b["buffer_size"] or 0) for b in board_states),
        "last_packet_age_s": min(
            (float(b["last_packet_age_s"]) for b in active if b["last_packet_age_s"] is not None),
            default=None,
        ),
        "last_rssi": primary.get("last_rssi"),
    }


@app.route("/status")
def status():
    return jsonify(_fuse_status([_board_status(board) for board in boards]))


@app.route("/heatmap")
def heatmap():
    return jsonify({
        "boards": [_board_heatmap(board) for board in boards],
    })


def _prompt_for_sources() -> tuple[list[str], list[int]]:
    """Interactively pick the USB port or optional UDP listener."""
    print("How is your ESP connected?")
    print("  [1] USB cable (serial COM port)")
    print("  [2] Optional wireless test listener (UDP)")
    print("  [3] Both")
    choice = input("Select 1/2/3: ").strip() or "1"

    usb: list[str] = []
    udp: list[int] = []

    if choice in ("1", "3"):
        available = [p.device for p in list_ports.comports()]
        if not available:
            print("(no serial ports detected)")
        else:
            print("Available serial ports:")
            for i, dev in enumerate(available, 1):
                print(f"  [{i}] {dev}")
            raw = input("Pick USB port number for the ESP32-S3: ").strip()
            for tok in (t.strip() for t in raw.split(",") if t.strip()):
                if tok.isdigit():
                    idx = int(tok)
                    if 1 <= idx <= len(available):
                        usb.append(available[idx - 1])
                        continue
                if tok.upper() in (a.upper() for a in available):
                    usb.append(tok.upper())
                    continue
                raise SystemExit(f"Invalid serial selection: {tok}")

    if choice in ("2", "3"):
        raw = input("UDP port to listen on [5005]: ").strip()
        if not raw:
            udp.append(5005)
        else:
            for tok in (t.strip() for t in raw.split(",") if t.strip()):
                if not tok.isdigit():
                    raise SystemExit(f"Invalid UDP port: {tok}")
                udp.append(int(tok))

    if not usb and not udp:
        raise SystemExit("No transports selected.")
    return usb, udp


def _new_detector(args: argparse.Namespace) -> PresenceDetector:
    return PresenceDetector(
        baseline_size=args.baseline_size,
        window=args.window,
        threshold_multiplier=args.motion_threshold,
        shift_threshold=args.shift_threshold,
        motion_exit_threshold=args.motion_exit_threshold,
        shift_exit_threshold=args.shift_exit_threshold,
        smoothing_alpha=args.smoothing_alpha,
        hold_seconds=args.hold_seconds,
        enter_hits_required=args.enter_hits,
        fast_window=args.fast_window,
        auto_tune=not args.no_auto_tune,
        subcarrier_keep_ratio=args.subcarrier_keep_ratio,
        warmup_seconds=args.warmup_seconds,
        stuck_exit_seconds=args.stuck_exit_seconds,
        ai_filter=not args.no_ai_filter,
    )


def main():
    p = argparse.ArgumentParser(description="SMHA single-node CSI backend")
    p.add_argument("--usb", nargs="+", default=None,
                   help="USB serial port for the ESP32-S3, e.g. --usb COM7")
    p.add_argument("--udp", nargs="+", type=int, default=None,
                   help="Optional UDP listen port for wireless tests, e.g. --udp 5005")
    p.add_argument("--udp-host", default="0.0.0.0",
                   help="Interface to bind UDP sockets to (default: all)")
    # Back-compat aliases
    p.add_argument("--port", default=None, help=argparse.SUPPRESS)
    p.add_argument("--ports", nargs="+", default=None, help=argparse.SUPPRESS)
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--http-port", type=int, default=5000)
    p.add_argument("--baseline-size", type=int, default=100)
    p.add_argument("--window", type=int, default=30)
    p.add_argument("--fast-window", type=int, default=10)
    p.add_argument("--motion-threshold", type=float, default=1.15)
    p.add_argument("--shift-threshold", type=float, default=1.6)
    p.add_argument("--motion-exit-threshold", type=float, default=1.02)
    p.add_argument("--shift-exit-threshold", type=float, default=1.1)
    p.add_argument("--smoothing-alpha", type=float, default=0.65)
    p.add_argument("--hold-seconds", type=float, default=1.5)
    p.add_argument("--enter-hits", type=int, default=4)
    p.add_argument("--no-auto-tune", action="store_true")
    p.add_argument("--no-ai-filter", action="store_true",
                   help="Disable the Hampel+PCA denoising stage")
    p.add_argument("--subcarrier-keep-ratio", type=float, default=0.85)
    p.add_argument("--warmup-seconds", type=float, default=10.0,
                   help="Pre-calibration countdown so the user can leave the room")
    p.add_argument("--stuck-exit-seconds", type=float, default=6.0,
                   help="Force exit PRESENCE after this many seconds of quiet motion "
                        "even if shift_score is still elevated (multipath rearrangement)")
    args = p.parse_args()

    usb_ports: list[str] = list(args.usb or [])
    udp_ports: list[int] = list(args.udp or [])

    # Back-compat: --port/--ports map onto --usb
    if args.ports:
        usb_ports.extend(args.ports)
    if args.port:
        usb_ports.append(args.port)

    if not usb_ports and not udp_ports:
        usb_ports, udp_ports = _prompt_for_sources()

    for com in usb_ports:
        reader = CSIReader(com, args.baud)
        boards.append(BoardRuntime(port=com, reader=reader,
                                   detector=_new_detector(args), transport="usb"))
        reader.start()

    for up in udp_ports:
        reader = UDPCSIReader(up, args.udp_host)
        boards.append(BoardRuntime(port=reader.port, reader=reader,
                                   detector=_new_detector(args), transport="udp"))
        reader.start()

    print(f"Started {len(boards)} sensor stream(s): "
          f"{', '.join(f'{b.port} ({b.transport})' for b in boards)}")
    app.run(host=args.host, port=args.http_port, debug=False, use_reloader=False)


if __name__ == "__main__":
    main()
