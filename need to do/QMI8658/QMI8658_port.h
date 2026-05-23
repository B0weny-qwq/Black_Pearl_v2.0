/**
 * @file    QMI8658_port.h
 * @brief   QMI8658 板级端口与 I2C 后端适配接口。
 * @author  boweny
 * @date    2026-05-07
 * @version v1.1
 *
 * @details
 * 本文件封装 QMI8658 所需的延时、总线恢复和寄存器读写接口，
 * 并对外暴露统一的软/硬 I2C 错误码定义。
 *
 * @see     Code_boweny/Device/QMI8658/QMI8658_port.c
 */

#ifndef __QMI8658_PORT_H__
#define __QMI8658_PORT_H__

#include "..\..\..\User\Config.h"
#include "QMI8658.h"

#define QMI8658_I2C_OK              0U  /**< I2C 访问成功。 */
#define QMI8658_I2C_ERR_BUSY        1U  /**< 总线忙。 */
#define QMI8658_I2C_ERR_DEVW_NACK   2U  /**< 设备写地址未应答。 */
#define QMI8658_I2C_ERR_REG_NACK    3U  /**< 寄存器地址未应答。 */
#define QMI8658_I2C_ERR_DEVR_NACK   4U  /**< 设备读地址未应答。 */
#define QMI8658_I2C_ERR_DATA_NACK   5U  /**< 数据阶段未应答。 */
#define QMI8658_I2C_ERR_PARAM       6U  /**< 参数错误。 */

/**
 * @brief   初始化 QMI8658 端口层。
 * @return  SUCCESS=成功，其他值表示底层准备失败。
 */
s8 QMI8658Port_Init(void);

/**
 * @brief   获取当前 I2C 后端名称。
 * @return  指向静态字符串的指针，例如 `"hard"` 或 `"soft"`。
 */
char *QMI8658Port_BackendName(void);

/**
 * @brief      毫秒级阻塞延时。
 * @param[in]  ms  延时时间，单位 ms。
 * @return     无。
 */
void QMI8658Port_DelayMs(u16 ms);

/**
 * @brief      微秒级阻塞延时。
 * @param[in]  us  延时时间，单位 us。
 * @return     无。
 */
void QMI8658Port_DelayUs(u16 us);

/**
 * @brief   查询当前总线是否需要恢复。
 * @return  1=需要恢复，0=总线状态正常。
 */
u8 QMI8658Port_BusNeedsRecover(void);

/**
 * @brief   执行总线恢复。
 * @return  无。
 */
void QMI8658Port_BusRecover(void);

/**
 * @brief      写 QMI8658 单字节寄存器。
 * @param[in]  addr      I2C 设备地址。
 * @param[in]  reg_addr  寄存器地址。
 * @param[in]  reg_val   要写入的寄存器值。
 * @param[out] err_code  I2C 错误码输出指针，可为 NULL。
 * @return     1=成功，0=失败。
 */
u8 QMI8658Port_WriteReg(u8 addr, u8 reg_addr, u8 reg_val, u8 *err_code);

/**
 * @brief      连续读取 QMI8658 多字节寄存器。
 * @param[in]  addr       I2C 设备地址。
 * @param[in]  start_reg  起始寄存器地址。
 * @param[out] buf        读缓冲区指针。
 * @param[in]  len        读取长度，单位 byte。
 * @param[out] err_code   I2C 错误码输出指针，可为 NULL。
 * @return     1=成功，0=失败。
 */
u8 QMI8658Port_ReadN(u8 addr, u8 start_reg, u8 *buf, u8 len, u8 *err_code);

#endif
