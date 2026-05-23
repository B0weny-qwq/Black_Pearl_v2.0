/**
 * @file    QMI8658.h
 * @brief   QMI8658 六轴 IMU 驱动接口。
 * @author  boweny
 * @date    2026-05-07
 * @version v1.1
 *
 * @details
 * 提供 QMI8658 的寄存器定义、初始化状态机、原始数据读取、ready 查询、
 * 运行期重初始化和调试接口。当前根目录工程中 IMU 处于启用状态，
 * 并通过 `QMI8658_Service()` + `MainLoop.c/IMU_AhrsPoll()` 进入
 * `AHRS + HeadingEstimator` 主链。
 *
 * @note
 * 当前默认运行档由 `User/FeatureSwitch.h` 控制：
 * - `ENABLE_IMU_MODULE=1`
 * - `ENABLE_IMU_AHRS_POLL=1`
 * - `ENABLE_IMU_BASIC_POLL=0`
 * - `QMI8658_INIT_NONBLOCKING=1`
 * - `QMI8658_READY_MODE_STATUS0=1`
 *
 * @see     Code_boweny/Device/QMI8658/QMI8658.c
 * @see     Code_boweny/Device/QMI8658/QMI8658_port.h
 */

#ifndef __QMI8658_H__
#define __QMI8658_H__

#include "..\..\..\User\Config.h"
#include "..\..\Function\Log\Log.h"

#define QMI8658_REG_WHO_AM_I        0x00  /**< 芯片 ID 寄存器。 */
#define QMI8658_REG_REVISION_ID     0x01  /**< 版本寄存器。 */
#define QMI8658_REG_CTRL1           0x02  /**< 控制寄存器 1。 */
#define QMI8658_REG_CTRL2           0x03  /**< 加速度配置寄存器。 */
#define QMI8658_REG_CTRL3           0x04  /**< 陀螺仪配置寄存器。 */
#define QMI8658_REG_CTRL4           0x05  /**< 预留控制寄存器。 */
#define QMI8658_REG_CTRL5           0x06  /**< 滤波配置寄存器。 */
#define QMI8658_REG_CTRL6           0x07  /**< 数据路径配置寄存器。 */
#define QMI8658_REG_CTRL7           0x08  /**< 传感器总开关寄存器。 */
#define QMI8658_REG_CTRL8           0x09  /**< FIFO/中断控制寄存器。 */
#define QMI8658_REG_CTRL9           0x0A  /**< 命令寄存器。 */
#define QMI8658_REG_FIFO_WTM        0x13  /**< FIFO 水位寄存器。 */
#define QMI8658_REG_FIFO_CTRL       0x14  /**< FIFO 控制寄存器。 */
#define QMI8658_REG_FIFO_STATUS     0x16  /**< FIFO 状态寄存器。 */
#define QMI8658_REG_STATUSINT       0x2D  /**< 中断状态寄存器。 */
#define QMI8658_REG_STATUS0         0x2E  /**< 数据就绪状态寄存器。 */
#define QMI8658_REG_TIMESTAMP_L     0x30  /**< 时间戳低字节。 */
#define QMI8658_REG_TIMESTAMP_M     0x31  /**< 时间戳中字节。 */
#define QMI8658_REG_TIMESTAMP_H     0x32  /**< 时间戳高字节。 */
#define QMI8658_REG_TEMP_L          0x33  /**< 温度低字节。 */
#define QMI8658_REG_TEMP_H          0x34  /**< 温度高字节。 */
#define QMI8658_REG_AX_L            0x35  /**< 加速度 X 低字节。 */
#define QMI8658_REG_AX_H            0x36  /**< 加速度 X 高字节。 */
#define QMI8658_REG_AY_L            0x37  /**< 加速度 Y 低字节。 */
#define QMI8658_REG_AY_H            0x38  /**< 加速度 Y 高字节。 */
#define QMI8658_REG_AZ_L            0x39  /**< 加速度 Z 低字节。 */
#define QMI8658_REG_AZ_H            0x3A  /**< 加速度 Z 高字节。 */
#define QMI8658_REG_GX_L            0x3B  /**< 陀螺仪 X 低字节。 */
#define QMI8658_REG_GX_H            0x3C  /**< 陀螺仪 X 高字节。 */
#define QMI8658_REG_GY_L            0x3D  /**< 陀螺仪 Y 低字节。 */
#define QMI8658_REG_GY_H            0x3E  /**< 陀螺仪 Y 高字节。 */
#define QMI8658_REG_GZ_L            0x3F  /**< 陀螺仪 Z 低字节。 */
#define QMI8658_REG_GZ_H            0x40  /**< 陀螺仪 Z 高字节。 */
#define QMI8658_REG_RESET_STATE     0x4D  /**< 复位后可短暂观测 0x80，后续可能被其他操作覆盖。 */
#define QMI8658_REG_RESET           0x60  /**< 软复位寄存器。 */

