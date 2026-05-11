"""Phase 1.5 presence detector — fixes A-E applied.

A: Consecutive-hit entry gate (enter_hits_required=4 resets on any miss)
B: Asymmetric windows — entry uses stable window only, exit uses fast window only
C: Tighter auto-tune (p95+0.25 motion, p99+0.50 shift), wider clamps [1.30,2.00]/[1.40,3.0]
D: Baseline drift compensation during NO_PRESENCE (alpha=1e-3 per update)
E: Phase co-confirmation — amplitude AND phase must both fire, OR shift alone
"""

from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Optional, Sequence

import numpy as np

from csi_filter import CSIDenoiser


@dataclass
class DetectorState:
    status: str            # "CALIBRATING" | "WAITING" | "PRESENCE" | "NO_PRESENCE"
    baseline_std: Optional[float]
    current_std: Optional[float]
    ratio: Optional[float]
    progress: float        # 0..1 calibration progress
    motion_ratio: Optional[float] = None
    shift_score: Optional[float] = None
    shift_threshold: Optional[float] = None
    trigger: Optional[str] = None
    raw_motion_ratio: Optional[float] = None
    raw_shift_score: Optional[float] = None
    motion_exit_threshold: Optional[float] = None
    shift_exit_threshold: Optional[float] = None
    confidence: float = 0.0
    hold_remaining_s: float = 0.0
    enter_hits: int = 0
    enter_hits_required: int = 4
    held: bool = False
    fast_motion_ratio: Optional[float] = None
    stable_motion_ratio: Optional[float] = None
    fast_shift_score: Optional[float] = None
    stable_shift_score: Optional[float] = None
    fast_window: int = 10
    stable_window: int = 30
    selected_subcarriers: int = 0
    total_subcarriers: int = 0
    auto_tuned: bool = False
    ai_filter_enabled: bool = False
    ai_filter_components: int = 0


