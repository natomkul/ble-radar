import numpy as np
import matplotlib.pyplot as plt

import cs_algo as algo
import cs_channel as ch

PATHS = [(4.0, 1.0), (10.0, 0.7)]
SNR_DB = 20
P_BAD = 0.05
P = 3
M = 30
SEED = None

rng = np.random.default_rng(SEED)
iq_init, iq_refl, valid = ch.generate(PATHS, SNR_DB, P_BAD, rng)
H = algo.combine(iq_init, iq_refl, valid)

d_ifft = np.arange(algo.NFFT) * algo.C / (2 * algo.NFFT * algo.DF)
ifft = algo.ifft_profile(H)
R, n = algo.music_covariance(H, valid, M)
music = algo.music_spectrum(R, P, algo.D_GRID)
est_ifft = algo.ifft_estimate(H)
est_music = algo.music_estimate(H, valid, P, M)

l_ifft, = plt.plot(d_ifft, 20 * np.log10(ifft / ifft.max()), label=f"IFFT → {est_ifft:.2f} m")
l_music, = plt.plot(algo.D_GRID, 10 * np.log10(music / music.max()), label=f"MUSIC, min. okien {n} → {est_music:.2f} m")
plt.axvline(est_ifft, color=l_ifft.get_color(), linestyle="--")
plt.axvline(est_music, color=l_music.get_color(), linestyle="--")
plt.axhline(20 * np.log10(0.4), color="gray", linestyle=":", label="próg IFFT")
plt.vlines([d for d, _ in PATHS], -40, 0, colors="g", linestyles=":", label="prawda")
plt.xlim(0, 20)
plt.ylim(-40, 3)
plt.xlabel("odległość [m]")
plt.ylabel("poziom [dB]")
plt.title(f"ścieżki {PATHS}, SNR {SNR_DB} dB, p_bad {P_BAD}, P {P}, M {M}")
plt.legend()
plt.grid(True)
plt.show()
