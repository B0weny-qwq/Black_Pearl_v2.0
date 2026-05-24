/**
 * @file board_storage.h
 * @brief 板级 EEPROM/IAP 存储接口。
 *
 * Services/App 通过本接口读写少量配置数据，不直接包含 STC EEPROM
 * 驱动头文件，也不直接操作 IAP 寄存器。
 */

#ifndef __BOARD_STORAGE_H__
#define __BOARD_STORAGE_H__

#include "type_def.h"

#define BOARD_STORAGE_OK          0
#define BOARD_STORAGE_ERR_PARAM  -1

/**
 * @brief 从板级非易失存储读取数据。
 * @param address 存储地址。
 * @param buf 输出缓冲区，不能为 NULL。
 * @param len 读取字节数。
 * @return BOARD_STORAGE_OK 读取完成。
 */
int8 board_storage_read(u32 address, u8 *buf, u16 len);

/**
 * @brief 写入板级非易失存储。
 * @param address 存储地址。
 * @param buf 输入缓冲区，不能为 NULL。
 * @param len 写入字节数。
 * @return BOARD_STORAGE_OK 写入完成。
 */
int8 board_storage_write(u32 address, const u8 *buf, u16 len);

/**
 * @brief 擦除包含指定地址的存储扇区。
 * @param address 扇区内任意地址。
 * @return BOARD_STORAGE_OK 擦除完成。
 */
int8 board_storage_erase_sector(u32 address);

#endif
