/**
 * @file ef_iic.h
 * @brief STC32G 硬件 IIC 主机模式的 MCU 抽象层封装。
 *
 * 本接口只暴露“寄存器连续读写”这种外设芯片最常用的能力，底层固定采用
 * STC 官方 `APP_DMA_I2C` 示例里的硬件 IIC + IIC DMA 路径：
 * - 写寄存器：TX DMA 发送 设备写地址 + 寄存器地址 + 数据。
 * - 读寄存器：先用短命令发送 设备写地址 + 寄存器地址 + 设备读地址，再用
 *   RX DMA 接收数据。
 *
 * 设计边界：
 * - App/BoardDevices 不需要包含 `STC32G_I2C.h`、`STC32G_DMA.h` 等官方头文件。
 * - 对外统一传 7-bit 设备地址，例如 QMI8658 常用地址传 `0x6B`，不要传左移后
 *   的 `0xD6/0xD7` 地址字节。
 * - 引脚复用、DMA 标志、I2C 命令字和 xdata 缓冲全部留在 `ef_iic.c` 内部。
 */

#ifndef __EF_IIC_H__
#define __EF_IIC_H__

#include "type_def.h"

#define EF_IIC_OK                 0
#define EF_IIC_ERR_PARAM          -1
#define EF_IIC_ERR_BUSY           -2
#define EF_IIC_ERR_TIMEOUT        -3
#define EF_IIC_ERR_LENGTH         -4
#define EF_IIC_ERR_NACK           -5
#define EF_IIC_ERR_NOT_INIT       -6

#define EF_IIC_OP_NONE            0U
#define EF_IIC_OP_WRITE           1U
#define EF_IIC_OP_READ            2U
#define EF_IIC_OP_RECOVER         3U

#define EF_IIC_STAGE_IDLE         0U
#define EF_IIC_STAGE_PREPARE      1U
#define EF_IIC_STAGE_START        2U
#define EF_IIC_STAGE_DEV_W        3U
#define EF_IIC_STAGE_DEV_W_ACK    4U
#define EF_IIC_STAGE_REG          5U
#define EF_IIC_STAGE_REG_ACK      6U
#define EF_IIC_STAGE_DATA         7U
#define EF_IIC_STAGE_DATA_ACK     8U
#define EF_IIC_STAGE_RESTART      9U
#define EF_IIC_STAGE_DEV_R        10U
#define EF_IIC_STAGE_DEV_R_ACK    11U
#define EF_IIC_STAGE_READ_DATA    12U
#define EF_IIC_STAGE_MASTER_ACK   13U
#define EF_IIC_STAGE_STOP         14U
#define EF_IIC_STAGE_ABORT        15U
#define EF_IIC_STAGE_RECOVER      16U

/* STC32G 硬件 IIC 可切换的四组复用脚，命名顺序跟官方 I2C_SW() 参数保持一致。 */
#define EF_IIC_PIN_P14_P15        0U
#define EF_IIC_PIN_P24_P25        1U
#define EF_IIC_PIN_P76_P77        2U
#define EF_IIC_PIN_P33_P32        3U

/*
 * IIC speed 直接映射 STC `I2C_InitTypeDef.I2C_Speed`：
 * SCL = Fosc / 2 / (speed * 2 + 4)，当前 MAIN_Fosc=24MHz。
 * 若后续修改 `config.h` 里的主频，需要重新计算这些分频值。
 */
#define EF_IIC_SPEED_100K         58U
#define EF_IIC_SPEED_200K         28U
#define EF_IIC_SPEED_400K         13U

/* 超时单位是轮询计数，不是毫秒；不同优化等级和主频下实际时间会变化。 */
#define EF_IIC_DEFAULT_TIMEOUT    60000U

/* 单次寄存器读写的数据长度上限。TX 缓冲内部会额外保留设备地址和寄存器地址。 */
#define EF_IIC_DMA_MAX_LEN        255U

