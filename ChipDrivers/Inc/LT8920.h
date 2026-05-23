#ifndef __LT8920_H__
#define __LT8920_H__

/**
 * @file LT8920.h
 * @brief LT8920 2.4GHz 射频收发芯片寄存器层驱动。
 *
 * 本文件只实现芯片层：寄存器、FIFO、信道、同步字、状态、RSSI 和模式切换。
 * SPI 传输和 CS 由外部回调提供；GPIO 映射、RST 控制、射频前端时序、
 * 无线协议解析和收发队列都不放在这里。
 */

#include "type_def.h"

/** 旧无线代码使用的 LT8920 单包载荷上限；FIFO 格式为 1 字节长度 + 载荷。 */
#define LT8920_MAX_PAYLOAD_LEN          60U

/** 旧工程默认射频参数。 */
#define LT8920_DEFAULT_CHANNEL          0x30U
#define LT8920_DEFAULT_SYNC_WORD        0x03800380UL

/** 本驱动用到的 LT8920 寄存器地址。 */
#define LT8920_REG_CHANNEL_MODE         7U
#define LT8920_REG_MODE_CONFIG          8U
#define LT8920_REG_POWER                9U
#define LT8920_REG_SYNCWORD_0           36U
#define LT8920_REG_SYNCWORD_1           37U
#define LT8920_REG_SYNCWORD_2           38U
#define LT8920_REG_SYNCWORD_3           39U
#define LT8920_REG_RSSI                 6U
#define LT8920_REG_STATUS               48U
#define LT8920_REG_FIFO                 50U
#define LT8920_REG_FIFO_CTRL            52U

/** 写入寄存器 7 的模式位，会和 7 位信道号按位或。 */
#define LT8920_MODE_IDLE                0x0000U
#define LT8920_MODE_RX                  0x0080U
#define LT8920_MODE_TX                  0x0100U

/** 状态寄存器 48 的标志位。 */
#define LT8920_STATUS_CRC_ERROR         0x8000U
#define LT8920_STATUS_SYNC_RECV         0x0080U
#define LT8920_STATUS_PKT_FLAG          0x0040U
#define LT8920_STATUS_FIFO_FLAG         0x0020U

/** 清 FIFO 时写入寄存器 52 的值，对齐旧工程。 */
#define LT8920_FIFO_CLEAR_VALUE         0x8080U

/** 驱动返回值，0 表示成功，负数表示错误。 */
#define LT8920_OK                       0
#define LT8920_ERR_FAIL                 -1
#define LT8920_ERR_PARAM                -2
#define LT8920_ERR_STATE                -3
#define LT8920_ERR_EMPTY                -4
#define LT8920_ERR_OVERFLOW             -5
#define LT8920_ERR_VERIFY               -6
#define LT8920_ERR_CRC                  -7

/**
 * @brief 传输一个 SPI 字节，并返回同时读到的字节。
 * @param ctx    外部 SPI 上下文。
 * @param value  MOSI 发出的字节。
 * @return MISO 读到的字节。
 */
typedef u8 (*lt8920_spi_transfer_fn)(void *ctx, u8 value);

/**
 * @brief 控制 LT8920 片选脚。
 * @param ctx    外部 SPI/GPIO 上下文。
 * @param level  0 选中芯片，1 释放芯片。
 */
typedef void (*lt8920_set_cs_fn)(void *ctx, u8 level);

/** @brief 可选 us 延时回调，用于强制发送的旧时序等待。 */
typedef void (*lt8920_delay_us_fn)(void *ctx, u16 us);

/** @brief 预留的可选 ms 延时回调，方便外部板级代码复用上下文。 */
typedef void (*lt8920_delay_ms_fn)(void *ctx, u16 ms);

/**
 * @brief LT8920 芯片层需要的外部回调。
 *
 * spi_transfer 和 set_cs 必填。delay_us 和 delay_ms 可为空。
 * SPI 模式、速率和引脚映射由外部负责。
 */
typedef struct
{
    void *ctx;
    lt8920_spi_transfer_fn spi_transfer;
    lt8920_set_cs_fn set_cs;
    lt8920_delay_us_fn delay_us;
    lt8920_delay_ms_fn delay_ms;
} lt8920_bus_t;

/**
 * @brief LT8920 驱动实例。
 *
 * 只保存芯片层状态：当前信道、同步字、初始化标记，以及寄存器校验和
 * TX FIFO 计数诊断信息。
 */
typedef struct
{
    lt8920_bus_t bus;
    u8 initialized;
    u8 channel;
    u32 sync_word;
    u8 verify_fail_reg;
    u16 verify_fail_expected;
    u16 verify_fail_actual;
    u8 last_tx_fifo_count;
} lt8920_t;

/** LT8920 默认寄存器表的一项。 */
typedef struct
{
    u8 reg;
    u16 value;       /**< 加载默认配置时写入的值。 */
    u16 verify_mask; /**< 回读校验的掩码；0 表示跳过校验。 */
} lt8920_reg_setting_t;

/**
 * @brief 绑定 SPI/CS 回调，并重置本地芯片层状态。
 * @return LT8920_OK 或 LT8920_ERR_PARAM。
 */
int8 LT8920_Bind(lt8920_t *dev, const lt8920_bus_t *bus);

/**
 * @brief 加载并校验默认寄存器表，设置同步字和信道，清 FIFO 后进入 idle。
 * @param channel    7 位射频信道，范围 0x00-0x7F。
 * @param sync_word  32 位同步字，会拆到寄存器 36 和 39。
 */
int8 LT8920_Init(lt8920_t *dev, u8 channel, u32 sync_word);

