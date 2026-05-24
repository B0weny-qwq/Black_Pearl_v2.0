/**
 * @file parameter_store.h
 * @brief 轻量参数保存服务。
 *
 * 本服务只保存 App 层传入的配置字节，不理解业务结构体内容；
 * 底层读写通过 BoardDevices/board_storage 完成。
 */

#ifndef __PARAMETER_STORE_H__
#define __PARAMETER_STORE_H__

#include "type_def.h"

#define PARAMETER_STORE_OK            0
#define PARAMETER_STORE_ERR_PARAM    -1
#define PARAMETER_STORE_ERR_EMPTY    -2
#define PARAMETER_STORE_ERR_VERIFY   -3
#define PARAMETER_STORE_ERR_IO       -4

#define PARAMETER_STORE_AUTODRIVE_MAX_LEN 16U

/**
 * @brief 初始化参数服务。
 * @return PARAMETER_STORE_OK 初始化完成。
 */
int8 parameter_store_init(void);

/**
 * @brief 读取 AutoDrive 配置字节。
 * @param buf 输出缓冲区。
 * @param len 期望读取长度，不能超过 PARAMETER_STORE_AUTODRIVE_MAX_LEN。
 * @return PARAMETER_STORE_OK 读取并校验成功。
 */
int8 parameter_store_load_autodrive(u8 *buf, u8 len);

/**
 * @brief 保存 AutoDrive 配置字节。
 * @param buf 输入配置字节。
 * @param len 配置长度，不能超过 PARAMETER_STORE_AUTODRIVE_MAX_LEN。
 * @return PARAMETER_STORE_OK 保存成功。
 */
int8 parameter_store_save_autodrive(const u8 *buf, u8 len);

#endif