typedef struct
{
    u8 pin_group;      /* EF_IIC_PIN_*，选择硬件 IIC 复用脚。 */
    u8 speed;          /* EF_IIC_SPEED_* 或 STC IIC speed 原始分频值。 */
    u16 timeout;       /* 轮询超时计数，0 表示使用 EF_IIC_DEFAULT_TIMEOUT。 */
} ef_iic_config_t;

typedef struct
{
    u8 op;
    u8 stage;
    int8 ret;
    u8 dev_addr;
    u8 reg_addr;
    u8 msst;
    u8 mscr;
    u8 bus_state_before;
    u8 bus_state_after;
    int8 recover_ret;
} ef_iic_diag_t;

/**
 * 初始化 STC 硬件 IIC 和 IIC DMA。
 *
 * 注意：pin_group 会配置对应引脚为开漏并打开内部上拉。若板上已有外部上拉，
 * 内部上拉仍可作为弱上拉保留；若某块板需要关闭内部上拉，应在 BoardDevices
 * 或 Platform 的私有初始化里处理，不让 App 直接接触 STC 引脚宏。
 *
 * 返回值：
 * - EF_IIC_OK：初始化完成。
 * - EF_IIC_ERR_PARAM：config 为空或 pin_group 非法。
 */
int8 ef_iic_init(const ef_iic_config_t *config);

/**
 * 向 8-bit 寄存器地址写连续数据。
 *
 * dev_addr: 7-bit 设备地址。
 * reg_addr: 起始寄存器地址。
 * buffer/len: 要写入的数据和长度，len 范围 1..EF_IIC_DMA_MAX_LEN。
 *
 * 返回值：
 * - EF_IIC_OK：传输完成。
 * - EF_IIC_ERR_NOT_INIT：尚未调用 ef_iic_init()。
 * - EF_IIC_ERR_PARAM / EF_IIC_ERR_LENGTH：参数非法。
 * - EF_IIC_ERR_BUSY / EF_IIC_ERR_TIMEOUT：总线或 DMA 等待超时。
 * - EF_IIC_ERR_NACK：设备地址、寄存器地址或数据阶段未收到 ACK。
 */
int8 ef_iic_write_regs(u8 dev_addr, u8 reg_addr, const u8 *buffer, u8 len);

/**
 * 从 8-bit 寄存器地址读取连续数据。
 *
 * dev_addr: 7-bit 设备地址。
 * reg_addr: 起始寄存器地址。
 * buffer/len: 接收缓冲和长度，len 范围 1..EF_IIC_DMA_MAX_LEN。
 *
 * 返回值同 ef_iic_write_regs()。读 1 字节时仍然走 DMA，底层按 STC DMA
 * “寄存器值 + 1 = 实际字节数”的规则配置长度。
 */
int8 ef_iic_read_regs(u8 dev_addr, u8 reg_addr, u8 *buffer, u8 len);

/**
 * 对当前硬件 IIC 总线做一次恢复。
 *
 * 典型流程：
 * - 关闭硬件 IIC/DMA
 * - 切回 GPIO 开漏并释放 SDA/SCL
 * - 额外输出若干个 SCL 脉冲，尝试让从机释放 SDA
 * - 发送一次 STOP，再恢复硬件 IIC
 *
 * 返回值：
 * - EF_IIC_OK：恢复后总线空闲，SDA/SCL 已释放
 * - EF_IIC_ERR_NOT_INIT：尚未调用 ef_iic_init()
 * - EF_IIC_ERR_BUSY：恢复后总线仍未释放
 */
int8 ef_iic_bus_recover(void);

/**
 * @brief 获取最近一次 IIC 操作诊断信息。
 * @param diag 输出诊断结构体，不能为 NULL。
 * @return EF_IIC_OK 获取成功；参数为空时返回 EF_IIC_ERR_PARAM。
 */
int8 ef_iic_get_last_diag(ef_iic_diag_t *diag);

/** @brief 查询 STC IIC 主机状态机是否忙；返回 1 表示忙，0 表示空闲。 */
u8 ef_iic_is_busy(void);

#endif
