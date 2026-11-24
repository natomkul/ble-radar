import numpy as np

C = 299792458.0
DF = 1e6
NFFT = 512
D_GRID = np.arange(0, C / (2 * DF), 0.01)


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


def music_covariance(H, valid, M):
    W = len(H) - M + 1
    V = np.array([valid[k:k + M] for k in range(W)]).astype(float)
    X = np.array([H[k:k + M] for k in range(W)]) * V
    n = V.T @ V
    return (X.T @ X.conj()) / np.maximum(n, 1), int(n.min())


def music_spectrum(R, P, d_grid):
    M = len(R)
    _, V = np.linalg.eigh(R)
    En = V[:, :M - P]
    A = np.exp(-1j * 4 * np.pi * DF * np.outer(np.arange(M), d_grid) / C)
    return 1 / np.sum(np.abs(En.conj().T @ A) ** 2, axis=0)


def music_estimate(H, valid, P, M=30):
    R, n = music_covariance(H, valid, M)
    if n == 0:
        return np.nan
    S = music_spectrum(R, P, D_GRID)
    i = np.where((S[1:-1] > S[:-2]) & (S[1:-1] > S[2:]))[0] + 1
    return D_GRID[np.min(i[np.argsort(S[i])[-P:]])]
