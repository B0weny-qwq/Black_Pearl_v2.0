#include "autodrive_config.h"
#include "parameter_store.h"
#include "logger.h"

#define AUTODRIVE_CFG_WIRE_LEN  11U

static AutoDrive_ReturnConfig_t autodrive_cfg_cache;

static void AutoDriveCfg_Default(AutoDrive_ReturnConfig_t *cfg)
{
    if (cfg == 0) {
        return;
    }

    cfg->auto_ret_onoff = 0x30U;
    cfg->ret_point.lon_ew = 0U;
    cfg->ret_point.lon_whole = 0U;
    cfg->ret_point.lon_frac = 0U;
    cfg->ret_point.lat_ns = 0U;
    cfg->ret_point.lat_whole = 0U;
    cfg->ret_point.lat_frac = 0U;
}

static void AutoDriveCfg_WriteU16(u8 *dst, u16 value)
{
    dst[0] = (u8)(value >> 8);
    dst[1] = (u8)(value & 0x00FFU);
}

static u16 AutoDriveCfg_ReadU16(const u8 *src)
{
    return (u16)(((u16)src[0] << 8) | src[1]);
}

static void AutoDriveCfg_Pack(const AutoDrive_ReturnConfig_t *cfg, u8 *buf)
{
    if ((cfg == 0) || (buf == 0)) {
        return;
    }

    buf[0] = cfg->auto_ret_onoff;
    buf[1] = cfg->ret_point.lon_ew;
    AutoDriveCfg_WriteU16(&buf[2], cfg->ret_point.lon_whole);
    AutoDriveCfg_WriteU16(&buf[4], cfg->ret_point.lon_frac);
    buf[6] = cfg->ret_point.lat_ns;
    AutoDriveCfg_WriteU16(&buf[7], cfg->ret_point.lat_whole);
    AutoDriveCfg_WriteU16(&buf[9], cfg->ret_point.lat_frac);
}

static void AutoDriveCfg_Unpack(AutoDrive_ReturnConfig_t *cfg, const u8 *buf)
{
    if ((cfg == 0) || (buf == 0)) {
        return;
    }

    cfg->auto_ret_onoff = buf[0];
    cfg->ret_point.lon_ew = buf[1];
    cfg->ret_point.lon_whole = AutoDriveCfg_ReadU16(&buf[2]);
    cfg->ret_point.lon_frac = AutoDriveCfg_ReadU16(&buf[4]);
    cfg->ret_point.lat_ns = buf[6];
    cfg->ret_point.lat_whole = AutoDriveCfg_ReadU16(&buf[7]);
    cfg->ret_point.lat_frac = AutoDriveCfg_ReadU16(&buf[9]);
}

void AutoDriveCfg_Init(void)
{
    (void)parameter_store_init();
    AutoDriveCfg_Default(&autodrive_cfg_cache);
}

void AutoDriveCfg_Load(AutoDrive_ReturnConfig_t *cfg)
{
    u8 buf[AUTODRIVE_CFG_WIRE_LEN];
    int8 ret;

    if (cfg == 0) {
        return;
    }

    AutoDriveCfg_Default(cfg);
    ret = parameter_store_load_autodrive(buf, AUTODRIVE_CFG_WIRE_LEN);
    if (ret == PARAMETER_STORE_OK) {
        AutoDriveCfg_Unpack(cfg, buf);
        autodrive_cfg_cache = *cfg;
        LOGI("ADCFG", "load ok sw=0x%02X", (u16)cfg->auto_ret_onoff);
        return;
    }

    autodrive_cfg_cache = *cfg;
    LOGW("ADCFG", "load default rc=%d", ret);
}

u8 AutoDriveCfg_Save(const AutoDrive_ReturnConfig_t *cfg)
{
    u8 buf[AUTODRIVE_CFG_WIRE_LEN];
    int8 ret;

    if (cfg == 0) {
        return 0U;
    }

    AutoDriveCfg_Pack(cfg, buf);
    ret = parameter_store_save_autodrive(buf, AUTODRIVE_CFG_WIRE_LEN);
    if (ret == PARAMETER_STORE_OK) {
        autodrive_cfg_cache = *cfg;
        LOGI("ADCFG", "save ok sw=0x%02X", (u16)cfg->auto_ret_onoff);
        return 1U;
    }

    LOGE("ADCFG", "save fail rc=%d", ret);
    return 0U;
}
