"""Unsupervised CSI denoiser used to clean amplitude frames before scoring.

Two stages, both learned per-sensor stream with no labels and no offline training:

1. Hampel filter across time for each subcarrier — replaces samples that lie
   more than ``n_sigmas * MAD`` from the local rolling median with the median
   itself. Kills burst packet noise / RF spikes without smearing real motion.

2. PCA reconstruction — at calibration the denoiser learns the principal
   subspace that explains the empty-room baseline. New frames are projected
   onto the top-K components and reconstructed, collapsing per-subcarrier
   noise that lives outside the stable signal subspace while preserving the
   real channel structure.

The number of components is chosen to retain a configurable share of the
baseline variance (default 92 %), clamped to a safe range.
"""

from __future__ import annotations

from typing import Optional

import numpy as np


def hampel_filter_matrix(
    matrix: np.ndarray,
    window: int = 7,
    n_sigmas: float = 3.0,
) -> np.ndarray:
    """Hampel filter along the time axis (axis=0) for each subcarrier (column)."""
    T, D = matrix.shape
    if T < 3 or D == 0:
        return matrix
    window = max(3, min(window, T))
    half = window // 2
    out = matrix.copy()
    for t in range(T):
        lo = max(0, t - half)
        hi = min(T, t + half + 1)
        block = matrix[lo:hi]
        med = np.median(block, axis=0)
        mad = np.median(np.abs(block - med), axis=0) * 1.4826
        threshold = n_sigmas * np.maximum(mad, 1e-9)
        mask = np.abs(matrix[t] - med) > threshold
        if mask.any():
            out[t, mask] = med[mask]
    return out


class PCADenoiser:
    def __init__(
        self,
        keep_variance: float = 0.92,
        min_components: int = 4,
        max_components: int = 24,
    ):
        self.keep_variance = float(np.clip(keep_variance, 0.50, 0.999))
        self.min_components = max(2, min_components)
        self.max_components = max(self.min_components, max_components)
        self.mean: Optional[np.ndarray] = None
        self.components: Optional[np.ndarray] = None  # (K, D)

    @property
    def fitted(self) -> bool:
        return self.components is not None and self.mean is not None

    @property
    def n_components(self) -> int:
        return 0 if self.components is None else int(self.components.shape[0])

    def fit(self, frames: np.ndarray) -> None:
        if frames.size == 0 or frames.shape[0] < self.min_components * 2:
            return
        mean = frames.mean(axis=0)
        X = frames - mean
        try:
            _, S, Vt = np.linalg.svd(X, full_matrices=False)
        except np.linalg.LinAlgError:
            return
        var = S * S
        total = float(var.sum())
        if total <= 1e-12:
            return
        cum = np.cumsum(var) / total
        k = int(np.searchsorted(cum, self.keep_variance) + 1)
        k = max(self.min_components, min(k, self.max_components, len(S)))
        self.mean = mean
        self.components = Vt[:k]

    def transform(self, matrix: np.ndarray) -> np.ndarray:
        if not self.fitted or matrix.size == 0:
            return matrix
        width = min(matrix.shape[1], self.mean.shape[0])
        if width == 0:
            return matrix
        out = matrix.copy()
        X = matrix[:, :width] - self.mean[:width]
        comp = self.components[:, :width]
        coef = X @ comp.T          # (T, K)
        recon = coef @ comp        # (T, width)
        out[:, :width] = recon + self.mean[:width]
        return out


class CSIDenoiser:
    """Hampel outlier rejection + PCA subspace reconstruction."""

    def __init__(
        self,
        enabled: bool = True,
        hampel_window: int = 7,
        hampel_sigmas: float = 3.0,
        pca_keep_variance: float = 0.92,
    ):
        self.enabled = enabled
        self.hampel_window = hampel_window
        self.hampel_sigmas = hampel_sigmas
        self._pca_keep_variance = pca_keep_variance
        self.pca = PCADenoiser(keep_variance=pca_keep_variance)

    def reset(self) -> None:
        self.pca = PCADenoiser(keep_variance=self._pca_keep_variance)

    def fit_baseline(self, baseline: np.ndarray) -> None:
        if not self.enabled or baseline.size == 0:
            return
        cleaned = hampel_filter_matrix(baseline, self.hampel_window, self.hampel_sigmas)
        self.pca.fit(cleaned)

    def filter(self, matrix: np.ndarray) -> np.ndarray:
        if not self.enabled or matrix.size == 0:
            return matrix
        cleaned = hampel_filter_matrix(matrix, self.hampel_window, self.hampel_sigmas)
        return self.pca.transform(cleaned)

    @property
    def fitted(self) -> bool:
        return self.pca.fitted

    @property
    def n_components(self) -> int:
        return self.pca.n_components
