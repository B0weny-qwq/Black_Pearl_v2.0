#include "app.h"
#include "board_console.h"
#include "board_gps.h"
#include "logger.h"

void app_init(void)
{
	if(board_console_init() == BOARD_CONSOLE_OK)
	{
		log_init();
	}

	(void)board_gps_init();
}

void app_loop(void)
{
	board_gps_poll();
}
