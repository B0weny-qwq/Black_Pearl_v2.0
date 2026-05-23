/**
 * @file board_mag.h
 * @brief Black Pearl 板级磁力计接口。
 *
 * 本文件属于 BoardDevices 层，面向 App 提供“板上磁力计”的稳定接口。
 * 具体芯片、IIC 地址和总线配置由板级实现私下处理，不向上暴露。
 */

#ifndef __BOARD_MAG_H__
#define __BOARD_MAG_H__

#include "type_def.h"

#define BOARD_MAG_OK                0
#define BOARD_MAG_ERR_PARAM        -1
#define BOARD_MAG_ERR_DRIVER       -2
#define BOARD_MAG_ERR_NOT_READY    -3
#define BOARD_MAG_ERR_DATA         -4

typedef struct
{
    int16 mag_x_raw;
    int16 mag_y_raw;
    int16 mag_z_raw;
} board_mag_sample_t;

/** @brief 初始化板级磁力计并完成最小 bring-up。 */
int8 board_mag_init(void);

/** @brief 返回板级磁力计是否已经可以读数。 */
u8 board_mag_is_ready(void);

/** @brief 读取一帧原始磁场数据。 */
int8 board_mag_read(board_mag_sample_t *sample);

#endif
