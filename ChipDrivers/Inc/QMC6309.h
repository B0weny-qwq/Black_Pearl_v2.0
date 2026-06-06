#ifndef __QMC6309_H__
#define __QMC6309_H__

/**
 * @file QMC6309.h
 * @brief QMC6309 地磁计芯片寄存器层驱动。
 *
 * 本层只负责 QMC6309 寄存器读写、初始化和原始三轴数据读取。
 * I2C 读写和延时由外部回调提供；这里不绑定板级引脚、I2C 外设、
 * 日志、AHRS 或滤波逻辑。
 */

#include "type_def.h"

/** 旧工程保留的 QMC6309 地址值；7 位/8 位地址格式由外部 I2C 回调决定。 */
#define QMC6309_I2C_ADDR_PRIMARY        0x7CU
#define QMC6309_I2C_ADDR_ALT            0x0CU
#define QMC6309_I2C_ADDR_PRIMARY_7BIT   0x3EU
#define QMC6309_I2C_ADDR_ALT_7BIT       0x06U

/** 芯片上电并正常响应后，0x00 寄存器应读到的 ID。 */
#define QMC6309_CHIP_ID_VALUE           0x90U

/** 本驱动用到的 QMC6309 寄存器地址。 */
#define QMC6309_REG_CHIP_ID             0x00U
#define QMC6309_REG_DATA_X_L            0x01U
#define QMC6309_REG_DATA_X_H            0x02U
#define QMC6309_REG_DATA_Y_L            0x03U
#define QMC6309_REG_DATA_Y_H            0x04U
#define QMC6309_REG_DATA_Z_L            0x05U
#define QMC6309_REG_DATA_Z_H            0x06U
#define QMC6309_REG_STATUS              0x09U
#define QMC6309_REG_CONTROL_1           0x0AU
#define QMC6309_REG_CONTROL_2           0x0BU

/** 从旧 QMC6309 初始化代码迁移来的控制寄存器值。 */
#define QMC6309_CTRL1_STANDBY           0x00U
#define QMC6309_CTRL1_ACTIVE            0x63U
#define QMC6309_CTRL2_INIT              0x00U
#define QMC6309_CTRL2_SOFT_RESET        0x80U
#define QMC6309_STATUS_DRDY             0x01U

/** 芯片初始化和软复位流程使用的默认等待时间。 */
#define QMC6309_POWER_UP_DELAY_MS       1000U
#define QMC6309_RESET_ENTER_DELAY_MS    5U
#define QMC6309_RESET_EXIT_DELAY_MS     10U
#define QMC6309_ENABLE_DELAY_MS         20U
#define QMC6309_DRDY_TIMEOUT_MS         80U
#define QMC6309_DRDY_POLL_DELAY_MS      5U

/** 驱动返回值，0 表示成功，负数表示错误。 */
#define QMC6309_OK                      0
#define QMC6309_ERR_FAIL                -1
#define QMC6309_ERR_PARAM               -2
#define QMC6309_ERR_BUS                 -3
#define QMC6309_ERR_ID                  -4
#define QMC6309_ERR_DATA                -5
#define QMC6309_ERR_NOT_READY           -6

/**
 * @brief 写入一个 QMC6309 寄存器。
 * @param ctx    外部 I2C 上下文。
 * @param addr   当前设备地址。
 * @param reg    寄存器地址。
 * @param value  待写入字节。
 * @return 0 表示总线写成功，非 0 表示总线失败。
 */
typedef int8 (*qmc6309_write_reg_fn)(u8 addr, u8 reg, u8 value);

/**
 * @brief 连续读取 QMC6309 寄存器。
 * @param ctx        外部 I2C 上下文。
 * @param addr       当前设备地址。
 * @param start_reg  起始寄存器地址。
 * @param buf        读出缓冲区。
 * @param len        读取字节数。
 * @return 0 表示总线读成功，非 0 表示总线失败。
 */
typedef int8 (*qmc6309_read_regs_fn)(u8 addr, u8 start_reg, u8 *buf, u8 len);

/**
 * @brief 可选阻塞延时回调，用于初始化、复位和轮询等待。
 * @param ctx  外部 I2C 上下文。
 * @param ms   延时时间，单位 ms。
 */
typedef void (*qmc6309_delay_ms_fn)(u16 ms);

