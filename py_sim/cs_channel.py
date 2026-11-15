import numpy as np

C = 299792458.0
F0 = 2404e6
DF = 1e6
N_CH = 75
K = np.arange(N_CH)
F = F0 + K * DF
FORBIDDEN = [21, 22, 23]


def _noise(power, rng):
    return np.sqrt(power / 2) * (rng.standard_normal(N_CH) + 1j * rng.standard_normal(N_CH))


def generate(paths, snr_db=None, p_bad=0.0, rng=None):
    rng = rng or np.random.default_rng()

    h = np.zeros(N_CH, dtype=complex)
    for d, a in paths:
        h += a * np.exp(-1j * 2 * np.pi * F * d / C)

    lo = np.exp(1j * rng.uniform(0, 2 * np.pi, N_CH))
    iq_refl = h / lo
    iq_init = h * lo

    p_signal = np.mean(np.abs(h) ** 2)
    if snr_db is not None:
        p_noise = p_signal / 10 ** (snr_db / 10)
        iq_refl += _noise(p_noise, rng)
        iq_init += _noise(p_noise, rng)

    valid = rng.random(N_CH) >= p_bad
    iq_refl[~valid] = _noise(p_signal, rng)[~valid]
    iq_init[~valid] = _noise(p_signal, rng)[~valid]

    valid[FORBIDDEN] = False
    iq_refl[FORBIDDEN] = 0
    iq_init[FORBIDDEN] = 0

    return iq_init, iq_refl, valid
