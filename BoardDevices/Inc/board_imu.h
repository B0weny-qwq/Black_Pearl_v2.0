/**
 * @file board_imu.h
 * @brief Black Pearl 板级 IMU 接口。
 *
 * 本文件属于 BoardDevices 层，面向 App 提供“板上 IMU”的稳定接口。
 * 当前板级实现绑定 QMI8658 + 传感器 IIC 总线，并对上提供稳定的初始化、
 * 就绪判断和原始采样接口。这里不会暴露 IIC API、IIC 地址、STC 引脚宏或
 * QMI8658 寄存器定义。
 *
 * 依赖顺序保持：
 * App -> board_imu -> ChipDrivers/QMI8658 -> McuAbstraction/ef_iic -> STC 官方驱动。
 */

#ifndef __BOARD_IMU_H__
#define __BOARD_IMU_H__

#include "type_def.h"

#define BOARD_IMU_MODEL_QMI8658        1U

/* BoardDevices 层返回值，负数表示错误，便于和 Drivers 层错误码区分。 */
#define BOARD_IMU_OK                   0
#define BOARD_IMU_ERR_PARAM            -1
#define BOARD_IMU_ERR_DRIVER           -2
#define BOARD_IMU_ERR_NOT_READY        -3
#define BOARD_IMU_ERR_DATA             -4

#define BOARD_IMU_STATE_IDLE           0U
#define BOARD_IMU_STATE_INIT           1U
#define BOARD_IMU_STATE_READY          2U
#define BOARD_IMU_STATE_ERROR          3U

/*
 * 默认量程换算占位：
 * - acc: 8192 LSB/g 通常对应 +/-4g。
 * - gyro: 128 LSB/dps 通常对应 +/-256 dps。
 * 后续真正配置 QMI8658 量程时，需要同步确认这些比例常量。
 */
#define BOARD_IMU_ACC_LSB_PER_G        8192L
#define BOARD_IMU_GYRO_LSB_PER_DPS     128L

/**
 * @brief IMU 原始采样值。
 *
 * BoardDevices 层先返回原始值，避免在 8-bit MCU 热路径里强制做浮点换算。
 * App/Components 可以按 `BOARD_IMU_*_LSB_*` 常量转换到物理单位。
 */
typedef struct
{
    int16 acc_x_raw;
    int16 acc_y_raw;
    int16 acc_z_raw;
    int16 gyro_x_raw;
    int16 gyro_y_raw;
    int16 gyro_z_raw;
    int16 temp_raw;
    u8 has_temp;
} board_imu_sample_t;

typedef struct
{
    int8 chip_error;
    u8 i2c_addr;
    u8 who_am_i;
    u8 ctrl1;
    u8 ctrl2;
    u8 ctrl3;
    u8 ctrl5;
    u8 ctrl7;
    u8 status0;
    u8 cfg_retry;
    u8 cfg_reg;
    u8 cfg_write;
    u8 cfg_read;
    int8 cfg_ret;
    u8 i2c_op;
    u8 i2c_stage;
    int8 i2c_ret;
    u8 i2c_state_before;
    u8 i2c_state_after;
    int8 i2c_recover_ret;
    u8 i2c_msst;
    u8 i2c_mscr;
} board_imu_diag_t;

/**
 * @brief 初始化板级 IMU。
 *
 * 本函数负责初始化板级传感器 IIC、绑定 QMI8658 芯片层并完成最小 bring-up。
 */
int8 board_imu_init(void);

/**
 * @brief 板级 IMU 周期服务函数。
 *
 * 当前实现会刷新一次 data-ready 标志。后续可在这里加入错误恢复或中断事件消费。
 */
int8 board_imu_service(void);

/** @brief 返回 IMU 是否已经初始化完成并可读取。 */
u8 board_imu_is_ready(void);

/** @brief 返回是否存在新的 IMU 数据就绪事件。 */
u8 board_imu_has_data_ready(void);

/** @brief 清除数据就绪标志，通常在 App 消费一次采样后调用。 */
void board_imu_clear_data_ready(void);

/**
 * @brief 读取一帧 IMU 原始数据。
 *
 * @param sample 输出缓冲，不能为 NULL。
 * @return BOARD_IMU_OK 读取成功。
 * @return BOARD_IMU_ERR_PARAM sample 为空。
 * @return BOARD_IMU_ERR_DRIVER 板级总线或芯片层初始化失败。
 * @return BOARD_IMU_ERR_NOT_READY 已绑定但尚未进入 READY。
 * @return BOARD_IMU_ERR_DATA 底层读取或数据校验失败。
 */
int8 board_imu_read(board_imu_sample_t *sample);

/** @brief 获取当前板级 IMU 状态机状态。 */
u8 board_imu_get_state(void);

/** @brief 获取最近一次 IMU 初始化/访问的诊断信息。 */
int8 board_imu_get_diag(board_imu_diag_t *diag);

#endif
