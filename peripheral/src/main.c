#include "ble.h"

int main(void)
{
	int err = ble_init();

	if (err)
	{
		return err;
	}

	return 0;
}
