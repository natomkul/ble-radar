#include <math.h>
#include <stdbool.h>

#include "cs_channels.h"
#include "ifft.h"

#define SPEED_OF_LIGHT 299792458.0f
#define DF             1e6f
#define PI             3.14159265f
#define NFFT           512
#define THRESHOLD      0.4f
#define MIN_DISTANCE   -2.0f
#define MIN_BIN        ((int)(MIN_DISTANCE * 2.0f * NFFT * DF / SPEED_OF_LIGHT))

static float cos_table[NFFT];
static float sin_table[NFFT];
static float profile[NFFT];

static void tables_init(void)
{
	static bool done;

	if (done)
	{
		return;
	}

	for (int i = 0; i < NFFT; i++)
	{
		cos_table[i] = cosf(2.0f * PI * i / NFFT);
		sin_table[i] = sinf(2.0f * PI * i / NFFT);
	}

	done = true;
}

static void ifft_profile(const float *re, const float *im)
{
	for (int n = 0; n < NFFT; n++)
	{
		float sre = 0.0f;
		float sim = 0.0f;

		for (int k = 0; k < CS_N_CH; k++)
		{
			int idx = (k * n) % NFFT;

			sre += re[k] * cos_table[idx] - im[k] * sin_table[idx];
			sim += re[k] * sin_table[idx] + im[k] * cos_table[idx];
		}

		profile[n] = sqrtf(sre * sre + sim * sim);
	}
}

static float at(int idx)
{
	return profile[(idx + NFFT) % NFFT];
}

static int earliest_peak(void)
{
	int g = 0;

	for (int i = 1; i < NFFT; i++)
	{
		if (profile[i] > profile[g])
		{
			g = i;
		}
	}

	if (g > NFFT / 2)
	{
		g -= NFFT;
	}

	for (int i = MIN_BIN; i < g; i++)
	{
		if (at(i) > at(i - 1) && at(i) > at(i + 1) && at(i) >= THRESHOLD * at(g))
		{
			return i;
		}
	}

	return g;
}

static float interp_peak(int idx)
{
	float early = at(idx - 1);
	float prompt = at(idx);
	float late = at(idx + 1);

	return idx + (late - early) / (4.0f * prompt - 2.0f * (early + late));
}

float ifft_estimate(const float *re, const float *im)
{
	tables_init();
	ifft_profile(re, im);

	return interp_peak(earliest_peak()) * SPEED_OF_LIGHT / (2.0f * NFFT * DF);
}
