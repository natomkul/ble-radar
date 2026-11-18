#include <stddef.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/cs.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>

#include "ble.h"
#include "cs_channels.h"
#include "distance.h"
#include "radar_svc.h"

LOG_MODULE_REGISTER(ble, LOG_LEVEL_INF);

static const char target_name[] = "peripheral";

static struct bt_conn *conn;

#define CS_CONFIG_ID 0

K_SEM_DEFINE(sem_encrypted, 0, 1);
K_SEM_DEFINE(sem_cs, 0, 1);

static uint8_t cs_status;

static struct radar_iq_report local_report;

static void scan_start(void);

static bool name_match(struct bt_data *data, void *user_data)
{
	bool *found = user_data;

	if (data->type != BT_DATA_NAME_COMPLETE)
	{
		return true;
	}

	if (data->data_len == sizeof(target_name) - 1 &&
	    memcmp(data->data, target_name, data->data_len) == 0)
	{
		*found = true;
	}

	return false;
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	bool found = false;
	int err;

	if (type != BT_GAP_ADV_TYPE_ADV_IND)
	{
		return;
	}

	bt_data_parse(ad, name_match, &found);

	if (!found)
	{
		return;
	}

	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	LOG_INF("Device found: %s (RSSI %d)", addr_str, rssi);

	if (bt_le_scan_stop())
	{
		return;
	}

	err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT, &conn);

	if (err)
	{
		LOG_ERR("Create connection failed (err %d)", err);
		scan_start();
	}
}

static void scan_start(void)
{
	int err = bt_le_scan_start(BT_LE_SCAN_PASSIVE, device_found);

	if (err)
	{
		LOG_ERR("Scanning failed to start (err %d)", err);
		return;
	}

	LOG_INF("Scanning started");
}

static void connected(struct bt_conn *c, uint8_t err)
{
	if (err)
	{
		LOG_ERR("Connection failed (err %u)", err);
		bt_conn_unref(conn);
		conn = NULL;
		return;
	}

	LOG_INF("Connected");
	local_report.count = 0;

	int ret = bt_conn_set_security(c, BT_SECURITY_L2);

	if (ret)
	{
		LOG_ERR("Set security failed (err %d)", ret);
	}
}

