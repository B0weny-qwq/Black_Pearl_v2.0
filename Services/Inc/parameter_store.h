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
#define PARAMETER_STORE_NORTH_CALIB_MAX_LEN 16U

/**
 * @brief 初始化参数服务。
 * @return PARAMETER_STORE_OK 初始化完成。
 */
int8 parameter_store_init(void);

/**
 * @brief 读取 AutoDrive 返航原点字节。
 * @param buf 输出缓冲区。
 * @param len 期望读取长度，不能超过 PARAMETER_STORE_AUTODRIVE_MAX_LEN。
 * @return PARAMETER_STORE_OK 读取并校验成功。
 */
int8 parameter_store_load_autodrive(u8 *buf, u8 len);

/**
 * @brief 保存 AutoDrive 返航原点字节。
 * @param buf 输入返航原点字节。
 * @param len 原点长度，不能超过 PARAMETER_STORE_AUTODRIVE_MAX_LEN。
 * @return PARAMETER_STORE_OK 保存成功。
 */
int8 parameter_store_save_autodrive(const u8 *buf, u8 len);

/**
 * @brief 读取北向校准参数槽。
 *
 * 北向校准使用 A/B 槽保存最近一次有效 `north_offset_cd` 记录。参数服务只处理
 * 字节记录、槽地址和校验，业务状态机仍由 App 层维护。
 *
 * @param slot 槽位索引，0 表示 A 槽，1 表示 B 槽。
 * @param buf 输出缓冲区，不能为 NULL。
 * @param len 期望读取长度，不能超过 PARAMETER_STORE_NORTH_CALIB_MAX_LEN。
 * @return PARAMETER_STORE_OK 读取并校验成功。
 */
int8 parameter_store_load_north_calib_slot(u8 slot, u8 *buf, u8 len);

/**
 * @brief 保存北向校准参数槽。
 * @param slot 槽位索引，0 表示 A 槽，1 表示 B 槽。
 * @param buf 输入记录字节，不能为 NULL。
 * @param len 记录长度，不能超过 PARAMETER_STORE_NORTH_CALIB_MAX_LEN。
 * @return PARAMETER_STORE_OK 保存成功。
 */
int8 parameter_store_save_north_calib_slot(u8 slot, const u8 *buf, u8 len);

#endif
