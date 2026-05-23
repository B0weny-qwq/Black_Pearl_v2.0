/**
 * @file    wireless.h
 * @brief   LT8920 无线链路管理层接口。
 * @author  boweny
 * @date    2026-05-06
 * @version v1.1
 *
 * @details
 * 本模块在 LT8920 芯片层之上提供初始化、收发队列、天线扫描、
 * 链路参数切换和 bring-up 诊断接口。业务层通常只需要调用
 * Wireless_Init()、Wireless_Poll()、Wireless_Send()、Wireless_Receive()
 * 以及必要的信道/同步字切换接口。
 *
 * Wireless_Receive() 返回的是 LT8920 RF payload，即发送端写入 FIFO 的
 * 业务字节序列，不负责按 `AA | len | cmd | payload | xor | BB` 做协议截帧。
 * 旧遥控器兼容协议的逐字节找帧和业务分发由 ShipProtocol_RunScheduler()
 * 内部完成。
 *
 * @note
 * Wireless_RunTxDiagBurst() 与 Wireless_RunPairTxOnlyTest() 是硬件 bring-up
 * 诊断接口，不属于当前遥控器配对最小业务主路径。
 *
 * @see     Code_boweny/Device/WIRELESS/wireless.c
 */

#ifndef __WIRELESS_H__
#define __WIRELESS_H__

#include "config.h"

#define WIRELESS_OK                SUCCESS  /**< 无线操作成功。 */
#define WIRELESS_ERR_PARAM         (-2)     /**< 参数非法或空指针。 */
#define WIRELESS_ERR_STATE         (-3)     /**< 当前状态不允许执行该操作。 */
#define WIRELESS_ERR_IO            (-4)     /**< 底层通信或外设读写失败。 */
#define WIRELESS_ERR_TIMEOUT       (-5)     /**< 等待发送、接收或状态变化超时。 */
#define WIRELESS_ERR_EMPTY         (-6)     /**< 接收队列或 FIFO 为空。 */
#define WIRELESS_ERR_OVERFLOW      (-7)     /**< 队列或缓冲区溢出。 */
#define WIRELESS_ERR_VERIFY        (-8)     /**< 寄存器回读、CRC 或帧校验失败。 */

#define WIRELESS_TAG               "WL"  /**< 日志输出使用的模块标签。 */

#define WIRELESS_ANT1              0U  /**< 1 号天线。 */
#define WIRELESS_ANT2              1U  /**< 2 号天线。 */

#define WIRELESS_MODE_IDLE         0U  /**< 无线芯片空闲模式。 */
#define WIRELESS_MODE_RX           1U  /**< 无线芯片接收模式。 */
#define WIRELESS_MODE_TX           2U  /**< 无线芯片发送模式。 */

#define WIRELESS_RX_QUEUE_DEPTH    4U     /**< 接收软件队列深度。 */
#define WIRELESS_SCAN_SAMPLE_COUNT 16U    /**< 天线扫描时每路采样次数。 */
#define WIRELESS_SCAN_SAMPLE_MS    2U     /**< 天线扫描单次采样间隔，单位 ms。 */
#define WIRELESS_SIGNAL_RSSI_MIN   12U    /**< 判定存在有效信号的最小 RSSI。 */
#define WIRELESS_SEARCH_POLL_DIV   32U    /**< 搜索信号轮询分频系数。 */
#define WIRELESS_SEARCH_MAX_RETRY  5U     /**< 运行期最多重扫次数，超过后进入正常收发。 */
#define WIRELESS_TX_TIMEOUT_LOOPS  1000U  /**< 发送完成轮询最大循环次数。 */
#define WIRELESS_TX_PKT_POLL_US    1000U  /**< 发送完成轮询间隔，单位 us。 */

/**
 * @brief   无线链路运行状态快照。
 */
typedef struct
{
    u8 initialized;         /**< 初始化标志，1=已初始化。 */
    u8 ready;               /**< 链路可用标志，1=底层芯片和参数已就绪。 */
    u8 mode;                /**< 当前工作模式，取值见 WIRELESS_MODE_*。 */
    u8 antenna;             /**< 当前使用天线，取值见 WIRELESS_ANT1/WIRELESS_ANT2。 */
    u8 scan_has_signal;     /**< 最近一次扫描是否检测到有效信号。 */
    s8 last_error;          /**< 最近一次无线操作错误码。 */

    u16 tx_ok_count;        /**< 成功发送帧计数。 */
    u16 rx_ok_count;        /**< 成功接收帧计数。 */
    u16 crc_error_count;    /**< CRC 或帧校验错误计数。 */
    u16 queue_overflow_count; /**< 接收软件队列溢出计数。 */
    u16 rx_drop_count;      /**< 接收丢弃帧计数。 */

    u16 antenna_rssi_ant1;  /**< 1 号天线最近一次扫描 RSSI 累计/评分。 */
    u16 antenna_rssi_ant2;  /**< 2 号天线最近一次扫描 RSSI 累计/评分。 */
} Wireless_State_t;

