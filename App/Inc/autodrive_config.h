/**
 * @file autodrive_config.h
 * @brief AutoDrive 配置持久化接口。
 *
 * 本接口属于 App 层业务配置边界，只暴露返航开关和返航点配置的
 * 加载/保存动作，实际非易失存储由 Services/parameter_store 承担。
 */

#ifndef __AUTODRIVE_CONFIG_H__
#define __AUTODRIVE_CONFIG_H__

#include "autodrive.h"

void AutoDriveCfg_Init(void);
void AutoDriveCfg_Load(AutoDrive_ReturnConfig_t *cfg);
u8 AutoDriveCfg_Save(const AutoDrive_ReturnConfig_t *cfg);

#endif