class PresenceDetector:
    def __init__(
        self,
        baseline_size: int = 100,
        window: int = 30,
        fast_window: int = 10,
        threshold_multiplier: float = 1.15,
        shift_threshold: float = 1.6,
        motion_exit_threshold: float = 1.02,
        shift_exit_threshold: float = 1.1,
        smoothing_alpha: float = 0.65,
        hold_seconds: float = 3.0,
        enter_hits_required: int = 4,          # Fix A: was 1
        auto_tune: bool = True,
        subcarrier_keep_ratio: float = 0.85,
        warmup_seconds: float = 0.0,
        stuck_exit_seconds: float = 6.0,
        ai_filter: bool = True,
    ):
        self.baseline_size = baseline_size
        self.window = window
        self.fast_window = min(max(3, fast_window), max(3, window))
        self.smoothing_alpha = min(max(smoothing_alpha, 0.0), 1.0)
        self.hold_seconds = max(0.0, hold_seconds)
        self.enter_hits_required = max(1, enter_hits_required)  # Fix A: no upper cap
        self.auto_tune = auto_tune
        self.subcarrier_keep_ratio = min(max(subcarrier_keep_ratio, 0.25), 1.0)
        self.warmup_seconds = max(0.0, warmup_seconds)
        self.stuck_exit_seconds = max(0.0, stuck_exit_seconds)
        self._denoiser = CSIDenoiser(enabled=ai_filter)
        self._warmup_start: Optional[float] = (
            time.monotonic() if self.warmup_seconds > 0 else None
        )

        self._base_motion_threshold = threshold_multiplier
        self._base_shift_threshold = shift_threshold
        self._base_motion_exit_threshold = motion_exit_threshold
        self._base_shift_exit_threshold = shift_exit_threshold
        self._base_phase_threshold = 1.4
        self._reset_thresholds()

        self.baseline_std: Optional[float] = None
        self._baseline_mean: Optional[np.ndarray] = None
        self._baseline_noise: Optional[np.ndarray] = None
        self._baseline_width: Optional[int] = None
        self._selected_idx: Optional[np.ndarray] = None
        self._total_subcarriers = 0
        self._auto_tuned = False

        self._phase_baseline_noise: Optional[np.ndarray] = None  # Fix E

        self._rms_baseline_mean: Optional[float] = None
        self._rms_baseline_std: Optional[float] = None

        # Fix B: separate smoothers for stable (entry) and fast (exit)
        self._smooth_stable_motion: Optional[float] = None
        self._smooth_stable_shift: Optional[float] = None
        self._smooth_fast_motion: Optional[float] = None
        self._smooth_fast_shift: Optional[float] = None
        self._smooth_stable_phase: Optional[float] = None  # Fix E

        self._presence_active = False
        self._presence_hold_until = 0.0
        self._consecutive_hits: int = 0  # Fix A: replaces _hit_history deque

        self._last_fast_motion_ratio: Optional[float] = None
        self._last_stable_motion_ratio: Optional[float] = None
        self._last_fast_shift_score: Optional[float] = None
        self._last_stable_shift_score: Optional[float] = None

        # Stuck-PRESENCE watchdog: timestamp when fast motion first dropped
        # below the exit threshold while PRESENCE was active. Cleared on motion.
        self._low_motion_since: Optional[float] = None

    def _reset_thresholds(self) -> None:
        self.threshold_multiplier = self._base_motion_threshold
        self.shift_threshold = self._base_shift_threshold
        self.motion_exit_threshold = self._base_motion_exit_threshold
        self.shift_exit_threshold = self._base_shift_exit_threshold
        self.phase_threshold = self._base_phase_threshold

    def reset(self) -> None:
        self._reset_thresholds()
        self.baseline_std = None
        self._baseline_mean = None
        self._baseline_noise = None
        self._baseline_width = None
        self._selected_idx = None
        self._total_subcarriers = 0
        self._auto_tuned = False
        self._phase_baseline_noise = None
        self._rms_baseline_mean = None
        self._rms_baseline_std = None
        self._denoiser.reset()
        self._warmup_start = (
            time.monotonic() if self.warmup_seconds > 0 else None
        )
        self._reset_runtime_state()

    def warmup_remaining_s(self) -> float:
        if self.warmup_seconds <= 0 or self._warmup_start is None:
            return 0.0
        return max(0.0, self.warmup_seconds - (time.monotonic() - self._warmup_start))

    def warmup_active(self) -> bool:
        return self.warmup_remaining_s() > 0.0

    def _reset_runtime_state(self) -> None:
        self._smooth_stable_motion = None
        self._smooth_stable_shift = None
        self._smooth_fast_motion = None
        self._smooth_fast_shift = None
        self._smooth_stable_phase = None
        self._presence_active = False
        self._presence_hold_until = 0.0
        self._consecutive_hits = 0
        self._last_fast_motion_ratio = None
        self._last_stable_motion_ratio = None
        self._last_fast_shift_score = None
        self._last_stable_shift_score = None
        self._low_motion_since = None

    @staticmethod
    def _matrix(
        frames: Sequence[Sequence[float]],
        width: Optional[int] = None,
    ) -> np.ndarray:
        usable = [frame for frame in frames if frame]
        if not usable:
            return np.empty((0, 0), dtype=np.float64)
        min_width = min(len(frame) for frame in usable)
        if width is not None:
            min_width = min(min_width, width)
        if min_width <= 0:
            return np.empty((0, 0), dtype=np.float64)
        return np.asarray(
            [list(frame[:min_width]) for frame in usable],
            dtype=np.float64,
        )

    @staticmethod
    def _motion_score(matrix: np.ndarray) -> float:
        if matrix.shape[0] < 2 or matrix.shape[1] == 0:
            return 0.0
        per_subcarrier_std = np.std(matrix, axis=0)
        return float(np.percentile(per_subcarrier_std, 75))

    @staticmethod
    def _clamp(value: float, lo: float = 0.0, hi: float = 1.0) -> float:
        return min(max(value, lo), hi)

    def _smooth(self, previous: Optional[float], raw: float) -> float:
        if previous is None:
            return raw
        return (self.smoothing_alpha * raw) + ((1.0 - self.smoothing_alpha) * previous)

    def _confidence_component(self, value: float, exit_value: float, enter_value: float) -> float:
        if enter_value <= exit_value:
            return 1.0 if value >= enter_value else 0.0
        return self._clamp((value - exit_value) / (enter_value - exit_value))

    def _trigger_name(self, amplitude_phase_hit: bool, shift_hit: bool) -> Optional[str]:
        if amplitude_phase_hit and shift_hit:
            return "motion+phase+shift"
        if amplitude_phase_hit:
            return "motion+phase"
        if shift_hit:
            return "baseline_shift"
        return None

    def _select_subcarriers(self, baseline: np.ndarray) -> np.ndarray:
        width = baseline.shape[1]
        if width <= 0:
            return np.asarray([], dtype=np.int64)

        mean = np.mean(baseline, axis=0)
        std = np.std(baseline, axis=0)
        median_std = max(float(np.median(std)), 1e-6)
        score = mean / (std + median_std)

        keep = int(round(width * self.subcarrier_keep_ratio))
        keep = min(width, max(8, keep))
        if width < 8:
            keep = width

        ranked = np.argsort(score)[::-1]
        chosen = ranked[:keep]
        return np.asarray(sorted(chosen.tolist()), dtype=np.int64)

    def _apply_selected(self, matrix: np.ndarray) -> np.ndarray:
        if matrix.shape[1] == 0 or self._selected_idx is None or self._selected_idx.size == 0:
            return matrix
        usable = self._selected_idx[self._selected_idx < matrix.shape[1]]
        if usable.size == 0:
            return matrix
        return matrix[:, usable]

    def _shift_score(self, matrix: np.ndarray) -> float:
        if (
            matrix.shape[0] == 0
            or matrix.shape[1] == 0
            or self._baseline_mean is None
            or self._baseline_noise is None
        ):
            return 0.0
        width = min(matrix.shape[1], self._baseline_mean.shape[0], self._baseline_noise.shape[0])
        recent_mean = np.mean(matrix[:, :width], axis=0)
        delta = np.abs(recent_mean - self._baseline_mean[:width])
        normalized_shift = delta / self._baseline_noise[:width]
        top_count = max(1, int(round(width * 0.20)))
        top_mean = float(np.mean(np.partition(normalized_shift, -top_count)[-top_count:]))
        return max(float(np.percentile(normalized_shift, 75)), top_mean * 0.85)

    def _phase_motion_score(self, matrix: np.ndarray) -> float:
        """Per-subcarrier phase std normalized to baseline phase noise (Fix E)."""
        if matrix.shape[0] < 2 or matrix.shape[1] == 0 or self._phase_baseline_noise is None:
            return 0.0
        per_sub_std = np.std(matrix, axis=0)
        width = min(per_sub_std.shape[0], self._phase_baseline_noise.shape[0])
        normalized = per_sub_std[:width] / np.maximum(self._phase_baseline_noise[:width], 1e-6)
        return float(np.percentile(normalized, 75))

    def _noise_scores(self, baseline: np.ndarray, size: int) -> tuple[list[float], list[float]]:
        motion: list[float] = []
        shift: list[float] = []
        if baseline.shape[0] < size or size < 2:
            return motion, shift
        step = max(1, size // 3)
        for start in range(0, baseline.shape[0] - size + 1, step):
            window = baseline[start : start + size]
            motion.append(self._motion_score(window) / max(self.baseline_std or 0.0, 1e-9))
            shift.append(self._shift_score(window))
        return motion, shift

    def _auto_tune_thresholds(
        self,
        baseline: np.ndarray,
        phase_baseline: Optional[np.ndarray] = None,
    ) -> None:
        if not self.auto_tune:
            return

        motion_noise: list[float] = []
        shift_noise: list[float] = []
        phase_noise: list[float] = []

        for size in (self.fast_window, self.window):
            motion, shift = self._noise_scores(baseline, size)
            motion_noise.extend(motion)
            shift_noise.extend(shift)
            if phase_baseline is not None and self._phase_baseline_noise is not None:
                step = max(1, size // 3)
                for start in range(0, phase_baseline.shape[0] - size + 1, step):
                    ph_win = phase_baseline[start : start + size]
                    if ph_win.shape[0] >= 2:
                        phase_noise.append(self._phase_motion_score(ph_win))

        if motion_noise:
            # Fix C: p95 + 0.25, clamp [1.30, 2.00]
            p95_motion = float(np.percentile(motion_noise, 95))
            tuned_motion = max(self._base_motion_threshold, p95_motion + 0.25)
            self.threshold_multiplier = min(max(tuned_motion, 1.30), 2.00)
            tuned_exit = max(self._base_motion_exit_threshold, self.threshold_multiplier * 0.84)
            self.motion_exit_threshold = min(tuned_exit, self.threshold_multiplier - 0.10)

        if shift_noise:
            # Fix C: p99 + 0.50, clamp [1.40, 3.0]
            p99_shift = float(np.percentile(shift_noise, 99))
            tuned_shift = max(self._base_shift_threshold, p99_shift + 0.50)
            self.shift_threshold = min(max(tuned_shift, 1.40), 3.0)
            tuned_exit = max(self._base_shift_exit_threshold, self.shift_threshold * 0.65)
            self.shift_exit_threshold = min(tuned_exit, self.shift_threshold - 0.15)

        if phase_noise:
            p95_phase = float(np.percentile(phase_noise, 95))
            self.phase_threshold = min(max(p95_phase + 0.30, self._base_phase_threshold), 2.5)

        self._auto_tuned = True

    def _learn_baseline(
        self,
        frames: Sequence[Sequence[float]],
        samples: Sequence[float],
        phase_frames: Optional[Sequence[Sequence[float]]] = None,
    ) -> bool:
        baseline = self._matrix(frames[: self.baseline_size])
        if baseline.shape[0] < self.baseline_size or baseline.shape[1] == 0:
            return False

        self._baseline_width = baseline.shape[1]
        self._total_subcarriers = baseline.shape[1]
        self._selected_idx = self._select_subcarriers(baseline)
        selected = self._apply_selected(baseline)

        # AI filter: fit the denoiser on the empty-room (selected) baseline.
        # Subsequent stable/fast windows go through Hampel + PCA before scoring.
        self._denoiser.fit_baseline(selected)
        if self._denoiser.fitted:
            selected = self._denoiser.filter(selected)

        self._baseline_mean = np.mean(selected, axis=0)
        baseline_std_vec = np.std(selected, axis=0)
        noise_floor = max(float(np.percentile(baseline_std_vec, 50)), 1e-6)
        self._baseline_noise = np.maximum(baseline_std_vec, noise_floor)
        self.baseline_std = max(self._motion_score(selected), 1e-9)

        # Phase baseline (Fix E)
        selected_phase: Optional[np.ndarray] = None
        if phase_frames:
            ph_bl = self._matrix(phase_frames[: self.baseline_size], self._baseline_width)
            ph_bl = self._apply_selected(ph_bl)
            if ph_bl.shape[0] >= 2:
                ph_noise_floor = max(float(np.percentile(np.std(ph_bl, axis=0), 50)), 1e-3)
                self._phase_baseline_noise = np.maximum(np.std(ph_bl, axis=0), ph_noise_floor)
                selected_phase = ph_bl
            else:
                self._phase_baseline_noise = None
        else:
            self._phase_baseline_noise = None

        if len(samples) >= self.baseline_size:
            rms_baseline = np.asarray(samples[: self.baseline_size], dtype=np.float64)
            self._rms_baseline_mean = float(np.mean(rms_baseline))
            self._rms_baseline_std = max(float(np.std(rms_baseline)), 1e-9)

        self._auto_tune_thresholds(selected, selected_phase)
        self._reset_runtime_state()
        return True

    def _rms_scores(self, samples: Sequence[float], size: int) -> tuple[float, float]:
        if len(samples) < size or self._rms_baseline_std is None:
            return 0.0, 0.0
        recent = np.asarray(samples[-size:], dtype=np.float64)
        motion_ratio = float(np.std(recent)) / self._rms_baseline_std
        shift_score = abs(float(np.mean(recent)) - (self._rms_baseline_mean or 0.0))
        shift_score /= self._rms_baseline_std
        motion_score = motion_ratio * (self.threshold_multiplier / 1.8)
        return motion_score, shift_score

    def _state_from_scores(
        self,
        stable_motion: float,
        stable_shift: float,
        fast_motion: float,
        fast_shift: float,
        stable_phase: float,
        current_std: float,
        progress: float = 1.0,
    ) -> DetectorState:
        stable_motion = max(stable_motion, 0.0)
        stable_shift = max(stable_shift, 0.0)
        fast_motion = max(fast_motion, 0.0)
        fast_shift = max(fast_shift, 0.0)
        stable_phase = max(stable_phase, 0.0)

        # Fix B: smooth stable (entry) and fast (exit) independently
        sm_stable_motion = self._smooth(self._smooth_stable_motion, stable_motion)
        sm_stable_shift = self._smooth(self._smooth_stable_shift, stable_shift)
        sm_stable_phase = self._smooth(self._smooth_stable_phase, stable_phase)
        sm_fast_motion = self._smooth(self._smooth_fast_motion, fast_motion)
        sm_fast_shift = self._smooth(self._smooth_fast_shift, fast_shift)

        self._smooth_stable_motion = sm_stable_motion
        self._smooth_stable_shift = sm_stable_shift
        self._smooth_stable_phase = sm_stable_phase
        self._smooth_fast_motion = sm_fast_motion
        self._smooth_fast_shift = sm_fast_shift

        # Fix E: entry requires amplitude AND phase, OR shift alone
        amplitude_hit = sm_stable_motion >= self.threshold_multiplier
        # If no phase baseline (e.g. phase_frames never supplied), gate is open
        phase_hit = (
            sm_stable_phase >= self.phase_threshold
            if self._phase_baseline_noise is not None
            else True
        )
        shift_hit = sm_stable_shift >= self.shift_threshold
        motion_hit = (amplitude_hit and phase_hit) or shift_hit

        # Fix A (revised): decay by 1 on miss instead of hard reset to zero.
        # This means one bad frame costs one point, not all progress — COM5 can
        # survive occasional dropped frames while still requiring sustained hits for entry.
        if motion_hit:
            self._consecutive_hits = min(
                self._consecutive_hits + 1, self.enter_hits_required * 2
            )
        else:
            self._consecutive_hits = max(self._consecutive_hits - 1, 0)
        enter_hits = self._consecutive_hits
        confirmed_enter = enter_hits >= self.enter_hits_required

        # Fix B: exit uses fast values only
        below_exit = (
            sm_fast_motion < self.motion_exit_threshold
            and sm_fast_shift < self.shift_exit_threshold
        )

        now = time.monotonic()
        held = False
        pure_motion_hit = amplitude_hit and phase_hit
        trigger = self._trigger_name(pure_motion_hit, shift_hit)

        if self._presence_active:
            # Use fast-window values to extend the hold timer so the stable window's
            # 3-second memory does not keep resetting the timer after the person leaves
            # (COM7 "stuck at 100%" root cause).
            fast_hit = (
                sm_fast_motion >= self.threshold_multiplier
                or sm_fast_shift >= self.shift_threshold
            )
            if fast_hit:
                self._presence_hold_until = now + self.hold_seconds

            # Stuck-PRESENCE watchdog: if motion has been quiet for stuck_exit_seconds,
            # force exit even when shift_score remains elevated due to multipath
            # rearrangement that didn't snap back when the person left.
            if sm_fast_motion < self.motion_exit_threshold:
                if self._low_motion_since is None:
                    self._low_motion_since = now
            else:
                self._low_motion_since = None
            stuck_timeout = (
                self.stuck_exit_seconds > 0.0
                and self._low_motion_since is not None
                and (now - self._low_motion_since) >= self.stuck_exit_seconds
            )

            if (below_exit and now >= self._presence_hold_until) or stuck_timeout:
                self._presence_active = False
                self._low_motion_since = None
                # Give partial credit so COM5 re-enters in ~1 frame, not 4
                self._consecutive_hits = max(self.enter_hits_required - 1, 1)
                trigger = None
            elif below_exit:
                held = True
                trigger = "hold"
            elif trigger is None:
                trigger = "hysteresis"
        elif confirmed_enter:
            self._presence_active = True
            self._presence_hold_until = now + self.hold_seconds
            self._low_motion_since = None
        else:
            self._low_motion_since = None
            trigger = None

        hold_remaining_s = 0.0
        if self._presence_active:
            hold_remaining_s = max(0.0, self._presence_hold_until - now)

        motion_conf = self._confidence_component(
            sm_stable_motion, self.motion_exit_threshold, self.threshold_multiplier
        )
        shift_conf = self._confidence_component(
            sm_stable_shift, self.shift_exit_threshold, self.shift_threshold
        )
        confidence = max(motion_conf, shift_conf)
        if self._presence_active:
            hold_fraction = (
                self._clamp(hold_remaining_s / self.hold_seconds)
                if self.hold_seconds > 0.0 else 0.0
            )
            confidence = max(confidence, 0.55 * hold_fraction)
        else:
            confidence *= self._clamp(enter_hits / self.enter_hits_required)

        return DetectorState(
            status="PRESENCE" if self._presence_active else "NO_PRESENCE",
            baseline_std=self.baseline_std,
            current_std=current_std,
            ratio=max(sm_stable_motion, sm_stable_shift),
            progress=progress,
            motion_ratio=sm_stable_motion,
            shift_score=sm_stable_shift,
            shift_threshold=self.shift_threshold,
            trigger=trigger,
            raw_motion_ratio=stable_motion,
            raw_shift_score=stable_shift,
            motion_exit_threshold=self.motion_exit_threshold,
            shift_exit_threshold=self.shift_exit_threshold,
            confidence=round(confidence * 100.0, 1),
            hold_remaining_s=hold_remaining_s,
            enter_hits=enter_hits,
            enter_hits_required=self.enter_hits_required,
            held=held,
            fast_motion_ratio=sm_fast_motion,
            stable_motion_ratio=sm_stable_motion,
            fast_shift_score=sm_fast_shift,
            stable_shift_score=sm_stable_shift,
            fast_window=self.fast_window,
            stable_window=self.window,
            selected_subcarriers=(
                int(self._selected_idx.size) if self._selected_idx is not None else 0
            ),
            total_subcarriers=self._total_subcarriers,
            auto_tuned=self._auto_tuned,
            ai_filter_enabled=self._denoiser.enabled,
            ai_filter_components=self._denoiser.n_components,
        )

    def _waiting_state(self, status: str, progress: float) -> DetectorState:
        return DetectorState(
            status=status,
            baseline_std=self.baseline_std,
            current_std=None,
            ratio=None,
            progress=progress,
            shift_threshold=self.shift_threshold,
            motion_exit_threshold=self.motion_exit_threshold,
            shift_exit_threshold=self.shift_exit_threshold,
            enter_hits_required=self.enter_hits_required,
            fast_window=self.fast_window,
            stable_window=self.window,
            selected_subcarriers=(
                int(self._selected_idx.size) if self._selected_idx is not None else 0
            ),
            total_subcarriers=self._total_subcarriers,
            auto_tuned=self._auto_tuned,
            ai_filter_enabled=self._denoiser.enabled,
            ai_filter_components=self._denoiser.n_components,
        )

    def _update_from_frames(
        self,
        frames: Sequence[Sequence[float]],
        samples: Sequence[float],
        phase_frames: Optional[Sequence[Sequence[float]]] = None,
    ) -> Optional[DetectorState]:
        n = len(frames)
        if self.baseline_std is None:
            if n < self.baseline_size:
                return self._waiting_state("CALIBRATING", n / self.baseline_size)
            if not self._learn_baseline(frames, samples, phase_frames):
                return None

        if n < self.fast_window:
            return self._waiting_state("WAITING", 1.0)

        # Fix B: stable window — used for entry decisions
        stable_size = min(self.window, n)
        stable_amp = self._matrix(frames[-stable_size:], self._baseline_width)
        stable_amp = self._apply_selected(stable_amp)
        stable_amp = self._denoiser.filter(stable_amp)
        if stable_amp.shape[0] >= 2:
            stable_motion = self._motion_score(stable_amp) / max(self.baseline_std, 1e-9)
            stable_shift = self._shift_score(stable_amp)
        else:
            stable_motion, stable_shift = 0.0, 0.0

        # Fix B: fast window — used for exit decisions
        fast_amp = self._matrix(frames[-self.fast_window:], self._baseline_width)
        fast_amp = self._apply_selected(fast_amp)
        fast_amp = self._denoiser.filter(fast_amp)
        if fast_amp.shape[0] >= 2:
            fast_motion = self._motion_score(fast_amp) / max(self.baseline_std, 1e-9)
            fast_shift = self._shift_score(fast_amp)
        else:
            fast_motion, fast_shift = 0.0, 0.0

        # RMS fallback blended into both windows
        rms_stable_motion, rms_stable_shift = self._rms_scores(samples, stable_size)
        rms_fast_motion, rms_fast_shift = self._rms_scores(samples, self.fast_window)
        stable_motion = max(stable_motion, rms_stable_motion)
        stable_shift = max(stable_shift, rms_stable_shift)
        fast_motion = max(fast_motion, rms_fast_motion)
        fast_shift = max(fast_shift, rms_fast_shift)

        # Fix E: phase motion on stable window
        stable_phase = 0.0
        if phase_frames and self._phase_baseline_noise is not None:
            ph_size = min(self.window, len(phase_frames))
            stable_ph = self._matrix(phase_frames[-ph_size:], self._baseline_width)
            stable_ph = self._apply_selected(stable_ph)
            if stable_ph.shape[0] >= 2:
                stable_phase = self._phase_motion_score(stable_ph)

        # Fix D: drift compensation — slowly adapt baseline mean during NO_PRESENCE
        # and also during PRESENCE when motion has died down (lets the baseline
        # catch up after a person leaves so a stuck shift_score eventually relaxes).
        drift_alpha = 0.0
        if self._baseline_mean is not None and stable_amp.shape[0] > 0:
            if not self._presence_active:
                drift_alpha = 1e-3
            elif fast_motion < self.motion_exit_threshold:
                drift_alpha = 1e-3
        if drift_alpha > 0.0:
            recent_mean = np.mean(stable_amp, axis=0)
            w = min(recent_mean.shape[0], self._baseline_mean.shape[0])
            self._baseline_mean[:w] = (
                (1 - drift_alpha) * self._baseline_mean[:w] + drift_alpha * recent_mean[:w]
            )

        self._last_fast_motion_ratio = fast_motion
        self._last_stable_motion_ratio = stable_motion
        self._last_fast_shift_score = fast_shift
        self._last_stable_shift_score = stable_shift

        current_std = self.baseline_std * max(stable_motion, fast_motion)

        return self._state_from_scores(
            stable_motion, stable_shift,
            fast_motion, fast_shift,
            stable_phase,
            current_std,
        )

    def update(
        self,
        samples: Sequence[float],
        frames: Optional[Sequence[Sequence[float]]] = None,
        phase_frames: Optional[Sequence[Sequence[float]]] = None,
    ) -> DetectorState:
        if self.warmup_seconds > 0 and self._warmup_start is not None:
            elapsed = time.monotonic() - self._warmup_start
            if elapsed < self.warmup_seconds:
                return self._waiting_state("CALIBRATING", elapsed / self.warmup_seconds)

        if frames:
            state = self._update_from_frames(frames, samples, phase_frames)
            if state is not None:
                return state

        # Fallback: RMS-only mode when no per-subcarrier frames are available
        n = len(samples)
        if self.baseline_std is None:
            if n < self.baseline_size:
                return self._waiting_state("CALIBRATING", n / self.baseline_size)
            arr = np.asarray(samples[: self.baseline_size], dtype=np.float64)
            self.baseline_std = max(float(np.std(arr)), 1e-9)
            self._reset_runtime_state()

        if n < self.fast_window:
            return self._waiting_state("WAITING", 1.0)

        fast_recent = np.asarray(samples[-self.fast_window:], dtype=np.float64)
        stable_size = min(self.window, n)
        stable_recent = np.asarray(samples[-stable_size:], dtype=np.float64)
        fast_ratio = float(np.std(fast_recent)) / self.baseline_std
        stable_ratio = float(np.std(stable_recent)) / self.baseline_std
        self._last_fast_motion_ratio = fast_ratio
        self._last_stable_motion_ratio = stable_ratio
        self._last_fast_shift_score = 0.0
        self._last_stable_shift_score = 0.0

        current_std = self.baseline_std * max(fast_ratio, stable_ratio)
        return self._state_from_scores(
            stable_ratio, 0.0,   # stable for entry
            fast_ratio, 0.0,     # fast for exit
            0.0,                 # phase not available in RMS mode
            current_std,
        )
