/**
 * @file    wireless_port.h
 * @brief   无线模块板级适配层接口。
 * @author  boweny
 * @date    2026-05-06
 * @version v1.1
 *
 * @details
 * 本文件封装 LT8920 无线芯片所需的 GPIO、SPI、延时和天线控制接口。
 * 上层无线驱动只依赖这些适配函数，不直接操作 STC32G 外设寄存器。
 *
 * @note
 * 本文件是板级适配层接口，主要供 lt8920.c 和 wireless.c 调用；业务层
 * 不应直接操作 CS/RST/RXEN/TXEN 等引脚。
 *
 * @see     Code_boweny/Device/WIRELESS/wireless_port.c
 */

#ifndef __WIRELESS_PORT_H__
#define __WIRELESS_PORT_H__

#include "config.h"

#define WIRELESS_PORT_ANT1  0U  /**< 选择 1 号天线通道。 */
#define WIRELESS_PORT_ANT2  1U  /**< 选择 2 号天线通道。 */

/**
 * @brief   初始化无线模块板级端口。
 * @return  SUCCESS=成功，其他值表示初始化失败。
 */
s8 WirelessPort_Init(void);

/**
 * @brief   反初始化无线模块板级端口。
 * @return  SUCCESS=成功，其他值表示反初始化失败。
 */
s8 WirelessPort_Deinit(void);

/**
 * @brief      设置 LT8920 片选引脚电平。
 * @param[in]  level  片选输出电平，0=拉低，非 0=拉高。
 * @return     无。
 *
 * @note
 * 该接口只改变 GPIO 电平，不做 SPI 事务互斥或时序保护。
 */
void WirelessPort_SetCs(u8 level);

/**
 * @brief      设置 LT8920 复位引脚电平。
 * @param[in]  level  复位输出电平，0=拉低，非 0=拉高。
 * @return     无。
 */
void WirelessPort_SetRst(u8 level);

/**
 * @brief      切换无线天线通道。
 * @param[in]  ant_sel  天线选择，取值见 WIRELESS_PORT_ANT1/WIRELESS_PORT_ANT2。
 * @return     无。
 */
void WirelessPort_SetAntSel(u8 ant_sel);

/**
 * @brief      设置射频接收使能引脚。
 * @param[in]  level  接收使能电平，0=关闭，非 0=开启。
 * @return     无。
 */
void WirelessPort_SetRxEn(u8 level);

/**
 * @brief      设置射频发送使能引脚。
 * @param[in]  level  发送使能电平，0=关闭，非 0=开启。
 * @return     无。
 */
void WirelessPort_SetTxEn(u8 level);

/**
 * @brief   获取当前天线选择状态。
 * @return  当前天线编号，取值见 WIRELESS_PORT_ANT1/WIRELESS_PORT_ANT2。
 */
u8 WirelessPort_GetAntSel(void);

/**
 * @brief   获取接收使能状态。
 * @return  0=接收关闭，非 0=接收开启。
 */
u8 WirelessPort_GetRxEn(void);

/**
 * @brief   获取发送使能状态。
 * @return  0=发送关闭，非 0=发送开启。
 */
u8 WirelessPort_GetTxEn(void);

/**
 * @brief      毫秒级阻塞延时。
 * @param[in]  ms  延时时间，单位 ms。
 * @return     无。
 */
void WirelessPort_DelayMs(u16 ms);

/**
 * @brief      微秒级阻塞延时。
 * @param[in]  us  延时时间，单位 us。
 * @return     无。
 */
void WirelessPort_DelayUs(u16 us);

/**
 * @brief      通过 SPI 收发 1 字节数据。
 * @param[in]  value  要发送的数据字节。
 * @return     同步接收到的数据字节。
 *
 * @note
 * 调用方负责在传输前后控制 CS；该接口本身不自动拉低或释放片选。
 */
u8 WirelessPort_SpiTransfer(u8 value);

#endif
