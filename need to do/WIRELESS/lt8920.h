/**
 * @file    lt8920.h
 * @brief   LT8920 2.4GHz 无线收发芯片驱动接口。
 * @author  boweny
 * @date    2026-05-06
 * @version v1.1
 *
 * @details
 * 提供 LT8920 初始化、信道/同步字配置、收发模式切换、FIFO 操作、
 * 状态读取和诊断信息读取接口。底层 SPI 与 GPIO 由 wireless_port 模块适配。
 *
 * @note
 * 本文件属于芯片层接口，正常业务优先通过 wireless.h 管理层访问无线链路。
 * 直接调用本层模式切换、同步字或 FIFO 接口时，要注意这些接口可能使芯片
 * 进入 idle/RX/TX，并清空 TX/RX FIFO。
 *
 * @see     Code_boweny/Device/WIRELESS/lt8920.c
 */

#ifndef __LT8920_H__
#define __LT8920_H__

#include "config.h"

#define LT8920_MAX_PAYLOAD_LEN       60U          /**< LT8920 单包最大载荷长度，单位 byte。 */

#define LT8920_DEFAULT_CHANNEL       0x30U        /**< 默认工作信道。 */
#define LT8920_DEFAULT_SYNC_WORD     0x03800380UL /**< 默认 32 位同步字。 */

#define LT89xx_6dBm                  0x4800U      /**< LT89xx 发射功率配置：约 6 dBm。 */
#define RF_Power                     LT89xx_6dBm  /**< 当前发射功率配置，保留兼容命名。 */
#define RadioFrequency_user          2450U        /**< 用户指定中心频率，单位 MHz。 */
#define Test_Channel                 (RadioFrequency_user - 2402U) /**< 由频率换算得到的测试信道。 */
#define Pair_Channel                 0x7FU        /**< 配对流程使用的固定信道。 */
#define Packet_Length                12U          /**< 兼容旧测试流程的固定包长。 */
#define Tx_Interval_mS               10U          /**< 兼容旧测试流程的发送间隔，单位 ms。 */
#define Work_Type                    0U           /**< 兼容旧测试流程的工作类型标志。 */
#define SyncPairWord                 0xE4E4E0E0UL /**< 兼容旧参考工程保留的配对同步字宏。 */
#define SyncTransferWord             0x6E6EFCFCUL /**< 兼容旧参考工程保留的数据同步字宏。 */

#define LT8920_STATUS_CRC_ERROR      0x8000U  /**< 状态寄存器 CRC 错误标志。 */
#define LT8920_STATUS_SYNC_RECV      0x0080U  /**< 状态寄存器同步字接收标志。 */
#define LT8920_STATUS_PKT_FLAG       0x0040U  /**< 状态寄存器数据包完成标志。 */
#define LT8920_STATUS_FIFO_FLAG      0x0020U  /**< 状态寄存器 FIFO 状态标志。 */

/**
 * @brief      初始化 LT8920 芯片。
 * @param[in]  channel    工作信道。
 * @param[in]  sync_word  32 位同步字。
 * @return     SUCCESS=成功，WIRELESS_ERR_* 表示失败原因。
 */
s8 LT8920_Init(u8 channel, u32 sync_word);

/**
 * @brief      设置 LT8920 工作信道。
 * @param[in]  channel  目标信道号。
 * @return     SUCCESS=成功，WIRELESS_ERR_* 表示失败原因。
 *
 * @note
 * 该函数只写芯片信道并进入 idle，不负责恢复无线管理层状态。
 */
s8 LT8920_SetChannel(u8 channel);

/**
 * @brief      设置 LT8920 32 位同步字。
 * @param[in]  sync_word  32 位同步字。
 * @return     SUCCESS=成功，WIRELESS_ERR_* 表示失败原因。
 *
 * @note
 * 写入同步字前会进入 idle，完成后会清 RX FIFO。
 */
s8 LT8920_SetSyncWord(u32 sync_word);

/**
 * @brief      直接设置 LT8920 同步字寄存器。
 * @param[in]  reg36  同步字寄存器 36 的值。
 * @param[in]  reg39  同步字寄存器 39 的值。
 * @return     SUCCESS=成功，WIRELESS_ERR_* 表示失败原因。
 *
 * @note
 * 对齐 `Wireless_other` 的 `RF_Encrypt_Config()`：只改 reg36/reg39，
 * 不改 reg37/reg38，不清 FIFO，也不自动打开 RX。
 */
s8 LT8920_SetSyncRegs(u16 reg36, u16 reg39);

/**
 * @brief   切换 LT8920 到空闲模式。
 * @return  SUCCESS=成功，WIRELESS_ERR_* 表示失败原因。
 */
s8 LT8920_EnterIdle(void);

/**
 * @brief   切换 LT8920 到接收模式。
 * @return  SUCCESS=成功，WIRELESS_ERR_* 表示失败原因。
 */
s8 LT8920_EnterRx(void);

