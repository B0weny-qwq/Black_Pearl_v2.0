/**
 * @file board_power.h
 * @brief 板级电池电压采样接口。
 *
 * App 层通过本接口读取电池 ADC 原始值、毫伏值和 0..4 电量等级，
 * 不直接包含 ADC/GPIO 驱动头文件，也不关心采样通道和分压配置。
 */

#ifndef __BOARD_POWER_H__
#define __BOARD_POWER_H__

#include "type_def.h"

#define BOARD_POWER_OK             0
#define BOARD_POWER_ERR_PARAM     -1
#define BOARD_POWER_ERR_NOT_READY -2
#define BOARD_POWER_ERR_SAMPLE    -3

#define BOARD_POWER_LEVEL_0        0U
#define BOARD_POWER_LEVEL_1        1U
#define BOARD_POWER_LEVEL_2        2U
#define BOARD_POWER_LEVEL_3        3U
#define BOARD_POWER_LEVEL_4        4U

#define BOARD_POWER_BAT_SCALE_NUM  1UL
#define BOARD_POWER_BAT_SCALE_DEN  1UL
#define BOARD_POWER_BAT_MV_UNCALIBRATED 1U

typedef struct
{
    u16 raw;
    u16 adc_mv;
    u32 bat_mv;
    u8 level;
    u8 valid;
} board_power_sample_t;

/**
 * @brief 初始化电池采样 ADC 通道。
 * @return BOARD_POWER_OK 初始化成功。
 */
int8 board_power_init(void);

/**
 * @brief 读取一次电池采样。
 * @param sample 输出采样结果，不能为 NULL。
 * @return BOARD_POWER_OK 采样成功。
 * @return BOARD_POWER_ERR_NOT_READY 尚未初始化。
 */
int8 board_power_read(board_power_sample_t *sample);

/**
 * @brief 返回最近一次有效电量等级。
 * @return 0..4 电量等级。
 */
u8 board_power_get_level(void);

/**
 * @brief 查询电池采样模块是否已初始化。
 * @return 1 已初始化，0 未初始化。
 */
u8 board_power_is_ready(void);

#endif
