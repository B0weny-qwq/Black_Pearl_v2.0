#include "app.h"
#include "platform.h"
#include "platform_scheduler.h"

void main(void)
{
	platform_init();
	app_init();

	while(1)
	{
		platform_scheduler_run();
		app_loop();
	}
}