/**
 * @brief   切换 LT8920 到发送模式。
 * @return  SUCCESS=成功，WIRELESS_ERR_* 表示失败原因。
 */
s8 LT8920_EnterTx(void);

/**
 * @brief   切换 LT8920 到连续载波输出模式。
 * @return  SUCCESS=成功，WIRELESS_ERR_* 表示失败原因。
 */
s8 LT8920_EnterCarrierWave(void);

/**
 * @brief   清空接收状态并打开接收窗口。
 * @return  SUCCESS=成功，WIRELESS_ERR_* 表示失败原因。
 *
 * @note
 * 该函数会进入 idle、清 RX FIFO，然后重新进入 RX。
 */
s8 LT8920_OpenRx(void);

/**
 * @brief      按旧版 `LT8920_OpenRx(FreqChannel, role)` 顺序打开指定信道 RX。
 * @param[in]  channel  接收信道，范围 0x00~0x7F。
 * @return     SUCCESS=成功，WIRELESS_ERR_* 表示失败原因。
 *
 * @note
 * 该接口先更新内部信道，再执行 `reg7 idle -> reg52 clear -> reg8 -> reg7 RX`，
 * 用于对齐 `Wireless_other` 工作接收入口。
 */
s8 LT8920_OpenRxOnChannel(u8 channel);

/**
 * @brief      写入并启动发送一帧数据。
 * @param[in]  buf  指向待发送载荷的指针。
 * @param[in]  len  载荷长度，单位 byte。
 * @return     SUCCESS=成功，WIRELESS_ERR_* 表示失败原因。
 */
s8 LT8920_StartTxPacket(const u8 *buf, u8 len);

/**
 * @brief      读取 LT8920 状态寄存器。
 * @param[out] status  指向状态寄存器输出变量的指针。
 * @return     SUCCESS=成功，WIRELESS_ERR_PARAM=空指针，其他 WIRELESS_ERR_* 表示读写失败。
 */
s8 LT8920_ReadStatus(u16 *status);

/**
 * @brief      读取 LT8920 原始 RSSI 值。
 * @param[out] rssi  指向 RSSI 输出变量的指针。
 * @return     SUCCESS=成功，WIRELESS_ERR_* 表示失败原因。
 */
s8 LT8920_ReadRawRssi(u8 *rssi);

/**
 * @brief   清空发送 FIFO。
 * @return  SUCCESS=成功，WIRELESS_ERR_* 表示失败原因。
 */
s8 LT8920_ClearTxFifo(void);

/**
 * @brief   清空接收 FIFO。
 * @return  SUCCESS=成功，WIRELESS_ERR_* 表示失败原因。
 *
 * @note
 * 清 FIFO 会丢弃尚未读取的接收数据。
 */
s8 LT8920_ClearRxFifo(void);

/**
 * @brief      向 LT8920 TX FIFO 写入一帧载荷。
 * @param[in]  buf  指向载荷缓冲区的指针。
 * @param[in]  len  载荷长度，单位 byte。
 * @return     SUCCESS=成功，WIRELESS_ERR_* 表示失败原因。
 */
s8 LT8920_WritePacket(const u8 *buf, u8 len);

/**
 * @brief      强制写入并发送一帧数据。
 * @param[in]  buf  指向待发送载荷的指针。
 * @param[in]  len  载荷长度，单位 byte。
 * @return     SUCCESS=成功，WIRELESS_ERR_* 表示失败原因。
 */
s8 LT8920_ForceTxPacket(const u8 *buf, u8 len);

/**
 * @brief      从 LT8920 RX FIFO 读取一帧载荷。
 * @param[out] buf      接收缓冲区指针。
 * @param[in]  buf_len  接收缓冲区容量，单位 byte。
 * @param[out] out_len  实际读取长度，单位 byte。
 * @return     SUCCESS=成功，WIRELESS_ERR_EMPTY=无包，其他 WIRELESS_ERR_* 表示失败。
 *
 * @note
 * 读取完成或遇到 CRC/长度异常后会复位 RX 路径。
 */
s8 LT8920_ReadPacket(u8 *buf, u8 buf_len, u8 *out_len);

/**
 * @brief      读取 LT8920 指定寄存器。
 * @param[in]  reg    寄存器地址。
 * @param[out] value  指向寄存器值输出变量的指针。
 * @return     SUCCESS=成功，WIRELESS_ERR_* 表示失败原因。
 */
s8 LT8920_ReadReg(u8 reg, u16 *value);

/**
 * @brief      获取最近一次寄存器校验失败信息。
 * @param[out] reg       失败寄存器地址输出指针，可为 NULL。
 * @param[out] expected  期望值输出指针，可为 NULL。
 * @param[out] actual    实际值输出指针，可为 NULL。
 * @return     无。
 */
void LT8920_GetVerifyFailure(u8 *reg, u16 *expected, u16 *actual);

/**
 * @brief   获取最近一次发送前 TX FIFO 计数。
 * @return  最近记录的 TX FIFO 计数值。
 */
u8 LT8920_GetLastTxFifoCount(void);

#endif
