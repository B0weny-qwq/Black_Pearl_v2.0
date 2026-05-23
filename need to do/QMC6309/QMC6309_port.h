/**
 * @file    QMC6309_port.h
 * @brief   QMC6309 板级端口与 I2C 后端适配接口。
 * @author  boweny
 * @date    2026-05-07
 * @version v1.1
 *
 * @details
 * 当前 QMC6309 端口层复用 QMI8658 的软/硬 I2C 后端实现，
 * 对上层地磁驱动暴露统一的初始化、延时、总线恢复和寄存器读写接口。
 *
 * @note
 * 若后续磁力计改为独立总线，只修改本层，不修改 `QMC6309.c` 业务逻辑。
 *
 * @see     Code_boweny/Device/QMC6309/QMC6309_port.c
 * @see     Code_boweny/Device/QMI8658/QMI8658_port.h
 */

#ifndef __QMC6309_PORT_H__
#define __QMC6309_PORT_H__

#include "..\..\..\User\Config.h"
#include "..\QMI8658\QMI8658_port.h"

#define QMC6309_I2C_OK              QMI8658_I2C_OK         /**< I2C 访问成功。 */
#define QMC6309_I2C_ERR_BUSY        QMI8658_I2C_ERR_BUSY   /**< 总线忙。 */
#define QMC6309_I2C_ERR_DEVW_NACK   QMI8658_I2C_ERR_DEVW_NACK /**< 设备写地址未应答。 */
#define QMC6309_I2C_ERR_REG_NACK    QMI8658_I2C_ERR_REG_NACK  /**< 寄存器地址未应答。 */
#define QMC6309_I2C_ERR_DEVR_NACK   QMI8658_I2C_ERR_DEVR_NACK /**< 设备读地址未应答。 */
#define QMC6309_I2C_ERR_DATA_NACK   QMI8658_I2C_ERR_DATA_NACK /**< 数据阶段未应答。 */
#define QMC6309_I2C_ERR_PARAM       QMI8658_I2C_ERR_PARAM     /**< 参数错误。 */

#define QMC6309_I2C_USE_SOFT        QMI8658_I2C_USE_SOFT      /**< 是否使用软件 I2C。 */
#define QMC6309_SOFT_I2C_DELAY_US   QMI8658_SOFT_I2C_DELAY_US /**< 软件 I2C 位间延时。 */

/**
 * @brief   初始化 QMC6309 端口层。
 * @return  SUCCESS=成功，其他值表示底层准备失败。
 */
s8 QMC6309Port_Init(void);

/**
 * @brief   获取当前 I2C 后端名称。
 * @return  指向静态字符串的指针，例如 `"hard"` 或 `"soft"`。
 */
char *QMC6309Port_BackendName(void);

/**
 * @brief      毫秒级阻塞延时。
 * @param[in]  ms  延时时间，单位 ms。
 * @return     无。
 */
void QMC6309Port_DelayMs(u16 ms);

/**
 * @brief   查询当前总线是否需要恢复。
 * @return  1=需要恢复，0=总线状态正常。
 */
u8 QMC6309Port_BusNeedsRecover(void);

/**
 * @brief   执行总线恢复。
 * @return  无。
 */
void QMC6309Port_BusRecover(void);

/**
 * @brief      写 QMC6309 单字节寄存器。
 * @param[in]  addr      I2C 设备地址。
 * @param[in]  reg_addr  寄存器地址。
 * @param[in]  reg_val   要写入的寄存器值。
 * @param[out] err_code  I2C 错误码输出指针，可为 NULL。
 * @return     1=成功，0=失败。
 */
u8 QMC6309Port_WriteReg(u8 addr, u8 reg_addr, u8 reg_val, u8 *err_code);

/**
 * @brief      连续读取 QMC6309 多字节寄存器。
 * @param[in]  addr       I2C 设备地址。
 * @param[in]  start_reg  起始寄存器地址。
 * @param[out] buf        读缓冲区指针。
 * @param[in]  len        读取长度，单位 byte。
 * @param[out] err_code   I2C 错误码输出指针，可为 NULL。
 * @return     1=成功，0=失败。
 */
u8 QMC6309Port_ReadN(u8 addr, u8 start_reg, u8 *buf, u8 len, u8 *err_code);

#endif
