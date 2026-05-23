/**
 * @file    QMC6309.h
 * @brief   QMC6309 地磁计驱动接口。
 * @author  boweny
 * @date    2026-05-07
 * @version v1.1
 *
 * @details
 * 提供 QMC6309 的初始化、芯片 ID 读取、原始三轴读取、滤波后三轴读取、
 * 输出速率设置和寄存器诊断接口。当前根目录工程中磁力计处于启用状态，
 * 通过 `AHRS_UpdateRawMag()` 和 `HeadingEstimator` 参与航向估计主链，
 * 同时也保留独立读数与寄存器诊断能力。
 *
 * @hardware
 * - 总线: 复用传感器 I2C 后端
 * - 默认引脚: P1.4=SDA / P1.5=SCL
 *
 * @note
 * 当前默认 `ENABLE_MAG_MODULE=1`，`ENABLE_MAG_STANDALONE_POLL=0`。
 * 也就是磁力计默认不单独刷屏，而是按 `AHRS_MAG_PERIOD_MS` 被 AHRS 低频读取。
 *
 * @see     Code_boweny/Device/QMC6309/QMC6309.c
 * @see     Code_boweny/Device/QMC6309/QMC6309_port.h
 */

#ifndef __QMC6309_H__
#define __QMC6309_H__

#include "Config.h"

#define QMC6309_I2C_ADDR_PRIMARY   0x7C                  /**< QMC6309 主 I2C 地址。 */
#define QMC6309_I2C_ADDR_ALT       0x0C                  /**< QMC6309 备选 I2C 地址。 */
#define QMC6309_CHIP_ID_VALUE      0x90                  /**< QMC6309 期望芯片 ID。 */
#define QMC6309_I2C_SPEED_CFG      SENSOR_I2C_SPEED_CFG  /**< 当前 I2C 速度配置。 */

/**
 * @brief   初始化 QMC6309 地磁计。
 * @return  SUCCESS=成功，其他值表示地址探测、寄存器配置或读写失败。
 */
s8 QMC6309_Init(void);

/**
 * @brief      读取地磁计原始三轴数据。
 * @param[out] x  X 轴输出指针。
 * @param[out] y  Y 轴输出指针。
 * @param[out] z  Z 轴输出指针。
 * @return     SUCCESS=成功，其他值表示读数失败或数据无效。
 */
s8 QMC6309_ReadXYZ(int16 *x, int16 *y, int16 *z);

/**
 * @brief      读取经软件低通后的地磁三轴数据。
 * @param[out] x  X 轴输出指针。
 * @param[out] y  Y 轴输出指针。
 * @param[out] z  Z 轴输出指针。
 * @return     SUCCESS=成功，其他值表示读数失败或滤波输入无效。
 */
s8 QMC6309_ReadXYZFiltered(int16 *x, int16 *y, int16 *z);

/**
 * @brief   读取 QMC6309 芯片 ID。
 * @return  读到的芯片 ID 字节。
 */
u8 QMC6309_ReadID(void);

/**
 * @brief      设置 QMC6309 输出数据率。
 * @param[in]  odr  目标输出速率寄存器值。
 * @return     SUCCESS=成功，其他值表示写寄存器失败。
 */
s8 QMC6309_SetODR(u8 odr);

/**
 * @brief      等待地磁计数据就绪。
 * @param[in]  timeout_ms  超时时间，单位 ms。
 * @return     SUCCESS=成功，其他值表示超时或总线访问失败。
 */
s8 QMC6309_Wait_Ready(u16 timeout_ms);

/**
 * @brief      打印指定地址下的 QMC6309 关键寄存器。
 * @param[in]  target_addr  目标 I2C 地址。
 * @return     无。
 *
 * @note
 * 仅用于 bring-up 或现场排障日志，不是业务主路径接口。
 */
void QMC6309_DumpRegs(u8 target_addr);

#endif
