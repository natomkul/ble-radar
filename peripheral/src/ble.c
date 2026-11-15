#include <stddef.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/cs.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>

#include "ble.h"
#include "radar_svc.h"

LOG_MODULE_REGISTER(ble, LOG_LEVEL_INF);

static struct bt_conn *conn;

K_SEM_DEFINE(sem_connected, 0, 1);

static void iq_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	LOG_INF("IQ notifications %s", value == BT_GATT_CCC_NOTIFY ? "enabled" : "disabled");
}

BT_GATT_SERVICE_DEFINE(radar_svc,
	BT_GATT_PRIMARY_SERVICE(RADAR_SVC_UUID),
	BT_GATT_CHARACTERISTIC(RADAR_IQ_UUID, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE,
			       NULL, NULL, NULL),
	BT_GATT_CCC(iq_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

static const struct bt_data ad[] =
{
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
	BT_DATA_BYTES(BT_DATA_MANUFACTURER_DATA, 0xFF, 0xFF, 0x01, 0x02, 0x03, 0x04),
};

static int adv_start(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);

	if (err)
	{
		LOG_ERR("Advertising failed to start (err %d)", err);
		return err;
	}

	LOG_INF("Advertising started");
	return 0;
}

static struct radar_iq_report report;

static bool cs_step_collect(struct bt_le_cs_subevent_step *step, void *user_data)
{
	if (step->mode != BT_HCI_OP_LE_CS_MAIN_MODE_2 || report.count >= RADAR_MAX_CHANNELS)
	{
		return true;
	}

	const struct bt_hci_le_cs_step_data_mode_2 *data = (const void *)step->data;
	struct bt_le_cs_iq_sample iq = bt_le_cs_parse_pct(data->tone_info[0].phase_correction_term);

	report.iq[report.count].channel = step->channel;
	report.iq[report.count].i = iq.i;
	report.iq[report.count].q = iq.q;
	report.iq[report.count].quality = data->tone_info[0].quality_indicator;
	report.count++;

	return true;
}

static void cs_report_send(struct bt_conn *c)
{
	if (!bt_gatt_is_subscribed(c, &radar_svc.attrs[1], BT_GATT_CCC_NOTIFY))
	{
		return;
	}

	uint16_t len = offsetof(struct radar_iq_report, iq) + report.count * sizeof(struct radar_iq);
	int err = bt_gatt_notify(c, &radar_svc.attrs[1], &report, len);

	if (err)
	{
		LOG_ERR("IQ notify failed (err %d)", err);
		return;
	}

	LOG_INF("Procedure %u sent, %u channels", report.procedure_counter, report.count);
}

static void cs_subevent_result(struct bt_conn *c, struct bt_conn_le_cs_subevent_result *result)
{
	if (result->header.procedure_counter != report.procedure_counter)
	{
		report.procedure_counter = result->header.procedure_counter;
		report.count = 0;
	}

	if (result->step_data_buf)
	{
		bt_le_cs_step_data_parse(result->step_data_buf, cs_step_collect, NULL);
	}

	if (result->header.procedure_done_status == BT_CONN_LE_CS_PROCEDURE_COMPLETE)
	{
		cs_report_send(c);
		report.count = 0;
	}
	else if (result->header.procedure_done_status == BT_CONN_LE_CS_PROCEDURE_ABORTED)
	{
		report.count = 0;
	}
}

static void connected(struct bt_conn *c, uint8_t err)
{
	LOG_INF("Connected (err %u)", err);

	if (err)
	{
		return;
	}

	conn = bt_conn_ref(c);
	k_sem_give(&sem_connected);
}

static void disconnected(struct bt_conn *c, uint8_t reason)
{
	LOG_INF("Disconnected (reason %u)", reason);
	bt_conn_unref(conn);
	conn = NULL;
}

static void recycled(void)
{
	adv_start();
}

BT_CONN_CB_DEFINE(conn_callbacks) =
{
	.connected = connected,
	.disconnected = disconnected,
	.recycled = recycled,
	.le_cs_subevent_data_available = cs_subevent_result,
};

int ble_init(void)
{
	int err = bt_enable(NULL);
	
    if (err) 
    {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return err;
	}

	return adv_start();
}

void ble_reflector_run(void)
{
	const struct bt_le_cs_set_default_settings_param settings =
	{
		.enable_initiator_role = false,
		.enable_reflector_role = true,
		.cs_sync_antenna_selection = BT_LE_CS_ANTENNA_SELECTION_OPT_REPETITIVE,
		.max_tx_power = BT_HCI_OP_LE_CS_MAX_MAX_TX_POWER,
	};

	while (true)
	{
		k_sem_take(&sem_connected, K_FOREVER);

		int err = bt_le_cs_set_default_settings(conn, &settings);

		if (err)
		{
			LOG_ERR("CS default settings failed (err %d)", err);
			continue;
		}

		LOG_INF("CS reflector role enabled");
	}
}
