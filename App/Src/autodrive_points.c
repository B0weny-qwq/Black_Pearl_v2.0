#include "autodrive_internal.h"

u16 AutoDrive_ReadU16Wire(const u8 *data_m)
{
    return (u16)(((u16)data_m[0] << 8) | data_m[1]);
}

void AutoDrive_PointFromLegacyWire(AutoDrive_PointRaw_t *point,
                                   const u8 *data_m)
{
    if ((point == 0) || (data_m == 0)) {
        return;
    }

    /* 旧 0x13/0x14/0x15 点位格式：E/W + lon + frac + N/S + lat + frac。 */
    point->lon_ew = data_m[0];
    point->lon_whole = AutoDrive_ReadU16Wire(&data_m[1]);
    point->lon_frac = AutoDrive_ReadU16Wire(&data_m[3]);
    point->lat_ns = data_m[5];
    point->lat_whole = AutoDrive_ReadU16Wire(&data_m[6]);
    point->lat_frac = AutoDrive_ReadU16Wire(&data_m[8]);
}

u8 AutoDrive_PointRawValid(const AutoDrive_PointRaw_t *point)
{
    if (point == 0) {
        return 0U;
    }
    if ((point->lon_ew != 'E') && (point->lon_ew != 'W')) {
        return 0U;
    }
    if (point->lon_whole == 0U) {
        return 0U;
    }
    return 1U;
}

void AutoDrive_CopyPoint(AutoDrive_PointRaw_t *dst,
                         const AutoDrive_PointRaw_t *src)
{
    if ((dst == 0) || (src == 0)) {
        return;
    }
    *dst = *src;
}

void AutoDrive_ClearPoint(AutoDrive_PointRaw_t *point)
{
    if (point == 0) {
        return;
    }

    point->lon_ew = 0U;
    point->lon_whole = 0U;
    point->lon_frac = 0U;
    point->lat_ns = 0U;
    point->lat_whole = 0U;
    point->lat_frac = 0U;
}

u8 AutoDrive_PointRawEqual(const AutoDrive_PointRaw_t *lhs,
                           const AutoDrive_PointRaw_t *rhs)
{
    if ((lhs == 0) || (rhs == 0)) {
        return 0U;
    }
    return ((lhs->lon_ew == rhs->lon_ew) &&
            (lhs->lon_whole == rhs->lon_whole) &&
            (lhs->lon_frac == rhs->lon_frac) &&
            (lhs->lat_ns == rhs->lat_ns) &&
            (lhs->lat_whole == rhs->lat_whole) &&
            (lhs->lat_frac == rhs->lat_frac)) ? 1U : 0U;
}

u8 AutoDrive_IsFishRxDuplicate(const AutoDrive_PointRaw_t *point, u32 now_ms)
{
    if ((point == 0) || (last_fish_rx_valid == 0U)) {
        return 0U;
    }
    if (AutoDrive_PointRawEqual(point, &last_fish_rx_point) == 0U) {
        return 0U;
    }
    return ((now_ms - last_fish_rx_ms) < AUTODRIVE_FISH_DUP_WAIT_MS) ? 1U : 0U;
}

void AutoDrive_RecordFishRxPoint(const AutoDrive_PointRaw_t *point, u32 now_ms)
{
    if ((point == 0) || (AutoDrive_PointRawValid(point) == 0U)) {
        return;
    }
    AutoDrive_CopyPoint(&last_fish_rx_point, point);
    last_fish_rx_ms = now_ms;
    last_fish_rx_valid = 1U;
}

void AutoDrive_PointFromGps(AutoDrive_PointRaw_t *point,
                            const board_gps_state_t *gps)
{
    u32 lat_abs;
    u32 lon_abs;
    u32 lat_deg;
    u32 lon_deg;
    u32 lat_min_x1e4;
    u32 lon_min_x1e4;

    if ((point == 0) || (gps == 0)) {
        return;
    }

    /* GPS 内部 deg1e7 快照转换回旧遥控器 ddmm.mmmm 坐标。 */
    lat_abs = (u32)((gps->lat_deg1e7 < 0L) ? -gps->lat_deg1e7 : gps->lat_deg1e7);
    lon_abs = (u32)((gps->lon_deg1e7 < 0L) ? -gps->lon_deg1e7 : gps->lon_deg1e7);

    lat_deg = lat_abs / 10000000UL;
    lon_deg = lon_abs / 10000000UL;
    lat_min_x1e4 = ((lat_abs % 10000000UL) * 6UL + 50UL) / 100UL;
    lon_min_x1e4 = ((lon_abs % 10000000UL) * 6UL + 50UL) / 100UL;

    point->lon_ew = (gps->lon_deg1e7 < 0L) ? 'W' : 'E';
    point->lon_whole = (u16)(lon_deg * 100UL + (lon_min_x1e4 / 10000UL));
    point->lon_frac = (u16)(lon_min_x1e4 % 10000UL);
    point->lat_ns = (gps->lat_deg1e7 < 0L) ? 'S' : 'N';
    point->lat_whole = (u16)(lat_deg * 100UL + (lat_min_x1e4 / 10000UL));
    point->lat_frac = (u16)(lat_min_x1e4 % 10000UL);
}
