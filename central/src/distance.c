#include <math.h>

#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/logging/log.h>

#include "cs_channels.h"
#include "distance.h"
#include "ifft.h"
#include "phase_slope.h"

LOG_MODULE_REGISTER(distance, LOG_LEVEL_INF);

#define AVG_N 10

static float ps_hist[AVG_N];
static float ifft_hist[AVG_N];
static uint8_t avg_pos;
static uint8_t avg_count;

static void mean_std(const float *v, float *mean, float *std)
{
	float sum = 0.0f;

	for (int n = 0; n < AVG_N; n++)
	{
		sum += v[n];
	}

	*mean = sum / AVG_N;

	float sq = 0.0f;

	for (int n = 0; n < AVG_N; n++)
	{
		float dv = v[n] - *mean;

		sq += dv * dv;
	}

	*std = sqrtf(sq / AVG_N);
}

static const struct radar_iq *iq_find(const struct radar_iq_report *report, uint8_t channel)
{
	for (uint8_t n = 0; n < report->count; n++)
	{
		if (report->iq[n].channel == channel)
		{
			return &report->iq[n];
		}
	}

	return NULL;
}

void distance_reset(void)
{
	avg_pos = 0;
	avg_count = 0;
}

void distance_process(const struct radar_iq_report *local, const struct radar_iq_report *peer)
{
	float re[CS_N_CH] = {0};
	float im[CS_N_CH] = {0};
	uint8_t valid = 0;

	for (uint8_t n = 0; n < local->count; n++)
	{
		const struct radar_iq *c = &local->iq[n];
		const struct radar_iq *p = iq_find(peer, c->channel);
		int k = c->channel - CS_FIRST_CH;

		if (!p || c->quality != BT_HCI_LE_CS_TONE_QUALITY_HIGH ||
		    p->quality != BT_HCI_LE_CS_TONE_QUALITY_HIGH || k < 0 || k >= CS_N_CH)
		{
			continue;
		}

		re[k] = c->i * p->i - c->q * p->q;
		im[k] = c->i * p->q + c->q * p->i;
		valid++;
	}

    float d_ps = phase_slope(re, im);
	
    if (isnan(d_ps))
	{
		LOG_WRN("Procedure %u: too few valid channels (%u)", peer->procedure_counter, valid);
		return;
	}
	
	float d_ifft = ifft_estimate(re, im);

	ps_hist[avg_pos] = d_ps;
	ifft_hist[avg_pos] = d_ifft;
	avg_pos = (avg_pos + 1) % AVG_N;

	if (avg_count < AVG_N)
	{
		avg_count++;
	}

	if (avg_count < AVG_N)
	{
		return;
	}

	float ps_mean, ps_std, ifft_mean, ifft_std;

	mean_std(ps_hist, &ps_mean, &ps_std);
	mean_std(ifft_hist, &ifft_mean, &ifft_std);

	LOG_INF("Avg PS %d cm (std %d cm), IFFT %d cm (std %d cm)",
		(int)(ps_mean * 100.0f), (int)(ps_std * 100.0f),
		(int)(ifft_mean * 100.0f), (int)(ifft_std * 100.0f));
}