/** @brief 写入内置默认寄存器表。 */
int8 LT8920_LoadDefaultProfile(lt8920_t *dev);

/** @brief 按每项 verify_mask 回读校验内置寄存器表。 */
int8 LT8920_VerifyDefaultProfile(lt8920_t *dev);

/** @brief 写入一个 16 位 LT8920 寄存器。 */
int8 LT8920_WriteReg(lt8920_t *dev, u8 reg, u16 value);

/** @brief 读取一个 16 位 LT8920 寄存器。 */
int8 LT8920_ReadReg(lt8920_t *dev, u8 reg, u16 *value);

/** @brief 向 FIFO 寄存器 50 写入原始字节。 */
int8 LT8920_WriteFifo(lt8920_t *dev, const u8 *buf, u8 len);

/** @brief 从 FIFO 寄存器 50 读取原始字节。 */
int8 LT8920_ReadFifo(lt8920_t *dev, u8 *buf, u8 len);

/** @brief 保存新的 7 位信道，并把寄存器 7 写成 idle + 当前信道。 */
int8 LT8920_SetChannel(lt8920_t *dev, u8 channel);

/**
 * @brief 按旧工程布局设置 32 位同步字。
 *
 * reg36 写低 16 位，reg37 固定 0x0380，reg38 固定 0x5A5A，
 * reg39 写高 16 位。写完后会清 RX FIFO。
 */
int8 LT8920_SetSyncWord(lt8920_t *dev, u32 sync_word);

/**
 * @brief 直接设置同步字寄存器 36 和 39。
 *
 * 该接口保持寄存器 37 和 38 不变，用于对齐旧 RF_Encrypt_Config 行为。
 */
int8 LT8920_SetSyncRegs(lt8920_t *dev, u16 reg36, u16 reg39);

/** @brief 将寄存器 7 写为 idle + 当前信道。 */
int8 LT8920_EnterIdle(lt8920_t *dev);

/** @brief 准备普通收包模式，并将寄存器 7 写为 RX + 当前信道。 */
int8 LT8920_EnterRx(lt8920_t *dev);

/** @brief 准备普通发包模式，并将寄存器 7 写为 TX + 当前信道。 */
int8 LT8920_EnterTx(lt8920_t *dev);

/** @brief 按旧工程寄存器值进入连续载波 TX 测试模式。 */
int8 LT8920_EnterCarrierWave(lt8920_t *dev);

/** @brief 进入 idle，清 RX FIFO，然后进入 RX 模式。 */
int8 LT8920_OpenRx(lt8920_t *dev);

/** @brief 切换信道，进入 idle，清 RX FIFO，然后进入 RX 模式。 */
int8 LT8920_OpenRxOnChannel(lt8920_t *dev, u8 channel);

/** @brief 向寄存器 52 写 LT8920_FIFO_CLEAR_VALUE 清 TX FIFO。 */
int8 LT8920_ClearTxFifo(lt8920_t *dev);

/** @brief 向寄存器 52 写 LT8920_FIFO_CLEAR_VALUE 清 RX FIFO。 */
int8 LT8920_ClearRxFifo(lt8920_t *dev);

/**
 * @brief 向 TX FIFO 写入一帧带长度前缀的载荷。
 *
 * FIFO 数据格式为 [len][payload...]。本函数写入前会进入 idle 并清 TX FIFO，
 * 但不会启动发送。
 */
int8 LT8920_WritePacket(lt8920_t *dev, const u8 *buf, u8 len);

/** @brief 先写入一帧数据，再进入 TX 模式。 */
int8 LT8920_StartTxPacket(lt8920_t *dev, const u8 *buf, u8 len);

/** @brief 启动发送后执行一个可选短延时，用于对齐旧时序。 */
int8 LT8920_ForceTxPacket(lt8920_t *dev, const u8 *buf, u8 len);

/**
 * @brief 从 RX FIFO 读取一帧带长度前缀的数据。
 *
 * 需要状态寄存器已置 LT8920_STATUS_PKT_FLAG。CRC 错误或长度异常时，
 * 会通过 idle + 清 RX FIFO 复位接收路径。
 */
int8 LT8920_ReadPacket(lt8920_t *dev, u8 *buf, u8 buf_len, u8 *out_len);

/** @brief 读取状态寄存器 48。 */
int8 LT8920_ReadStatus(lt8920_t *dev, u16 *status);

/** @brief 读取寄存器 6 的 [15:10] 原始 6 位 RSSI 字段。 */
int8 LT8920_ReadRawRssi(lt8920_t *dev, u8 *rssi);

/** @brief 读取寄存器 52 的 [13:8] TX FIFO 字节计数。 */
int8 LT8920_ReadTxFifoCount(lt8920_t *dev, u8 *count);

/** @brief LT8920_STATUS_PKT_FLAG 置位时返回 1。 */
u8 LT8920_IsPacketDone(u16 status);

/** @brief LT8920_STATUS_CRC_ERROR 置位时返回 1。 */
u8 LT8920_IsCrcError(u16 status);

/** @brief 获取最近一次默认寄存器表校验失败信息。 */
void LT8920_GetVerifyFailure(lt8920_t *dev, u8 *reg, u16 *expected, u16 *actual);

/** @brief 返回 LT8920_WritePacket 最近记录的 TX FIFO 计数。 */
u8 LT8920_GetLastTxFifoCount(const lt8920_t *dev);

/** @brief 返回当前本地信道值；dev 为空时返回 0。 */
u8 LT8920_GetChannel(const lt8920_t *dev);

#endif
