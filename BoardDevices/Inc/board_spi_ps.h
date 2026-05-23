/**
 * @file board_spi_ps.h
 * @brief Black Pearl 板级 SPI-PS 对等通信接口。
 *
 * 本文件属于 BoardDevices 层，用于封装 STC 官方 `APP_SPI_PS` 示例中的对等 SPI
 * 主从切换模型。App 层通过本接口发送和读取帧，不关心 SPI 引脚复用、SS 管脚、
 * STC SPI 模式位或 ISR 接收缓冲。
 */

#ifndef __BOARD_SPI_PS_H__
#define __BOARD_SPI_PS_H__

#include "type_def.h"

#define BOARD_SPI_PS_OK                 0
#define BOARD_SPI_PS_ERR_PARAM         -1
#define BOARD_SPI_PS_ERR_BUSY          -2
#define BOARD_SPI_PS_ERR_DRIVER        -3
#define BOARD_SPI_PS_ERR_OVERFLOW      -4

/**
 * @brief 初始化板级 SPI-PS 链路。
 *
 * 当前按官方 `APP_SPI_PS` 行为初始化：默认从机模式、SS 由引脚决定、MSB first、
 * CPOL=Low、CPHA=2Edge、SPI 时钟 Fosc/4，并开启 SPI 中断接收缓冲。
 *
 * @return BOARD_SPI_PS_OK 初始化成功。
 * @return BOARD_SPI_PS_ERR_DRIVER 底层 SPI 初始化失败。
 */
int8 board_spi_ps_init(void);

/**
 * @brief 查询 SPI-PS 总线是否空闲。
 *
 * 当前以板级 SS 引脚高电平作为可抢主发送条件。
 *
 * @return 1 总线空闲，可发送。
 * @return 0 总线忙。
 */
u8 board_spi_ps_is_idle(void);

/**
 * @brief 以 SPI-PS 主机窗口发送一帧数据。
 *
 * 函数会在 SS 空闲时拉低本端 SS、切换为主机、阻塞发送指定字节，然后释放 SS 并
 * 退回从机模式。若 SS 当前为低，说明对端可能正在发送，本函数直接返回忙。
 *
 * @param data 待发送数据，不能为 NULL。
 * @param len 待发送长度，不能为 0。
 * @return BOARD_SPI_PS_OK 发送完成。
 * @return BOARD_SPI_PS_ERR_PARAM 参数非法。
 * @return BOARD_SPI_PS_ERR_BUSY 总线忙。
 */
int8 board_spi_ps_send(const u8 *data, u8 len);

/**
 * @brief 推进 SPI-PS 从机接收帧超时判断。
 *
 * App 或调度器应周期性调用该函数。返回 1 表示 ISR 接收缓冲中的一帧已经完成，
 * 随后可调用 `board_spi_ps_read()` 取出数据。
 *
 * @return 1 接收帧完成。
 * @return 0 暂无完整接收帧。
 */
u8 board_spi_ps_service(void);

/**
 * @brief 读取并清空 SPI-PS 从机接收帧。
 *
 * @param buffer 输出缓冲，不能为 NULL。
 * @param max_len 输出缓冲最大长度。
 * @param out_len 实际读取长度，不能为 NULL。
 * @return BOARD_SPI_PS_OK 读取成功。
 * @return BOARD_SPI_PS_ERR_PARAM 参数非法。
 * @return BOARD_SPI_PS_ERR_OVERFLOW 输出缓冲不足，数据已截断且底层缓冲已清空。
 */
int8 board_spi_ps_read(u8 *buffer, u8 max_len, u8 *out_len);

/**
 * @brief 清空 SPI-PS 从机接收缓冲。
 */
void board_spi_ps_reset_rx(void);

#endif
