/**
 * @file QMI8658.h
 * @brief QMI8658 六轴 IMU 芯片寄存器层驱动。
 *
 * 本文件只负责 QMI8658 的寄存器读写、地址探测、最小初始化和原始数据读取。
 * IIC 总线、延时和板级引脚映射由外部回调提供；这里不绑定具体 MCU 外设、
 * 板名、日志系统或 AHRS 算法。
 */

#ifndef __QMI8658_H__
#define __QMI8658_H__

#include "type_def.h"

#define QMI8658_I2C_ADDR_PRIMARY        0x6BU
#define QMI8658_I2C_ADDR_ALT            0x6AU
#define QMI8658_CHIP_ID_VALUE           0x05U

#define QMI8658_REG_WHO_AM_I            0x00U
#define QMI8658_REG_CTRL1               0x02U
#define QMI8658_REG_CTRL2               0x03U
#define QMI8658_REG_CTRL3               0x04U
#define QMI8658_REG_CTRL5               0x06U
#define QMI8658_REG_CTRL7               0x08U
#define QMI8658_REG_STATUS0             0x2EU
#define QMI8658_REG_TEMP_L              0x33U
#define QMI8658_REG_AX_L                0x35U

#define QMI8658_ACC_ODR_56HZ            0x07U
#define QMI8658_GYRO_ODR_56HZ           0x07U
#define QMI8658_ACC_RANGE_4G            0x10U
#define QMI8658_GYRO_RANGE_256          0x40U

#define QMI8658_CTRL1_INIT              0x40U
#define QMI8658_CTRL2_INIT              (QMI8658_ACC_RANGE_4G | QMI8658_ACC_ODR_56HZ)
#define QMI8658_CTRL3_INIT              (QMI8658_GYRO_RANGE_256 | QMI8658_GYRO_ODR_56HZ)
#define QMI8658_CTRL5_INIT              0x11U
#define QMI8658_CTRL7_INIT              0x03U

#define QMI8658_STATUS0_A_DA            0x01U
#define QMI8658_STATUS0_G_DA            0x02U

#define QMI8658_POWER_UP_DELAY_MS       500U
#define QMI8658_INIT_RETRY_MAX          3U
#define QMI8658_INIT_RETRY_DELAY_MS     200U
#define QMI8658_ID_RETRY_MAX            5U
#define QMI8658_ID_RETRY_DELAY_MS       2U
#define QMI8658_ENABLE_DELAY_MS         30U
#define QMI8658_READY_TIMEOUT_MS        200U
#define QMI8658_VERIFY_RETRY_MAX        2U
#define QMI8658_VERIFY_RETRY_DELAY_MS   2U
#define QMI8658_CONFIG_RETRY_MAX        1U
#define QMI8658_CONFIG_RETRY_DELAY_MS   5U

#define QMI8658_OK                      0
#define QMI8658_ERR_PARAM              -1
#define QMI8658_ERR_BUS                -2
#define QMI8658_ERR_ID                 -3
#define QMI8658_ERR_VERIFY             -4
#define QMI8658_ERR_NOT_READY          -5
#define QMI8658_ERR_DATA               -6

/**
 * @brief 写一个 QMI8658 寄存器。
 * @return 0 表示总线写成功，非 0 表示总线失败。
 */
typedef int8 (*qmi8658_write_reg_fn)(u8 addr, u8 reg, u8 value);

/**
 * @brief 连续读取 QMI8658 寄存器。
 * @return 0 表示总线读成功，非 0 表示总线失败。
 */
typedef int8 (*qmi8658_read_regs_fn)(u8 addr, u8 start_reg, u8 *buf, u8 len);

/** @brief 可选毫秒延时回调，用于上电等待和 ready 轮询。 */
typedef void (*qmi8658_delay_ms_fn)(u16 ms);

typedef struct
{
    qmi8658_write_reg_fn write_reg;
    qmi8658_read_regs_fn read_regs;
    qmi8658_delay_ms_fn delay_ms;
} qmi8658_bus_t;

/**
 * @brief QMI8658 一帧原始数据。
 *
 * 输出为寄存器里的有符号 16 位原始值，不在芯片层做物理量换算。
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
} qmi8658_sample_t;

typedef struct
{
    qmi8658_bus_t bus;
    u8 addr;
    u8 initialized;
    u8 data_ready;
    int8 last_error;
    u8 last_id;
    u8 last_ctrl1;
    u8 last_ctrl2;
    u8 last_ctrl3;
    u8 last_ctrl5;
    u8 last_ctrl7;
    u8 last_status0;
    u8 last_cfg_retry;
    u8 last_cfg_reg;
    u8 last_cfg_write;
    u8 last_cfg_read;
    int8 last_cfg_ret;
} qmi8658_t;

typedef struct
{
    u8 who_am_i;
    u8 ctrl1;
    u8 ctrl2;
    u8 ctrl3;
    u8 ctrl5;
    u8 ctrl7;
    u8 status0;
} qmi8658_diag_regs_t;

/** @brief 绑定总线回调，并重置本地状态。 */
int8 QMI8658_Bind(qmi8658_t *dev, const qmi8658_bus_t *bus);

/** @brief 手动覆盖当前 QMI8658 地址。 */
void QMI8658_SetAddress(qmi8658_t *dev, u8 addr);

/** @brief 返回当前 QMI8658 地址；dev 为空时返回 0。 */
u8 QMI8658_GetAddress(const qmi8658_t *dev);

/** @brief 探测地址、写最小寄存器集并等待 accel/gyro ready。 */
int8 QMI8658_Init(qmi8658_t *dev);

/** @brief 通过已绑定回调写入一个寄存器。 */
int8 QMI8658_WriteReg(qmi8658_t *dev, u8 reg, u8 value);

/** @brief 通过已绑定回调连续读取寄存器。 */
int8 QMI8658_ReadRegs(qmi8658_t *dev, u8 start_reg, u8 *buf, u8 len);

/** @brief 通过已绑定回调读取一个寄存器。 */
int8 QMI8658_ReadReg(qmi8658_t *dev, u8 reg, u8 *value);

/** @brief 读取 WHO_AM_I 寄存器。 */
int8 QMI8658_ReadID(qmi8658_t *dev, u8 *chip_id);

/** @brief 读取 STATUS0，并刷新本地 data_ready 状态。 */
int8 QMI8658_ReadStatus0(qmi8658_t *dev, u8 *status0);

/** @brief 从 AX_L 连续读取 12 字节六轴原始数据；温度另行尝试读取。 */
int8 QMI8658_ReadRawSample(qmi8658_t *dev, qmi8658_sample_t *sample);

/** @brief 读取关键诊断寄存器，供 bring-up 失败排查使用。 */
int8 QMI8658_ReadDiagRegs(qmi8658_t *dev, qmi8658_diag_regs_t *regs);

/** @brief 返回芯片是否已经完成初始化。 */
u8 QMI8658_IsReady(const qmi8658_t *dev);

/** @brief 返回最近一次状态更新记录的数据就绪标志。 */
u8 QMI8658_HasDataReady(const qmi8658_t *dev);

/** @brief 清除本地数据就绪标志。 */
void QMI8658_ClearDataReady(qmi8658_t *dev);

/** @brief 返回最近一次芯片层错误码。 */
int8 QMI8658_GetLastError(const qmi8658_t *dev);

/** @brief 返回最近一次探测/读取到的芯片 ID。 */
u8 QMI8658_GetLastID(const qmi8658_t *dev);

#endif