#define QMI8658_I2C_ADDR_PRIMARY    0x6B  /**< 主 I2C 地址。 */
#define QMI8658_I2C_ADDR_ALT        0x6A  /**< 备选 I2C 地址。 */
#define QMI8658_I2C_WRITE(addr)     ((addr) << 1)         /**< 生成写地址字节。 */
#define QMI8658_I2C_READ(addr)      (((addr) << 1) | 0x01) /**< 生成读地址字节。 */

#define QMI8658_ACC_ODR_3HZ         0x0F
#define QMI8658_ACC_ODR_11HZ        0x0E
#define QMI8658_ACC_ODR_21HZ        0x0D
#define QMI8658_ACC_ODR_28HZ        0x08  /**< 6DOF典型输出约 28.025Hz。 */
#define QMI8658_ACC_ODR_56HZ        0x07  /**< 6DOF典型输出约 56.05Hz。 */
#define QMI8658_ACC_ODR_112HZ       0x06  /**< 6DOF典型输出约 112.1Hz。 */
#define QMI8658_ACC_ODR_224HZ       0x05  /**< 6DOF典型输出约 224.2Hz。 */
#define QMI8658_ACC_ODR_448HZ       0x04  /**< 6DOF典型输出约 448.4Hz。 */
#define QMI8658_ACC_ODR_896HZ       0x03  /**< 6DOF典型输出约 896.8Hz。 */

#define QMI8658_GYRO_ODR_28HZ       0x08  /**< 6DOF典型输出约 28.025Hz。 */
#define QMI8658_GYRO_ODR_56HZ       0x07  /**< 6DOF典型输出约 56.05Hz。 */
#define QMI8658_GYRO_ODR_112HZ      0x06  /**< 6DOF典型输出约 112.1Hz。 */
#define QMI8658_GYRO_ODR_224HZ      0x05  /**< 6DOF典型输出约 224.2Hz。 */
#define QMI8658_GYRO_ODR_448HZ      0x04  /**< 6DOF典型输出约 448.4Hz。 */
#define QMI8658_GYRO_ODR_896HZ      0x03  /**< 6DOF典型输出约 896.8Hz。 */
#define QMI8658_GYRO_ODR_1880HZ     0x02
#define QMI8658_GYRO_ODR_3760HZ     0x01
#define QMI8658_GYRO_ODR_7520HZ     0x00

/* Compatibility aliases kept for existing code paths. */
#define QMI8658_ACC_ODR_29HZ        QMI8658_ACC_ODR_28HZ
#define QMI8658_ACC_ODR_58HZ        QMI8658_ACC_ODR_56HZ
#define QMI8658_ACC_ODR_117HZ       QMI8658_ACC_ODR_112HZ
#define QMI8658_ACC_ODR_235HZ       QMI8658_ACC_ODR_224HZ
#define QMI8658_ACC_ODR_470HZ       QMI8658_ACC_ODR_448HZ
#define QMI8658_ACC_ODR_940HZ       QMI8658_ACC_ODR_896HZ

#define QMI8658_GYRO_ODR_29HZ       QMI8658_GYRO_ODR_28HZ
#define QMI8658_GYRO_ODR_58HZ       QMI8658_GYRO_ODR_56HZ
#define QMI8658_GYRO_ODR_117HZ      QMI8658_GYRO_ODR_112HZ
#define QMI8658_GYRO_ODR_235HZ      QMI8658_GYRO_ODR_224HZ
#define QMI8658_GYRO_ODR_470HZ      QMI8658_GYRO_ODR_448HZ
#define QMI8658_GYRO_ODR_940HZ      QMI8658_GYRO_ODR_896HZ

