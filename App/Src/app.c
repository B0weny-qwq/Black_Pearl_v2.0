#include "app.h"
#include "board_console.h"
#include "board_gps.h"
#include "board_imu.h"
#include "board_lt8920.h"
#include "board_mag.h"
#include "logger.h"

static void app_bring_up_devices(void)
{
	int8 ret;
	u8 verify_reg;
	u16 verify_expected;
	u16 verify_actual;

	LOGI("APP", "Black Pearl v2.0");
	LOGI("APP", "v1.0 移植/解耦重构基线");

	ret = board_imu_init();
	if(ret == BOARD_IMU_OK)
	{
		LOGI("IMU", "QMI8658 初始化成功");
	}
	else
	{
		LOGE("IMU", "QMI8658 初始化失败 rc=%d", ret);
	}

	ret = board_mag_init();
	if(ret == BOARD_MAG_OK)
	{
		LOGI("MAG", "QMC6309 初始化成功");
	}
	else
	{
		LOGE("MAG", "QMC6309 初始化失败 rc=%d", ret);
	}

	ret = board_lt8920_init();
	if(ret == BOARD_LT8920_OK)
	{
		LOGI("RF", "LT8920 初始化成功");
	}
	else
	{
		LOGE("RF", "LT8920 初始化失败 rc=%d", ret);
		if(ret == BOARD_LT8920_ERR_VERIFY)
		{
			board_lt8920_get_verify_failure(&verify_reg, &verify_expected, &verify_actual);
			if(verify_reg != 0xFFU)
			{
				LOGE("RF", "寄存器校验失败 reg=%u actual=0x%04X expect=0x%04X",
					 (u16)verify_reg,
					 verify_actual,
					 verify_expected);
			}
		}
	}
}

void app_init(void)
{
	if(board_console_init() == BOARD_CONSOLE_OK)
	{
		log_init();
	}

	app_bring_up_devices();
	(void)board_gps_init();
}

void app_loop(void)
{
	board_gps_poll();
	(void)board_imu_service();
}
