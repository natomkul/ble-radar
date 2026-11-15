#ifndef RADAR_SVC_H_
#define RADAR_SVC_H_

#include <stdint.h>

#include <zephyr/toolchain.h>
#include <zephyr/bluetooth/uuid.h>

#define RADAR_SVC_UUID_VAL \
	BT_UUID_128_ENCODE(0x5a1e0001, 0x6b2c, 0x4f3a, 0x9d1e, 0x0c4b7a2f8e10)
#define RADAR_IQ_UUID_VAL \
	BT_UUID_128_ENCODE(0x5a1e0002, 0x6b2c, 0x4f3a, 0x9d1e, 0x0c4b7a2f8e10)

#define RADAR_SVC_UUID BT_UUID_DECLARE_128(RADAR_SVC_UUID_VAL)
#define RADAR_IQ_UUID  BT_UUID_DECLARE_128(RADAR_IQ_UUID_VAL)

#define RADAR_MAX_CHANNELS 72

struct radar_iq
{
	uint8_t channel;
	int16_t i;
	int16_t q;
	uint8_t quality;
} __packed;

struct radar_iq_report
{
	uint16_t procedure_counter;
	uint8_t count;
	struct radar_iq iq[RADAR_MAX_CHANNELS];
} __packed;

#endif
