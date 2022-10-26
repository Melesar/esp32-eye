#include "app/app.h"

void app_main(void)
{
	if (ST_SUCCESS != app_init()) {
		return;
	}

	app_run();
}