/**
 * @brief   无线接收路径调试快照。
 */
typedef struct
{
    u16 reg7;        /**< LT8920 reg7（模式位+信道位）。 */
    u16 reg8;        /**< LT8920 reg8（收发相关配置）。 */
    u16 reg36;       /**< LT8920 reg36（老版同步字低段/key0 key0）。 */
    u16 reg37;       /**< LT8920 reg37（老版保留默认同步段）。 */
    u16 reg38;       /**< LT8920 reg38（老版保留默认同步段）。 */
    u16 reg39;       /**< LT8920 reg39（老版同步字高段/key1 key1）。 */
    u16 reg48;       /**< LT8920 reg48（PKT/CRC 等状态位）。 */
    u16 reg52;       /**< LT8920 reg52（FIFO 状态/计数）。 */
    u8 rssi;         /**< LT8920 原始 RSSI 读数。 */
    u8 rx_en;        /**< 前端 RXEN 引脚状态。 */
    u8 tx_en;        /**< 前端 TXEN 引脚状态。 */
    u8 mode;         /**< 无线管理层当前模式，见 WIRELESS_MODE_*。 */
    u8 rx_mode_bit;  /**< reg7 的 RX 模式位，1=RX，0=非RX。 */
    u8 channel;      /**< reg7 低 7 位信道值。 */
} Wireless_RxDebug_t;

/**
 * @brief   初始化无线链路。
 * @return  SUCCESS=成功，WIRELESS_ERR_* 表示失败原因。
 */
s8 Wireless_Init(void);

/**
 * @brief   关闭无线链路并释放板级端口状态。
 * @return  SUCCESS=成功，WIRELESS_ERR_* 表示失败原因。
 */
s8 Wireless_Deinit(void);

/**
 * @brief   轮询无线链路，接收新数据并维护内部状态。
 * @return  SUCCESS=成功，WIRELESS_ERR_* 表示失败原因。
 */
s8 Wireless_Poll(void);

/**
 * @brief      发送一帧无线数据。
 * @param[in]  buf  指向待发送数据缓冲区的指针。
 * @param[in]  len  待发送数据长度，单位 byte。
 * @return     SUCCESS=发送完成，WIRELESS_ERR_* 表示失败原因。
 */
s8 Wireless_Send(const u8 *buf, u8 len);

/**
 * @brief      按旧版 `LT8920_TxData()` 时序在指定信道发送一帧。
 * @param[in]  channel  发送信道。
 * @param[in]  buf      待发送 RF payload。
 * @param[in]  len      payload 长度，单位 byte。
 * @return     SUCCESS=发送完成，WIRELESS_ERR_* 表示失败原因。
 *
 * @note
 * 该接口用于 `Wireless_other` 业务移植：先把 reg7 写到指定信道 idle，
 * 再清 TX FIFO、写 FIFO、进入 TX，发送结束后保持 idle，不自动打开 RX。
 */
s8 Wireless_SendOnChannel(u8 channel, const u8 *buf, u8 len);

/**
 * @brief      从无线接收队列读取一帧数据。
 * @param[out] buf      接收缓冲区指针。
 * @param[in]  buf_len  接收缓冲区容量，单位 byte。
 * @param[out] out_len  实际读出的帧长度，单位 byte。
 * @return     SUCCESS=读取成功，WIRELESS_ERR_EMPTY=无数据，其他 WIRELESS_ERR_* 表示失败。
 *
 * @note
 * 这里的“一帧”指一次 LT8920 RF payload，不等同于旧业务协议帧。
 * 业务层必须允许 payload 内存在前导噪声或多余字节，并由协议层按长度字段截帧。
 */
s8 Wireless_Receive(u8 *buf, u8 buf_len, u8 *out_len);

/**
 * @brief      手动切换无线天线。
 * @param[in]  ant_sel  目标天线，取值见 WIRELESS_ANT1/WIRELESS_ANT2。
 * @return     SUCCESS=切换成功，WIRELESS_ERR_* 表示失败原因。
 */
s8 Wireless_SetAntenna(u8 ant_sel);