#define QMI8658_ACC_RANGE_2G        0x00
#define QMI8658_ACC_RANGE_4G        0x10
#define QMI8658_ACC_RANGE_8G        0x20
#define QMI8658_ACC_RANGE_16G       0x30
#define QMI8658_GYRO_RANGE_16       0x00
#define QMI8658_GYRO_RANGE_32       0x10
#define QMI8658_GYRO_RANGE_64       0x20
#define QMI8658_GYRO_RANGE_128      0x30  /**< 物理量程 ±128dps，256LSB/dps。 */
#define QMI8658_GYRO_RANGE_256      0x40  /**< 物理量程 ±256dps，128LSB/dps。 */
#define QMI8658_GYRO_RANGE_512      0x50
#define QMI8658_GYRO_RANGE_1024     0x60
#define QMI8658_GYRO_RANGE_2048     0x70

/* Compatibility aliases kept for existing code paths. */
#define QMI8658_GYRO_RANGE_125      QMI8658_GYRO_RANGE_128
#define QMI8658_GYRO_RANGE_250      QMI8658_GYRO_RANGE_256

#define QMI8658_CTRL1_INIT          0x40  /**< 当前默认 CTRL1 初始化值。 */
#define QMI8658_CTRL2_INIT          0x07  /**< 16:45 诊断固件基线：ACC bring-up 配置。 */
#define QMI8658_CTRL3_INIT          0x07  /**< 16:45 诊断固件基线：GYRO bring-up 配置。 */
#define QMI8658_CTRL5_INIT          0x11  /**< 当前默认 CTRL5 初始化值。 */
#define QMI8658_CTRL6_INIT          0x00  /**< 当前默认 CTRL6 初始化值。 */
#define QMI8658_CTRL7_INIT          0x03  /**< 当前默认 CTRL7 初始化值。 */
#define QMI8658_CTRL8_INIT          0x00  /**< 当前默认 CTRL8 初始化值。 */
#define QMI8658_CTRL9_CMD_ACK       0x00  /**< CTRL9 ACK 命令值。 */
#define QMI8658_CTRL9_CMD_RST_FIFO  0x04  /**< CTRL9 FIFO reset 命令值。 */
#define QMI8658_FIFO_WTM_INIT       0x00  /**< FIFO 水位默认值。 */
#define QMI8658_FIFO_CTRL_BYPASS    0x00  /**< FIFO 旁路模式值。 */

#define QMI8658_CHIP_ID_VALUE       0x05  /**< 期望芯片 ID。 */
#define QMI8658_RESET_STATE_READY   0x80  /**< 仅用于复位后瞬时观测，不作为稳定运行判据。 */

#undef QMI8658_CTRL2_INIT
#undef QMI8658_CTRL3_INIT
#define QMI8658_CTRL2_INIT          (QMI8658_ACC_RANGE_4G | QMI8658_ACC_ODR_56HZ)
#define QMI8658_CTRL3_INIT          (QMI8658_GYRO_RANGE_256 | QMI8658_GYRO_ODR_56HZ)

#ifndef QMI8658_CLEAR_DATAPATH_ENABLE
#define QMI8658_CLEAR_DATAPATH_ENABLE  0
#endif

#ifndef QMI8658_SOFT_RESET_ENABLE
#define QMI8658_SOFT_RESET_ENABLE      0
#endif

#ifndef QMI8658_DIAG_ENABLE
#define QMI8658_DIAG_ENABLE            0
#endif

#ifndef QMI8658_I2C_USE_SOFT
#define QMI8658_I2C_USE_SOFT           0
#endif

#ifndef QMI8658_SOFT_I2C_DELAY_US
#define QMI8658_SOFT_I2C_DELAY_US      2U
#endif

#ifndef QMI8658_READY_MODE_STATUS0
#define QMI8658_READY_MODE_STATUS0     1
#endif

#ifndef QMI8658_READY_MODE_STATUSINT
#define QMI8658_READY_MODE_STATUSINT   0
#endif

#ifndef QMI8658_INIT_NONBLOCKING
#define QMI8658_INIT_NONBLOCKING       1
#endif

