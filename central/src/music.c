#include <math.h>
#include <stdbool.h>

#include "cs_channels.h"
#include "music.h"

#define MUSIC_M 30
#define MUSIC_W (CS_N_CH - MUSIC_M + 1)

#define MUSIC_P        3
#define MUSIC_STEP     0.01f
#define MUSIC_GRID_MAX 1024

#define JACOBI_MAX_SWEEPS 15
#define JACOBI_TOL        1e-10f

#define SPEED_OF_LIGHT 299792458.0f
#define DF             1e6f
#define PI             3.14159265f

static float r_re[MUSIC_M][MUSIC_M];
static float r_im[MUSIC_M][MUSIC_M];
static float v_re[MUSIC_M][MUSIC_M];
static float v_im[MUSIC_M][MUSIC_M];
static float eig[MUSIC_M];
static float spec[MUSIC_GRID_MAX];

static bool is_valid(const float *re, const float *im, int k)
{
	return re[k] != 0.0f || im[k] != 0.0f;
}

static bool covariance(const float *re, const float *im)
{
	for (int i = 0; i < MUSIC_M; i++)
	{
		for (int j = i; j < MUSIC_M; j++)
		{
			float sre = 0.0f;
			float sim = 0.0f;
			int n = 0;

			for (int w = 0; w < MUSIC_W; w++)
			{
				int a = w + i;
				int b = w + j;

				if (!is_valid(re, im, a) || !is_valid(re, im, b))
				{
					continue;
				}

				sre += re[a] * re[b] + im[a] * im[b];
				sim += im[a] * re[b] - re[a] * im[b];
				n++;
			}

			if (n == 0)
			{
				return false;
			}

			r_re[i][j] = sre / n;
			r_im[i][j] = sim / n;
			r_re[j][i] = r_re[i][j];
			r_im[j][i] = -r_im[i][j];
		}
	}

	return true;
}

static void cmul(float *re, float *im, float br, float bi)
{
	float ar = *re;

	*re = ar * br - *im * bi;
	*im = ar * bi + *im * br;
}

static void rotate(int p, int q)
{
	float mag = sqrtf(r_re[p][q] * r_re[p][q] + r_im[p][q] * r_im[p][q]);

	if (mag == 0.0f)
	{
		return;
	}

	float app = r_re[p][p];
	float aqq = r_re[q][q];
	float er = r_re[p][q] / mag;
	float ei = r_im[p][q] / mag;

	for (int k = 0; k < MUSIC_M; k++)
	{
		cmul(&r_re[k][q], &r_im[k][q], er, -ei);
		cmul(&v_re[k][q], &v_im[k][q], er, -ei);
	}

	float theta = (aqq - app) / (2.0f * mag);
	float t = 1.0f / (fabsf(theta) + sqrtf(theta * theta + 1.0f));

	if (theta < 0.0f)
	{
		t = -t;
	}

	float c = 1.0f / sqrtf(t * t + 1.0f);
	float s = t * c;

	for (int k = 0; k < MUSIC_M; k++)
	{
		float kp_re = v_re[k][p];
		float kp_im = v_im[k][p];
		float kq_re = v_re[k][q];
		float kq_im = v_im[k][q];

		v_re[k][p] = c * kp_re - s * kq_re;
		v_im[k][p] = c * kp_im - s * kq_im;
		v_re[k][q] = s * kp_re + c * kq_re;
		v_im[k][q] = s * kp_im + c * kq_im;

		if (k == p || k == q)
		{
			continue;
		}

		kp_re = r_re[k][p];
		kp_im = r_im[k][p];
		kq_re = r_re[k][q];
		kq_im = r_im[k][q];

		r_re[k][p] = c * kp_re - s * kq_re;
		r_im[k][p] = c * kp_im - s * kq_im;
		r_re[k][q] = s * kp_re + c * kq_re;
		r_im[k][q] = s * kp_im + c * kq_im;

		r_re[p][k] = r_re[k][p];
		r_im[p][k] = -r_im[k][p];
		r_re[q][k] = r_re[k][q];
		r_im[q][k] = -r_im[k][q];
	}

	r_re[p][p] = app - t * mag;
	r_re[q][q] = aqq + t * mag;
	r_im[p][p] = 0.0f;
	r_im[q][q] = 0.0f;
	r_re[p][q] = 0.0f;
	r_im[p][q] = 0.0f;
	r_re[q][p] = 0.0f;
	r_im[q][p] = 0.0f;
}