/**
 * @brief      获取无线链路状态快照。
 * @param[out] state  指向状态结构体的输出指针。
 * @return     SUCCESS=获取成功，WIRELESS_ERR_PARAM=空指针。
 */
s8 Wireless_GetState(Wireless_State_t *state);

/**
 * @brief   重新扫描天线并选择信号较好的通道。
 * @return  SUCCESS=扫描完成，WIRELESS_ERR_* 表示失败原因。
 */
s8 Wireless_RescanAntenna(void);

/**
 * @brief   按低频分频节奏搜索无线信号。
 * @return  SUCCESS=本次轮询完成，WIRELESS_ERR_* 表示失败原因。
 */
s8 Wireless_SearchSignalPoll(void);

/**
 * @brief   运行最小无线收发自测试流程。
 *
 * @details
 * 用于上电后一次性验证 LT8920 SPI/寄存器读写链路。该函数会恢复默认
 * 同步字和默认信道；在 WIRELESS_MINIMAL_TEST_ONLY=1 时不会进入短发包探测。
 *
 * @return  SUCCESS=测试通过，WIRELESS_ERR_* 表示失败原因。
 */
s8 Wireless_RunMinimalTest(void);

/**
 * @brief      运行发送诊断突发测试。
 * @param[in]  log_detail  1=输出详细日志，0=只输出关键结果。
 * @return     SUCCESS=测试完成，WIRELESS_ERR_* 表示失败原因。
 *
 * @note
 * 仅用于硬件发射链路诊断；正常业务主循环不应周期调用该接口。
 */
s8 Wireless_RunTxDiagBurst(u8 log_detail);

/**
 * @brief      运行配对发送端单向诊断测试。
 * @param[in]  log_detail  1=输出详细日志，0=只输出关键结果。
 * @return     SUCCESS=测试完成，WIRELESS_ERR_* 表示失败原因。
 *
 * @note
 * 仅用于验证 PAIR_REQ(0x10) 单向发射，不接收遥控器响应；当前最小业务
 * 配对流程由 ShipProtocol_RunScheduler() 负责。
 */
s8 Wireless_RunPairTxOnlyTest(u8 log_detail);

/**
 * @brief      设置无线工作信道。
 * @param[in]  channel  LT8920 信道号。
 * @return     SUCCESS=设置成功，WIRELESS_ERR_* 表示失败原因。
 *
 * @note
 * 调用后会重新打开 RX，底层可能清空 LT8920 RX FIFO；应只在协议状态切换
 * 或发包前后等明确时机调用。
 */
s8 Wireless_SetChannel(u8 channel);

/**
 * @brief      设置无线同步字。
 * @param[in]  sync_word  32 位同步字。
 * @return     SUCCESS=设置成功，WIRELESS_ERR_* 表示失败原因。
 *
 * @note
 * 调用后会重新打开 RX，底层可能清空 LT8920 RX FIFO。
 */
s8 Wireless_SetSyncWord(u32 sync_word);

/**
 * @brief      直接设置 LT8920 同步寄存器值。
 * @param[in]  reg36  同步字寄存器 36 的值。
 * @param[in]  reg39  同步字寄存器 39 的值。
 * @return     SUCCESS=设置成功，WIRELESS_ERR_* 表示失败原因。
 *
 * @note
 * 该接口用于兼容 `Wireless_other` 中由 seed 派生的同步字，只更新 reg36/reg39，
 * 保持 reg37/reg38 为老版默认值；调用后会重新打开 RX，底层可能清空 LT8920 RX FIFO。
 */
s8 Wireless_SetSyncRegs(u16 reg36, u16 reg39);

/**
 * @brief      按旧版 `RF_Encrypt_Config()` 时序只写同步寄存器并停在 idle。
 * @param[in]  reg36  同步字寄存器 36 的值。
 * @param[in]  reg39  同步字寄存器 39 的值。
 * @return     SUCCESS=设置成功，WIRELESS_ERR_* 表示失败原因。
 *
 * @note
 * 该接口只改 reg36/reg39，不清 FIFO，并保持 idle，不自动打开 RX；业务层随后应显式
 * 切到工作信道 RX，对齐旧版 `RF_Receive(work_ch)`。
 */
s8 Wireless_SetSyncRegsIdle(u16 reg36, u16 reg39);

/**
 * @brief      读取当前接收路径调试快照。
 * @param[out] dbg  调试快照输出指针。
 * @return     SUCCESS=读取成功，WIRELESS_ERR_* 表示失败原因。
 */
s8 Wireless_GetRxDebug(Wireless_RxDebug_t *dbg);

#endif
