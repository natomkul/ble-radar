#include <math.h>

#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/logging/log.h>

#include "cs_channels.h"
#include "distance.h"
#include "ifft.h"
#include "phase_slope.h"

LOG_MODULE_REGISTER(distance, LOG_LEVEL_INF);

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

	LOG_INF("PS %d cm, IFFT %d cm", (int)(d_ps * 100.0f), (int)(d_ifft * 100.0f));
}