#ifndef QMI8658_SOFT_I2C_USE_P14_P15
#define QMI8658_SOFT_I2C_USE_P14_P15   1
#endif

#define QMI8658_RESET_DELAY_MS         500U  /**< 软复位后等待时间，明显大于15ms上限。 */
#define QMI8658_PWR_UP_DELAY_MS        500U  /**< 上电后等待时间。 */
#define QMI8658_INIT_RETRY_MAX         3U    /**< 初始化最大重试次数。 */
#define QMI8658_INIT_RETRY_DELAY_MS    200U  /**< 初始化重试间隔。 */
#define QMI8658_ENABLE_DELAY_MS        30U   /**< 使能后初始等待，后续由ready轮询补足。 */
#define QMI8658_READY_TIMEOUT_MS       200U  /**< ready 轮询超时，覆盖gyro起振时间。 */
#define QMI8658_STATUSINT_AVAIL        0x01  /**< STATUSINT 可用位。 */
#define QMI8658_STATUS0_A_DA           0x01  /**< STATUS0 加速度 ready 位。 */
#define QMI8658_STATUS0_G_DA           0x02  /**< STATUS0 陀螺仪 ready 位。 */
/* STATUS0 bit[7:2] reserved; temperature data follows accel/gyro refresh. */
#define QMI8658_READ_FAIL_REINIT_COUNT 4U    /**< 连续读失败后请求重拉起阈值。 */

#define QMI8658_ACC_IS_ZERO(x, y, z)     (((x) == 0) && ((y) == 0) && ((z) == 0))
#define QMI8658_GYRO_IS_ZERO(x, y, z)    (((x) == 0) && ((y) == 0) && ((z) == 0))
#define QMI8658_DATA_IS_INVALID(x, y, z) (((x) == -1) && ((y) == -1) && ((z) == -1))

/**
 * @brief   QMI8658 初始化与运行状态机状态。
 */
typedef enum
{
    QMI8658_STATE_IDLE = 0,      /**< 空闲态。 */
    QMI8658_STATE_BUS_PREPARE,   /**< 准备总线。 */
    QMI8658_STATE_PWR_WAIT,      /**< 上电等待。 */
    QMI8658_STATE_ID_PROBE,      /**< 探测地址并读 ID。 */
    QMI8658_STATE_QUIESCE,       /**< 关闭传感器输出。 */
    QMI8658_STATE_SOFT_RESET,    /**< 执行软复位。 */
    QMI8658_STATE_RESET_WAIT,    /**< 等待复位完成。 */
    QMI8658_STATE_CLEAR_PATH,    /**< 清理数据路径。 */
    QMI8658_STATE_CONFIG_WRITE,  /**< 写配置寄存器。 */
    QMI8658_STATE_CONFIG_VERIFY, /**< 读回校验配置。 */
    QMI8658_STATE_ENABLE,        /**< 打开传感器输出。 */
    QMI8658_STATE_READY_WAIT,    /**< 等待 ready。 */
    QMI8658_STATE_READY,         /**< 已就绪。 */
    QMI8658_STATE_FAILED         /**< 初始化失败。 */
} QMI8658_State_t;

extern u8 QMI8658_I2C_Addr;  /**< 当前生效的 I2C 地址。 */

/**
 * @brief   执行一次阻塞式 IMU 初始化。
 * @return  SUCCESS=成功，其他值表示地址探测、寄存器配置或 ready 等待失败。
 */
s8 QMI8658_Init(void);

/**
 * @brief   推进一次非阻塞初始化/维护状态机。
 * @return  SUCCESS=本轮推进成功，其他值表示状态机遇到错误。
 */
s8 QMI8658_Service(void);

/**
 * @brief   请求重新初始化 IMU。
 * @return  无。
 */
void QMI8658_RequestReinit(void);

/**
 * @brief   查询 IMU 是否已经 ready。
 * @return  1=ready，0=未 ready。
 */
u8 QMI8658_IsReady(void);

/**
 * @brief   读取当前状态机状态。
 * @return  当前 `QMI8658_State_t` 状态值。
 */
QMI8658_State_t QMI8658_GetState(void);

/**
 * @brief   查询是否有新的 IMU 数据就绪标志。
 * @return  1=有新数据，0=无新数据。
 */
