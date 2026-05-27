#include "app.h"
#include "platform.h"
#include "platform_scheduler.h"

/* 固件入口只负责平台初始化和 App 调度；业务逻辑不要写在 main.c。 */
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
