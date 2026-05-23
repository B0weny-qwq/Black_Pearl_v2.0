/**
 * @file ef_spi.h
 * @brief MCU SPI 统一封装接口。
 *
 * 本文件属于 McuAbstraction 层，对上隐藏 STC32G_SPI/STC32G_NVIC 的初始化
 * 结构体、中断使能和收发缓冲细节。BoardDevices 层通过本接口配置 SPI 模式、
 * 收发字节和读取从机接收缓冲；App 层不应直接包含本头文件。
 */

#ifndef __EF_SPI_H__
#define __EF_SPI_H__

#include "type_def.h"

#define EF_SPI_OK                 0
#define EF_SPI_ERR_PARAM         -1
#define EF_SPI_ERR_OVERFLOW      -2

#define EF_SPI_DISABLE            0U
#define EF_SPI_ENABLE             1U

#define EF_SPI_SS_BY_PIN          0U
#define EF_SPI_SS_IGNORE          1U

#define EF_SPI_MODE_SLAVE         0U
#define EF_SPI_MODE_MASTER        1U

#define EF_SPI_CPOL_LOW           0U
#define EF_SPI_CPOL_HIGH          1U

#define EF_SPI_CPHA_1EDGE         0U
#define EF_SPI_CPHA_2EDGE         1U

#define EF_SPI_FIRST_MSB          0U
#define EF_SPI_FIRST_LSB          1U

#define EF_SPI_SPEED_FOSC_4       0U
#define EF_SPI_SPEED_FOSC_8       1U
#define EF_SPI_SPEED_FOSC_16      2U
#define EF_SPI_SPEED_FOSC_2       3U

typedef struct
{
    u8 enable;      /**< SPI 使能，取值为 EF_SPI_ENABLE/EF_SPI_DISABLE。 */
    u8 ss_control;  /**< SS 控制方式，取值为 EF_SPI_SS_BY_PIN/EF_SPI_SS_IGNORE。 */
    u8 first_bit;   /**< 数据位序，取值为 EF_SPI_FIRST_MSB/EF_SPI_FIRST_LSB。 */
    u8 mode;        /**< 主从模式，取值为 EF_SPI_MODE_MASTER/EF_SPI_MODE_SLAVE。 */
    u8 cpol;        /**< 时钟极性，取值为 EF_SPI_CPOL_LOW/EF_SPI_CPOL_HIGH。 */
    u8 cpha;        /**< 时钟相位，取值为 EF_SPI_CPHA_1EDGE/EF_SPI_CPHA_2EDGE。 */
    u8 speed;       /**< SPI 分频，取值为 EF_SPI_SPEED_FOSC_x。 */
    u8 irq_enable;  /**< SPI 中断使能，取值为 EF_SPI_ENABLE/EF_SPI_DISABLE。 */
} ef_spi_config_t;

/**
 * @brief 初始化 SPI 外设。
 *
 * 内部调用 STC 官方 SPI 驱动，并按配置开启或关闭 SPI 中断。从机接收缓冲会在
 * 初始化时清空。
 *
 * @param config SPI 初始化参数，不能为 NULL。
 * @return EF_SPI_OK 初始化成功。
 * @return EF_SPI_ERR_PARAM 参数为空。
 */
int8 ef_spi_init(const ef_spi_config_t *config);

/**
 * @brief 切换 SPI 主从模式。
 *
 * @param mode EF_SPI_MODE_MASTER 或 EF_SPI_MODE_SLAVE。
 */
void ef_spi_set_mode(u8 mode);

/**
 * @brief 发送 1 字节 SPI 数据。
 *
 * 当前底层沿用 STC 官方阻塞发送行为；若 SPI 中断开启，会等待发送中断释放忙标志。
 *
 * @param data 待发送字节。
 */
void ef_spi_write_byte(u8 data);

/**
 * @brief 发送 1 字节并返回同时收到的字节。
 *
 * 用于 LT8920 这类需要全双工寄存器访问的 SPI 外设。若 SPI 中断开启，
 * 本函数会等待本次传输结束后再返回接收值。
 *
 * @param data 待发送字节。
 * @return 同步收到的字节。
 */
u8 ef_spi_transfer_byte(u8 data);

/**
 * @brief 读取 1 字节 SPI 数据。
 *
 * 本函数会发送 0xFF 产生时钟，并返回同时收到的数据。
 *
 * @return 读取到的字节。
 */
u8 ef_spi_read_byte(void);

/**
 * @brief 处理从机接收超时计数。
 *
 * STC 官方 SPI ISR 在从机收到字节后会重载接收超时计数。调用者周期性调用本函数，
 * 当计数归零且缓冲中有数据时，表示一帧接收完成。
 *
 * @return 1 接收帧完成。
 * @return 0 接收帧未完成。
 */
u8 ef_spi_slave_rx_tick(void);

/**
 * @brief 读取从机接收字节数。
 *
 * @return 当前从机接收缓冲字节数。
 */
u8 ef_spi_slave_rx_count(void);

/**
 * @brief 读取并清空从机接收缓冲。
 *
 * @param buffer 输出缓冲，不能为 NULL。
 * @param max_len 输出缓冲最大长度。
 * @param out_len 实际复制字节数，不能为 NULL。
 * @return EF_SPI_OK 读取成功。
 * @return EF_SPI_ERR_PARAM 参数为空或长度为 0。
 * @return EF_SPI_ERR_OVERFLOW 接收数据超过输出缓冲，已截断并清空底层缓冲。
 */
int8 ef_spi_slave_rx_read(u8 *buffer, u8 max_len, u8 *out_len);

/**
 * @brief 清空从机接收缓冲和超时计数。
 */
void ef_spi_slave_rx_reset(void);

#endif