static void security_changed(struct bt_conn *c, bt_security_t level,
			     enum bt_security_err err)
{
	if (err)
	{
		LOG_ERR("Security failed (level %u, err %d)", level, err);
		bt_conn_disconnect(c, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		return;
	}

	LOG_INF("Security changed (level %u)", level);
	k_sem_give(&sem_encrypted);
}

static void cs_done(uint8_t status)
{
	cs_status = status;
	k_sem_give(&sem_cs);
}

static void cs_remote_capabilities(struct bt_conn *c, uint8_t status,
				   struct bt_conn_le_cs_capabilities *params)
{
	cs_done(status);
}

static void cs_config_complete(struct bt_conn *c, uint8_t status,
			       struct bt_conn_le_cs_config *config)
{
	cs_done(status);
}

static void cs_security_enable_complete(struct bt_conn *c, uint8_t status)
{
	cs_done(status);
}

static void cs_procedure_enable_complete(struct bt_conn *c, uint8_t status,
					 struct bt_conn_le_cs_procedure_enable_complete *params)
{
	cs_done(status);
}

static bool cs_step_collect(struct bt_le_cs_subevent_step *step, void *user_data)
{
	if (step->mode != BT_HCI_OP_LE_CS_MAIN_MODE_2 || local_report.count >= RADAR_MAX_CHANNELS)
	{
		return true;
	}

	const struct bt_hci_le_cs_step_data_mode_2 *data = (const void *)step->data;
	struct bt_le_cs_iq_sample iq = bt_le_cs_parse_pct(data->tone_info[0].phase_correction_term);

	local_report.iq[local_report.count].channel = step->channel;
	local_report.iq[local_report.count].i = iq.i;
	local_report.iq[local_report.count].q = iq.q;
	local_report.iq[local_report.count].quality = data->tone_info[0].quality_indicator;
	local_report.count++;

	return true;
}

static void cs_subevent_result(struct bt_conn *c, struct bt_conn_le_cs_subevent_result *result)
{
	if (result->header.procedure_counter != local_report.procedure_counter)
	{
		local_report.procedure_counter = result->header.procedure_counter;
		local_report.count = 0;
	}

	if (result->step_data_buf)
	{
		bt_le_cs_step_data_parse(result->step_data_buf, cs_step_collect, NULL);
	}

	if (result->header.procedure_done_status == BT_CONN_LE_CS_PROCEDURE_ABORTED)
	{
		LOG_WRN("Procedure %u aborted", local_report.procedure_counter);
		local_report.count = 0;
	}
}

static void disconnected(struct bt_conn *c, uint8_t reason)
{
	LOG_INF("Disconnected (reason %u)", reason);
	bt_conn_unref(conn);
	conn = NULL;
}

static void recycled(void)
{
	scan_start();
}

BT_CONN_CB_DEFINE(conn_callbacks) =
{
	.connected = connected,
	.disconnected = disconnected,
	.recycled = recycled,
	.security_changed = security_changed,
	.le_cs_read_remote_capabilities_complete = cs_remote_capabilities,
	.le_cs_config_complete = cs_config_complete,
	.le_cs_security_enable_complete = cs_security_enable_complete,
	.le_cs_procedure_enable_complete = cs_procedure_enable_complete,
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

	scan_start();
	return 0;
}

static int cs_wait(const char *step)
{
	if (k_sem_take(&sem_cs, K_SECONDS(5)))
	{
		LOG_ERR("%s timed out", step);
		return -ETIMEDOUT;
	}

	if (cs_status)
	{
		LOG_ERR("%s failed (status 0x%02x)", step, cs_status);
		return -EIO;
	}

	LOG_INF("%s done", step);
	return 0;
}

static int cs_setup(void)
{
	const struct bt_le_cs_set_default_settings_param settings =
	{
		.enable_initiator_role = true,
		.enable_reflector_role = false,
		.cs_sync_antenna_selection = BT_LE_CS_ANTENNA_SELECTION_OPT_REPETITIVE,
		.max_tx_power = BT_HCI_OP_LE_CS_MAX_MAX_TX_POWER,
	};

	struct bt_le_cs_create_config_params config =
	{
		.id = CS_CONFIG_ID,
		.mode = BT_CONN_LE_CS_MAIN_MODE_2_NO_SUB_MODE,
		.min_main_mode_steps = 2,
		.max_main_mode_steps = 10,
		.main_mode_repetition = 0,
		.mode_0_steps = 3,
		.role = BT_CONN_LE_CS_ROLE_INITIATOR,
		.rtt_type = BT_CONN_LE_CS_RTT_TYPE_AA_ONLY,
		.cs_sync_phy = BT_CONN_LE_CS_SYNC_1M_PHY,
		.channel_map_repetition = 1,
		.channel_selection_type = BT_CONN_LE_CS_CHSEL_TYPE_3B,
	};

	for (uint8_t ch = CS_FIRST_CH; ch < CS_FIRST_CH + CS_N_CH; ch++)
	{
		if (ch >= CS_FORBIDDEN_FIRST_CH && ch <= CS_FORBIDDEN_LAST_CH)
		{
			continue;
		}

		BT_LE_CS_CHANNEL_BIT_SET_VAL(config.channel_map, ch, 1);
	}

	int err = bt_le_cs_set_default_settings(conn, &settings);

	if (err)
	{
		LOG_ERR("CS default settings failed (err %d)", err);
		return err;
	}

	err = bt_le_cs_read_remote_supported_capabilities(conn);

	if (err)
	{
		LOG_ERR("CS read remote capabilities failed (err %d)", err);
		return err;
	}

	err = cs_wait("CS remote capabilities");

	if (err)
	{
		return err;
	}

	err = bt_le_cs_create_config(conn, &config, BT_LE_CS_CREATE_CONFIG_CONTEXT_LOCAL_AND_REMOTE);

	if (err)
	{
		LOG_ERR("CS create config failed (err %d)", err);
		return err;
	}

	err = cs_wait("CS config");

	if (err)
	{
		return err;
	}

	err = bt_le_cs_security_enable(conn);

	if (err)
	{
		LOG_ERR("CS security enable failed (err %d)", err);
		return err;
	}

	err = cs_wait("CS security");

	if (err)
	{
		return err;
	}

	const struct bt_le_cs_set_procedure_parameters_param procedure =
	{
		.config_id = CS_CONFIG_ID,
		.max_procedure_len = 1600,
		.min_procedure_interval = 100,
		.max_procedure_interval = 100,
		.max_procedure_count = 0,
		.min_subevent_len = 50000,
		.max_subevent_len = 50000,
		.tone_antenna_config_selection = BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B1,
		.phy = BT_LE_CS_PROCEDURE_PHY_1M,
		.tx_power_delta = 0x80,
		.preferred_peer_antenna = BT_LE_CS_PROCEDURE_PREFERRED_PEER_ANTENNA_1,
		.snr_control_initiator = BT_LE_CS_SNR_CONTROL_NOT_USED,
		.snr_control_reflector = BT_LE_CS_SNR_CONTROL_NOT_USED,
	};

	err = bt_le_cs_set_procedure_parameters(conn, &procedure);

	if (err)
	{
		LOG_ERR("CS procedure parameters failed (err %d)", err);
		return err;
	}

	const struct bt_le_cs_procedure_enable_param enable =
	{
		.config_id = CS_CONFIG_ID,
		.enable = 1,
	};

	err = bt_le_cs_procedure_enable(conn, &enable);

	if (err)
	{
		LOG_ERR("CS procedure enable failed (err %d)", err);
		return err;
	}

	return cs_wait("CS procedure enable");
}

static uint16_t iq_value_handle;

static void mtu_exchanged(struct bt_conn *c, uint8_t err, struct bt_gatt_exchange_params *params)
{
	cs_done(err);
}

static uint8_t iq_discovered(struct bt_conn *c, const struct bt_gatt_attr *attr,
			     struct bt_gatt_discover_params *params)
{
	if (!attr)
	{
		cs_done(BT_ATT_ERR_ATTRIBUTE_NOT_FOUND);
		return BT_GATT_ITER_STOP;
	}

