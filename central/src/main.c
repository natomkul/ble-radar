#include "ble.h"

int main(void)
{
	int err = ble_init();

	if (err)
	{
		return err;
	}

	ble_initiator_run();

	return 0;
}
