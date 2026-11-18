#include <math.h>

#include "cs_channels.h"
#include "phase_slope.h"

#define SPEED_OF_LIGHT 299792458.0f
#define DF             1e6f
#define PI             3.14159265f

float phase_slope(const float *re, const float *im)
{
	float sre = 0.0f;
	float sim = 0.0f;

	for (int k = 1; k < CS_N_CH; k++)
	{
		sre += re[k] * re[k - 1] + im[k] * im[k - 1];
		sim += im[k] * re[k - 1] - re[k] * im[k - 1];
	}

	float slope = atan2f(sim, sre);

	float phi[CS_N_CH];
	float w[CS_N_CH];
	float prev = 0.0f;
	float sw = 0.0f;
	float swk = 0.0f;
	int n = 0;

	for (int k = 0; k < CS_N_CH; k++)
	{
		w[k] = 0.0f;
		phi[k] = 0.0f;

		if (re[k] == 0.0f && im[k] == 0.0f)
		{
			continue;
		}

		float c = cosf(slope * k);
		float s = sinf(slope * k);
		float rre = re[k] * c + im[k] * s;
		float rim = im[k] * c - re[k] * s;
		float p = atan2f(rim, rre);

		if (n > 0)
		{
			p -= 2.0f * PI * roundf((p - prev) / (2.0f * PI));
		}

		prev = p;
		phi[k] = p;
		w[k] = sqrtf(rre * rre + rim * rim);
		sw += w[k];
		swk += w[k] * k;
		n++;
	}

	if (n < 2)
	{
		return NAN;
	}

	float k_mean = swk / sw;
	float num = 0.0f;
	float den = 0.0f;

	for (int k = 0; k < CS_N_CH; k++)
	{
		float dk = k - k_mean;

		num += w[k] * dk * phi[k];
		den += w[k] * dk * dk;
	}

	slope += num / den;

	return -SPEED_OF_LIGHT * slope / (4.0f * PI * DF);
}