u8 QMI8658_HasDataReady(void);

/**
 * @brief   清除内部数据就绪标志。
 * @return  无。
 */
void QMI8658_ClearDataReady(void);

/**
 * @brief   轮询芯片数据就绪寄存器。
 * @return  1=检测到 ready，0=未 ready。
 */
u8 QMI8658_PollDataReady(void);

/**
 * @brief   读取芯片 ID。
 * @return  读到的 `WHO_AM_I` 字节。
 */
u8 QMI8658_ReadID(void);

/**
 * @brief      读取加速度三轴原始值。
 * @param[out] x  X 轴输出指针。
 * @param[out] y  Y 轴输出指针。
 * @param[out] z  Z 轴输出指针。
 * @return     SUCCESS=成功，其他值表示读数失败或数据无效。
 */
s8 QMI8658_ReadAcc(int16 *x, int16 *y, int16 *z);

/**
 * @brief      读取陀螺仪三轴原始值。
 * @param[out] x  X 轴输出指针。
 * @param[out] y  Y 轴输出指针。
 * @param[out] z  Z 轴输出指针。
 * @return     SUCCESS=成功，其他值表示读数失败或数据无效。
 */
s8 QMI8658_ReadGyro(int16 *x, int16 *y, int16 *z);

/**
 * @brief      读取经软件低通后的陀螺仪三轴数据。
 * @param[out] x  X 轴输出指针。
 * @param[out] y  Y 轴输出指针。
 * @param[out] z  Z 轴输出指针。
 * @return     SUCCESS=成功，其他值表示读数失败或滤波输入无效。
 */
s8 QMI8658_ReadGyroFiltered(int16 *x, int16 *y, int16 *z);

/**
 * @brief      读取温度原始值。
 * @param[out] temp  温度输出指针。
 * @return     SUCCESS=成功，其他值表示读数失败。
 */
s8 QMI8658_ReadTemp(int16 *temp);

/**
 * @brief      连续读取加速度和陀螺仪六轴数据。
 * @param[out] ax  加速度 X 输出指针。
 * @param[out] ay  加速度 Y 输出指针。
 * @param[out] az  加速度 Z 输出指针。
 * @param[out] gx  陀螺仪 X 输出指针。
 * @param[out] gy  陀螺仪 Y 输出指针。
 * @param[out] gz  陀螺仪 Z 输出指针。
 * @return     SUCCESS=成功，其他值表示读数失败或数据无效。
 */
s8 QMI8658_ReadAll(int16 *ax, int16 *ay, int16 *az,
                   int16 *gx, int16 *gy, int16 *gz);

/**
 * @brief      阻塞等待加速度 ready。
 * @param[in]  timeout_ms  超时时间，单位 ms。
 * @return     SUCCESS=成功，其他值表示超时或读状态失败。
 */
s8 QMI8658_Wait_AccReady(u16 timeout_ms);

/**
 * @brief      阻塞等待陀螺仪 ready。
 * @param[in]  timeout_ms  超时时间，单位 ms。
 * @return     SUCCESS=成功，其他值表示超时或读状态失败。
 */
s8 QMI8658_Wait_GyroReady(u16 timeout_ms);

/**
 * @brief   执行 I2C 总线恢复。
 * @return  无。
 */
void QMI8658_BusRecover(void);

/**
 * @brief   使能加速度和陀螺仪输出。
 * @return  SUCCESS=成功，其他值表示寄存器写入失败。
 */
s8 QMI8658_Enable(void);

/**
 * @brief   关闭加速度和陀螺仪输出。
 * @return  SUCCESS=成功，其他值表示寄存器写入失败。
 */
s8 QMI8658_Disable(void);

/**
 * @brief   打印关键原始寄存器窗口。
 * @return  无。
 *
 * @note
 * 仅用于 bring-up 排障日志。
 */
void QMI8658_DumpRawRegs(void);

/**
 * @brief   获取最近一次 I2C 错误码。
 * @return  最近一次端口层返回的错误码。
 */
u8 QMI8658_GetLastI2cError(void);

/**
 * @brief   获取最近一次 I2C 错误码名称。
 * @return  指向静态字符串的指针。
 */
char *QMI8658_GetLastI2cErrorName(void);

#endif
