#ifndef __KCT8206_H__
#define __KCT8206_H__

/**
 * @file KCT8206.h
 * @brief KCT8206 射频前端控制层。
 *
 * 本层只描述无线硬件用到的前端控制信号：RXEN、TXEN 和可选 ANT_SEL。
 * 这里不绑定 MCU GPIO 端口，也不处理 LT8920 发包/收包状态。
 */

#include "type_def.h"

/** 驱动返回值，0 表示成功，负数表示错误。 */
#define KCT8206_OK                 0
#define KCT8206_ERR_PARAM          -1

/** 旧板级硬件里的 ANT_SEL 电平：ANT1=0，ANT2=1。 */
#define KCT8206_ANT1               0U
#define KCT8206_ANT2               1U

/** 前端状态缓存值。 */
#define KCT8206_STATE_IDLE         0U
#define KCT8206_STATE_RX           1U
#define KCT8206_STATE_TX           2U

/**
 * @brief 设置一个前端控制脚电平。
 * @param ctx    外部 GPIO 上下文。
 * @param level  0=低电平，非 0=高电平。
 */
typedef void (*kct8206_set_level_fn)(void *ctx, u8 level);

/**
 * @brief 读取一个前端控制脚电平。
 * @param ctx  外部 GPIO 上下文。
 * @return 0=低电平，非 0=高电平。
 */
typedef u8 (*kct8206_get_level_fn)(void *ctx);

/** @brief 可选 us 延时回调，用于 RX/TX 使能后的稳定等待。 */
typedef void (*kct8206_delay_us_fn)(void *ctx, u16 us);

/**
 * @brief KCT8206 前端层需要的外部回调。
 *
 * set_rxen 和 set_txen 必填。set_ant_sel 和所有 get 回调可为空，
 * 这样固定天线或只能写不能读的 GPIO 也能使用。
 */
typedef struct
{
    void *ctx;
    kct8206_set_level_fn set_rxen;
    kct8206_set_level_fn set_txen;
    kct8206_set_level_fn set_ant_sel;
    kct8206_get_level_fn get_rxen;
    kct8206_get_level_fn get_txen;
    kct8206_get_level_fn get_ant_sel;
    kct8206_delay_us_fn delay_us;
} kct8206_bus_t;

/** KCT8206 前端实例。 */
typedef struct
{
    kct8206_bus_t bus;
    u8 state;
    u8 antenna;
} kct8206_t;

/** 前端状态快照；有读取回调时同时包含实际 GPIO 电平。 */
typedef struct
{
    u8 state;
    u8 antenna;
    u8 rxen;
    u8 txen;
    u8 ant_sel;
} kct8206_status_t;

/**
 * @brief 绑定 GPIO 回调，并强制前端进入 idle、选择 ANT1。
 * @return KCT8206_OK 或 KCT8206_ERR_PARAM。
 */
int8 KCT8206_Bind(kct8206_t *dev, const kct8206_bus_t *bus);

/** @brief 在提供 ANT_SEL 回调时选择 ANT1 或 ANT2。 */
int8 KCT8206_SetAntenna(kct8206_t *dev, u8 antenna);

/** @brief 同时关闭 TXEN 和 RXEN。 */
int8 KCT8206_EnterIdle(kct8206_t *dev);

/** @brief 接收前端状态：TXEN=0，RXEN=1，然后可选等待稳定。 */
int8 KCT8206_EnterRx(kct8206_t *dev);

/**
 * @brief 旧工程 TX 预备时序：保持 RXEN=1，先拉低 TXEN，再短延时。
 *
 * 单独拆出该步骤，方便外部板级代码按旧无线实现对齐 LT8920 FIFO 写入和
 * TXEN 上升沿时机。
 */
int8 KCT8206_PrepareLegacyTx(kct8206_t *dev);

/** @brief 执行旧 TX 预备时序后，将 TXEN 拉高。 */
int8 KCT8206_EnterTx(kct8206_t *dev);

/** @brief 读取前端状态缓存，以及可选 GPIO 回读电平。 */
int8 KCT8206_ReadStatus(kct8206_t *dev, kct8206_status_t *status);

#endif