/**
 * @brief QMC6309 芯片层需要的外部回调。
 *
 * write_reg 和 read_regs 必填。delay_ms 可为空；为空时驱动仍然执行寄存器
 * 读写，但上电、复位和使能步骤之间不会主动等待。
 */
typedef struct
{
    qmc6309_write_reg_fn write_reg;
    qmc6309_read_regs_fn read_regs;
    qmc6309_delay_ms_fn delay_ms;
} qmc6309_bus_t;

/**
 * @brief QMC6309 驱动实例。
 *
 * 只保存回调绑定和当前选中的设备地址，可由外部板级代码静态分配。
 */
typedef struct
{
    qmc6309_bus_t bus;
    u8 addr;
    u8 initialized;
    u8 data_ready;
    int8 last_error;
    u8 last_id;
    u8 last_status;
    u8 last_control_1;
    u8 last_control_2;
} qmc6309_t;

/** 用于 bring-up 和寄存器检查的关键寄存器快照。 */
typedef struct
{
    u8 chip_id;
    u8 status;
    u8 control_1;
    u8 control_2;
} qmc6309_regs_t;

/**
 * @brief 绑定总线回调，并设置默认 QMC6309 地址。
 * @return QMC6309_OK 或 QMC6309_ERR_PARAM。
 */
int8 QMC6309_Bind(qmc6309_t *dev, const qmc6309_bus_t *bus);

/** @brief 在底层寄存器访问前手动覆盖当前 QMC6309 地址。 */
void QMC6309_SetAddress(qmc6309_t *dev, u8 addr);

/** @brief 返回当前 QMC6309 地址；dev 为空时返回 0。 */
u8 QMC6309_GetAddress(const qmc6309_t *dev);

/**
 * @brief 探测地址、等待芯片 ID、软复位，并进入 active 模式。
 *
 * 初始化顺序为 CTRL1 standby、CTRL2 init、CTRL1 active。
 * 本函数不做上层校准、滤波或航向估计。
 */
int8 QMC6309_Init(qmc6309_t *dev);

/**
 * @brief 探测主地址和备选地址，并保留能读到正确 ID 的地址。
 * @return 读到 QMC6309_CHIP_ID_VALUE 时返回 QMC6309_OK。
 */
int8 QMC6309_SelectAddress(qmc6309_t *dev);

/** @brief 通过 CONTROL_2 执行软复位序列。 */
int8 QMC6309_Reset(qmc6309_t *dev);

/** @brief 轮询芯片 ID，直到匹配或 timeout_ms 超时。 */
int8 QMC6309_WaitReady(qmc6309_t *dev, u16 timeout_ms);

/** @brief 通过已绑定的总线回调写入一个寄存器。 */
int8 QMC6309_WriteReg(qmc6309_t *dev, u8 reg, u8 value);

/** @brief 通过已绑定的总线回调读取一个寄存器。 */
int8 QMC6309_ReadReg(qmc6309_t *dev, u8 reg, u8 *value);

/** @brief 从 start_reg 开始连续读取寄存器。 */
int8 QMC6309_ReadRegs(qmc6309_t *dev, u8 start_reg, u8 *buf, u8 len);

/** @brief 读取芯片 ID 寄存器。 */
int8 QMC6309_ReadID(qmc6309_t *dev, u8 *chip_id);

int8 QMC6309_ReadStatus(qmc6309_t *dev, u8 *status);

/**
 * @brief 读取原始 X/Y/Z 地磁数据。
 *
 * 输出为寄存器里的小端有符号 16 位原始值。
 * 本函数不做校准、轴映射、量纲换算或滤波。
 */
int8 QMC6309_ReadXYZ(qmc6309_t *dev, int16 *x, int16 *y, int16 *z);

/** @brief 直接写 CONTROL_2，用于外部选择输出速率等配置。 */
int8 QMC6309_SetODR(qmc6309_t *dev, u8 odr);

/** @brief 读取关键寄存器，供底层诊断使用。 */
int8 QMC6309_ReadDiagRegs(qmc6309_t *dev, qmc6309_regs_t *regs);

u8 QMC6309_IsReady(const qmc6309_t *dev);
u8 QMC6309_HasDataReady(const qmc6309_t *dev);
int8 QMC6309_GetLastError(const qmc6309_t *dev);

#endif