static void swap_columns(int a, int b)
{
	for (int k = 0; k < MUSIC_M; k++)
	{
		float re = v_re[k][a];
		float im = v_im[k][a];

		v_re[k][a] = v_re[k][b];
		v_im[k][a] = v_im[k][b];
		v_re[k][b] = re;
		v_im[k][b] = im;
	}
}

static int eigen(void)
{
	for (int i = 0; i < MUSIC_M; i++)
	{
		for (int j = 0; j < MUSIC_M; j++)
		{
			v_re[i][j] = (i == j) ? 1.0f : 0.0f;
			v_im[i][j] = 0.0f;
		}
	}

	int sweep;

	for (sweep = 0; sweep < JACOBI_MAX_SWEEPS; sweep++)
	{
		float off = 0.0f;
		float diag = 0.0f;

		for (int p = 0; p < MUSIC_M; p++)
		{
			diag += r_re[p][p] * r_re[p][p];

			for (int q = p + 1; q < MUSIC_M; q++)
			{
				off += r_re[p][q] * r_re[p][q] + r_im[p][q] * r_im[p][q];
			}
		}

		if (off <= JACOBI_TOL * diag)
		{
			break;
		}

		for (int p = 0; p < MUSIC_M - 1; p++)
		{
			for (int q = p + 1; q < MUSIC_M; q++)
			{
				rotate(p, q);
			}
		}
	}

	for (int i = 0; i < MUSIC_M; i++)
	{
		eig[i] = r_re[i][i];
	}

	for (int i = 0; i < MUSIC_M - 1; i++)
	{
		int min = i;

		for (int j = i + 1; j < MUSIC_M; j++)
		{
			if (eig[j] < eig[min])
			{
				min = j;
			}
		}

		if (min != i)
		{
			float tmp = eig[i];

			eig[i] = eig[min];
			eig[min] = tmp;
			swap_columns(i, min);
		}
	}

	return sweep;
}

static float spectrum_at(float d)
{
	float a_re[MUSIC_M];
	float a_im[MUSIC_M];
	float phi = -4.0f * PI * DF * d / SPEED_OF_LIGHT;
	float c = cosf(phi);
	float s = sinf(phi);

	a_re[0] = 1.0f;
	a_im[0] = 0.0f;

	for (int k = 1; k < MUSIC_M; k++)
	{
		a_re[k] = a_re[k - 1] * c - a_im[k - 1] * s;
		a_im[k] = a_re[k - 1] * s + a_im[k - 1] * c;
	}

	float acc = 0.0f;

	for (int m = 0; m < MUSIC_M - MUSIC_P; m++)
	{
		float sre = 0.0f;
		float sim = 0.0f;

		for (int k = 0; k < MUSIC_M; k++)
		{
			sre += v_re[k][m] * a_re[k] + v_im[k][m] * a_im[k];
			sim += v_re[k][m] * a_im[k] - v_im[k][m] * a_re[k];
		}

		acc += sre * sre + sim * sim;
	}

	return 1.0f / acc;
}

float music_estimate(const float *re, const float *im, float d_min, float d_max)
{
	if (!covariance(re, im))
	{
		return NAN;
	}

	eigen();

	int n = (int)((d_max - d_min) / MUSIC_STEP) + 1;

	if (n > MUSIC_GRID_MAX)
	{
		n = MUSIC_GRID_MAX;
	}

	if (n < 3)
	{
		return NAN;
	}

	int g = 0;

	for (int i = 0; i < n; i++)
	{
		spec[i] = spectrum_at(d_min + i * MUSIC_STEP);

		if (spec[i] > spec[g])
		{
			g = i;
		}
	}

	if (g == 0 || g == n - 1)
	{
		return NAN;
	}

	int best[MUSIC_P];
	int nbest = 0;

	for (int i = 1; i < n - 1; i++)
	{
		if (spec[i] <= spec[i - 1] || spec[i] <= spec[i + 1])
		{
			continue;
		}

		if (nbest < MUSIC_P)
		{
			best[nbest++] = i;
			continue;
		}

		int weakest = 0;

		for (int b = 1; b < MUSIC_P; b++)
		{
			if (spec[best[b]] < spec[best[weakest]])
			{
				weakest = b;
			}
		}

		if (spec[i] > spec[best[weakest]])
		{
			best[weakest] = i;
		}
	}

	if (nbest == 0)
	{
		return NAN;
	}

	int first = best[0];

	for (int b = 1; b < nbest; b++)
	{
		if (best[b] < first)
		{
			first = best[b];
		}
	}

	return d_min + first * MUSIC_STEP;
}
