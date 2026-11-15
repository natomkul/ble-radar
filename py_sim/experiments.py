import os
import sys

import numpy as np
import matplotlib.pyplot as plt

import cs_algo as algo
import cs_channel as ch

FIG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fig")
DIST = np.arange(algo.NFFT) * algo.C / (2 * algo.NFFT * algo.DF)


def save(fig, name):
    os.makedirs(FIG_DIR, exist_ok=True)
    path = os.path.join(FIG_DIR, f"{name}.png")
    fig.savefig(path, dpi=150)
    plt.close(fig)
    print(f"zapisano {path}")


def e1():
    d = 5.0
    iq_init, iq_refl, valid = ch.generate([(d, 1.0)], rng=np.random.default_rng(0))
    H = algo.combine(iq_init, iq_refl, valid)

    f = ch.F / 1e6
    wrapped = np.full(ch.N_CH, np.nan)
    wrapped[valid] = np.angle(H[valid])
    unwrapped = np.full(ch.N_CH, np.nan)
    unwrapped[valid] = np.unwrap(wrapped[valid])

    profile = algo.ifft_profile(H)
    profile /= profile.max()
    d_ifft = algo.ifft_estimate(H)
    print(f"E1: phase slope {algo.phase_slope(H):.3f} m, IFFT {d_ifft:.3f} m (prawda {d} m)")

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(8, 7))
    ax1.axhspan(-np.pi, np.pi, color="gold", alpha=0.3, label="zakres radia: −π…π")
    ax1.plot(f, wrapped, ".", label="zawinięta (z radia)")
    ax1.plot(f, unwrapped, "-", label="rozwinięta")
    ax1.set_xlabel("częstotliwość [MHz]")
    ax1.set_ylabel("faza H [rad]")
    ax1.set_title("Phase slope: faza maleje liniowo z częstotliwością")
    ax1.legend()
    ax1.grid(True)

    ax2.plot(DIST, profile, ".-")
    ax2.axvline(d, color="g", linewidth=4, alpha=0.4, label="prawda")
    ax2.axvline(d_ifft, color="r", linestyle="--", label="estymata IFFT")
    ax2.set_xlim(0, 30)
    ax2.set_xlabel("odległość [m]")
    ax2.set_ylabel("|IFFT| (znormalizowane)")
    ax2.set_title("IFFT: profil odległości z pikiem w miejscu ścieżki")
    ax2.legend()
    ax2.grid(True)

    fig.suptitle(f"E1: zasada działania, d = {d} m")
    fig.tight_layout()
    save(fig, "e1")


def e2():
    rng = np.random.default_rng(0)
    d_true = np.arange(0.0, 200.0, 0.1)
    ps, ifft = [], []
    for d in d_true:
        H = algo.combine(*ch.generate([(d, 1.0)], rng=rng))
        ps.append(algo.phase_slope(H))
        ifft.append(algo.ifft_estimate(H))

    d_max = algo.C / (2 * algo.DF)
    print(f"E2: c / (2 * DF) = {d_max:.1f} m")
    print("    prawda [m]   PS [m]   IFFT [m]")
    for d in [70.0, 80.0, 140.0, 155.0]:
        i = int(round(d * 10))
        print(f"    {d:10.1f}   {ps[i]:6.1f}   {ifft[i]:8.1f}")

    fig, ax = plt.subplots(figsize=(8, 5))
    ax.plot(d_true, d_true, "g:", label="prawda")
    ax.plot(d_true, ps, label="phase slope")
    ax.plot(d_true, ifft, "--", label="IFFT")
    ax.axvline(d_max, color="gray", linestyle="--", label=f"c / (2·Δf) = {d_max:.0f} m")
    ax.set_xlabel("prawdziwa odległość [m]")
    ax.set_ylabel("estymata [m]")
    ax.set_title("E2: zakres jednoznaczności (bez szumu)")
    ax.legend()
    ax.grid(True)
    fig.tight_layout()
    save(fig, "e2")


def e3():
    rng = np.random.default_rng(0)
    d_true = np.arange(1.0, 20.0, 0.01)
    ps, ifft_arg, ifft_int = [], [], []
    for d in d_true:
        H = algo.combine(*ch.generate([(d, 1.0)], rng=rng))
        profile = algo.ifft_profile(H)
        ps.append(algo.phase_slope(H))
        ifft_arg.append(DIST[np.argmax(profile)])
        ifft_int.append(algo.ifft_estimate(H))

    err_ps = 100 * (np.array(ps) - d_true)
    err_arg = 100 * (np.array(ifft_arg) - d_true)
    err_int = 100 * (np.array(ifft_int) - d_true)
    print(f"E3: max |błąd| [cm]: PS {np.abs(err_ps).max():.2e},"
          f" IFFT argmax {np.abs(err_arg).max():.2f}, IFFT interpolacja {np.abs(err_int).max():.2f}")

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(8, 7), sharex=True)
    ax1.plot(d_true, err_arg, label="IFFT bez interpolacji")
    ax1.plot(d_true, err_int, "--", linewidth=2, label="IFFT z interpolacją")
    ax1.plot(d_true, err_ps, ":", linewidth=2, label="phase slope")
    ax1.set_ylabel("błąd [cm]")
    ax1.set_title("Wszystkie metody")
    ax1.legend()
    ax1.grid(True)

    ax2.plot(d_true, err_int, "--", linewidth=2, color="C1", label="IFFT z interpolacją")
    ax2.plot(d_true, err_ps, ":", linewidth=2, color="C2", label="phase slope")
    ax2.set_xlim(4, 6)
    ax2.set_xlabel("prawdziwa odległość [m]")
    ax2.set_ylabel("błąd [cm]")
    ax2.set_title("Powiększenie: interpolacja i phase slope")
    ax2.legend()
    ax2.grid(True)

    fig.suptitle("E3: błąd bez szumu")
    fig.tight_layout()
    save(fig, "e3")


EXPERIMENTS = {1: e1, 2: e2, 3: e3}

if __name__ == "__main__":
    if len(sys.argv) < 2:
        for n in sorted(EXPERIMENTS):
            EXPERIMENTS[n]()
    elif int(sys.argv[1]) in EXPERIMENTS:
        EXPERIMENTS[int(sys.argv[1])]()
    else:
        sys.exit(f"Użycie: python3 experiments.py [nr], dostępne: {sorted(EXPERIMENTS)}")
