/**
 * @file board_imu.h
 * @brief Black Pearl 板级 IMU 接口占位。
 *
 * 本文件属于 BoardDevices 层，面向 App 提供“板上 IMU”的稳定接口。
 * 当前只为 QMI8658 移植预留入口，还没有把 IIC 总线和芯片寄存器驱动接进来；
 * 因此这里不会暴露 IIC API、IIC 地址、STC 引脚宏或 QMI8658 寄存器定义。
 *
 * 后续接入顺序建议保持：
 * App -> board_imu -> ChipDrivers/QMI8658 -> McuAbstraction/ef_iic -> STC 官方驱动。
 */

#ifndef __BOARD_IMU_H__
#define __BOARD_IMU_H__

#include "type_def.h"

#define BOARD_IMU_MODEL_QMI8658        1U

/* BoardDevices 层返回值，负数表示错误，便于和 Drivers 层错误码区分。 */
#define BOARD_IMU_OK                   0
#define BOARD_IMU_ERR_PARAM            -1
#define BOARD_IMU_ERR_NOT_BOUND        -2
#define BOARD_IMU_ERR_NOT_READY        -3
#define BOARD_IMU_ERR_DATA             -4

/* 简单状态机：未绑定 -> 初始化中 -> 可读取；错误态留给后续芯片驱动接入。 */
#define BOARD_IMU_STATE_NOT_BOUND      0U
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

/**
 * @brief 初始化板级 IMU。
 *
 * 当前实现尚未绑定 QMI8658 传输层，因此会返回 BOARD_IMU_ERR_NOT_BOUND。
 * 后续接入芯片驱动后，此函数负责配置板级 IIC、初始化 QMI8658 并更新状态机。
 */
int8 board_imu_init(void);

/**
 * @brief 板级 IMU 周期服务函数。
 *
 * 当前占位实现只检查状态；后续可在这里处理 data-ready 标志、错误恢复或低速轮询。
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
 * @return BOARD_IMU_ERR_NOT_BOUND 当前还未接入底层 QMI8658。
 * @return BOARD_IMU_ERR_NOT_READY 已绑定但尚未进入 READY。
 * @return BOARD_IMU_ERR_DATA 底层读取或数据校验失败。
 */
int8 board_imu_read(board_imu_sample_t *sample);

/** @brief 获取当前板级 IMU 状态机状态。 */
u8 board_imu_get_state(void);

#endif