	const struct bt_gatt_chrc *chrc = attr->user_data;

	iq_value_handle = chrc->value_handle;
	cs_done(0);
	return BT_GATT_ITER_STOP;
}

static uint8_t iq_notified(struct bt_conn *c, struct bt_gatt_subscribe_params *params,
			   const void *data, uint16_t length)
{
	if (!data)
	{
		LOG_INF("IQ unsubscribed");
		return BT_GATT_ITER_STOP;
	}

	const struct radar_iq_report *peer = data;
	size_t header = offsetof(struct radar_iq_report, iq);

	if (length < header || peer->count > RADAR_MAX_CHANNELS ||
	    length < header + peer->count * sizeof(struct radar_iq))
	{
		LOG_ERR("IQ invalid length %u", length);
		return BT_GATT_ITER_CONTINUE;
	}

	if (peer->procedure_counter != local_report.procedure_counter)
	{
		LOG_WRN("IQ procedure mismatch (local %u, peer %u)",
			local_report.procedure_counter, peer->procedure_counter);
		return BT_GATT_ITER_CONTINUE;
	}

	LOG_INF("Procedure %u: local %u, peer %u channels", peer->procedure_counter,
		local_report.count, peer->count);

	distance_process(&local_report, peer);
	return BT_GATT_ITER_CONTINUE;
}

static void iq_subscribed(struct bt_conn *c, uint8_t err, struct bt_gatt_subscribe_params *params)
{
	cs_done(err);
}

static int gatt_setup(void)
{
	static struct bt_gatt_exchange_params mtu_params =
	{
		.func = mtu_exchanged,
	};

	static struct bt_gatt_discover_params discover_params =
	{
		.func = iq_discovered,
		.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE,
		.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE,
		.type = BT_GATT_DISCOVER_CHARACTERISTIC,
	};

	static struct bt_gatt_discover_params ccc_discover_params;

	static struct bt_gatt_subscribe_params subscribe_params =
	{
		.notify = iq_notified,
		.subscribe = iq_subscribed,
		.value = BT_GATT_CCC_NOTIFY,
		.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE,
		.disc_params = &ccc_discover_params,
	};

	int err = bt_gatt_exchange_mtu(conn, &mtu_params);

	if (err)
	{
		LOG_ERR("MTU exchange failed (err %d)", err);
		return err;
	}

	err = cs_wait("MTU exchange");

	if (err)
	{
		return err;
	}

	LOG_INF("MTU %u", bt_gatt_get_mtu(conn));

	discover_params.uuid = RADAR_IQ_UUID;
	err = bt_gatt_discover(conn, &discover_params);

	if (err)
	{
		LOG_ERR("IQ discovery failed (err %d)", err);
		return err;
	}

	err = cs_wait("IQ discovery");

	if (err)
	{
		return err;
	}

	LOG_INF("IQ value handle 0x%04x", iq_value_handle);

	subscribe_params.value_handle = iq_value_handle;
	subscribe_params.ccc_handle = BT_GATT_AUTO_DISCOVER_CCC_HANDLE;
	err = bt_gatt_subscribe(conn, &subscribe_params);

	if (err)
	{
		LOG_ERR("IQ subscribe failed (err %d)", err);
		return err;
	}

	return cs_wait("IQ subscribe");
}

void ble_initiator_run(void)
{
	while (true)
	{
		k_sem_take(&sem_encrypted, K_FOREVER);

		if (gatt_setup() || cs_setup())
		{
			bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		}
	}
}
