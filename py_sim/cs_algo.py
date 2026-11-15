import numpy as np

C = 299792458.0
DF = 1e6
NFFT = 512


def combine(iq_init, iq_refl, valid):
    return np.where(valid, iq_init * iq_refl, 0)


def phase_slope(H):
    k = np.arange(len(H))
    slope = np.angle(np.sum(H[1:] * np.conj(H[:-1])))

    m = H != 0
    k = k[m]
    r = H[m] * np.exp(-1j * slope * k)
    phi = np.unwrap(np.angle(r))
    w = np.abs(r)

    k_mean = np.sum(w * k) / np.sum(w)
    slope += np.sum(w * (k - k_mean) * phi) / np.sum(w * (k - k_mean) ** 2)
    return -C * slope / (4 * np.pi * DF)


def ifft_profile(H, window=False):
    if window:
        H = H * np.hanning(len(H))
    return np.abs(np.fft.ifft(H, NFFT))


def earliest_peak(profile, threshold):
    g = np.argmax(profile)
    for i in range(1, g):
        if profile[i] > profile[i - 1] and profile[i] > profile[i + 1] and profile[i] >= threshold * profile[g]:
            return i
    return g


def interp_peak(profile, idx):
    early = profile[(idx - 1) % NFFT]
    prompt = profile[idx]
    late = profile[(idx + 1) % NFFT]
    return idx + (late - early) / (4 * prompt - 2 * (early + late))


def ifft_estimate(H, threshold=0.4, window=False):
    profile = ifft_profile(H, window)
    idx = earliest_peak(profile, threshold)
    return interp_peak(profile, idx) * C / (2 * NFFT * DF)
