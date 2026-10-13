#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/logging/log.h>

#include "ble.h"

LOG_MODULE_REGISTER(ble, LOG_LEVEL_INF);

static const char target_name[] = "peripheral";

static struct bt_conn *conn;

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
