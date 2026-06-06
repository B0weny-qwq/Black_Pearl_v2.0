/**
 * @file autodrive_config.h
 * @brief AutoDrive 返航原点持久化接口。
 *
 * 本接口属于 App 层业务配置边界，只把返航原点保存到 flash。
 * 钓点由遥控器每次下发，自动返航开关也只作为本次运行态输入。
 * 实际非易失存储由 Services/parameter_store 承担。
 */

#ifndef __AUTODRIVE_CONFIG_H__
#define __AUTODRIVE_CONFIG_H__

#include "autodrive.h"

/**
 * @brief 初始化 AutoDrive 配置缓存和底层参数服务。
 *
 * 本函数属于 App 配置边界，不直接访问 EEPROM/IAP 驱动；实际非易失存储由
 * Services/parameter_store 封装。
 */
void AutoDriveCfg_Init(void);

/**
 * @brief 读取返航原点配置。
 * @param cfg 输出配置，不能为 NULL；读取失败时写入默认关闭返航配置。
 */
void AutoDriveCfg_Load(AutoDrive_ReturnConfig_t *cfg);

/**
 * @brief 保存返航原点配置。
 * @param cfg 输入配置，不能为 NULL。
 * @return 1 保存成功，0 参数为空或参数服务写入失败。
 */
u8 AutoDriveCfg_Save(const AutoDrive_ReturnConfig_t *cfg);

#endif
