#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/logging/log.h>

#include "ble.h"

LOG_MODULE_REGISTER(ble, LOG_LEVEL_INF);

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

static void connected(struct bt_conn *conn, uint8_t err)
{
	LOG_INF("Connected (err %u)", err);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Disconnected (reason %u)", reason);
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
