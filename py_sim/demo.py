import numpy as np
import matplotlib.pyplot as plt

import cs_algo as algo
import cs_channel as ch

PATHS = [(4.0, 1.0)]
SNR_DB = 20
P_BAD = 0.05
N_MEAS = 20

rng = np.random.default_rng()
ps, ifft = [], []

print(f"ścieżki {PATHS}, SNR {SNR_DB} dB, p_bad {P_BAD}")
print(" nr   PS [m]   IFFT [m]")
for n in range(N_MEAS):
    H = algo.combine(*ch.generate(PATHS, SNR_DB, P_BAD, rng))
    ps.append(algo.phase_slope(H))
    ifft.append(algo.ifft_estimate(H))
    print(f"{n:3d}   {ps[-1]:6.3f}   {ifft[-1]:8.3f}")

print(f"średnia   {np.mean(ps):6.3f}   {np.mean(ifft):8.3f}")
print(f"odch.std  {np.std(ps):6.3f}   {np.std(ifft):8.3f}")

plt.plot(ps, ".-", label="phase slope")
plt.plot(ifft, ".-", label="IFFT")
plt.axhline(PATHS[0][0], color="g", linestyle=":", label="prawda")
plt.xlabel("nr pomiaru")
plt.ylabel("odległość [m]")
plt.title(f"ścieżki {PATHS}, SNR {SNR_DB} dB, p_bad {P_BAD}")
plt.legend()
plt.grid(True)
plt.show()
