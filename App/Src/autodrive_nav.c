#include "autodrive_internal.h"

u32 AutoDrive_Abs32Diff(u32 lhs, u32 rhs)
{
    if (lhs >= rhs) {
        return lhs - rhs;
    }
    return rhs - lhs;
}

u16 AutoDrive_Atan01Deg(u16 z_q10)
{
    u32 z;
    u32 curve;
    u32 angle_deg100;

    z = z_q10;
    if (z > AUTODRIVE_ATAN_Q10) {
        z = AUTODRIVE_ATAN_Q10;
    }

    /* 保留旧工程整数近似曲线，避免引入 atan 浮点库。 */
    curve = 4500UL + ((1564UL * (AUTODRIVE_ATAN_Q10 - z)) / AUTODRIVE_ATAN_Q10);
    angle_deg100 = (z * curve) / AUTODRIVE_ATAN_Q10;
    return (u16)((angle_deg100 + 50UL) / 100UL);
}

u8 AutoDrive_GetDirectionNowToDestination(const u8 *nowpositionData,
                                          const u8 *despositionData)
{
    const AutoDrive_PointRaw_t *nowposition;
    const AutoDrive_PointRaw_t *desposition;
    u8 direction;

    nowposition = (const AutoDrive_PointRaw_t *)nowpositionData;
    desposition = (const AutoDrive_PointRaw_t *)despositionData;
    direction = 0U;

    if ((nowposition->lon_whole > desposition->lon_whole) ||
        ((nowposition->lon_whole == desposition->lon_whole) &&
         (nowposition->lon_frac > desposition->lon_frac))) {
        direction = POSITION_WEST;
        if ((nowposition->lat_whole > desposition->lat_whole) ||
            ((nowposition->lat_whole == desposition->lat_whole) &&
             (nowposition->lat_frac > desposition->lat_frac))) {
            direction = POSITION_WEST_SOUTH;
        } else if ((nowposition->lat_whole < desposition->lat_whole) ||
                   ((nowposition->lat_whole == desposition->lat_whole) &&
                    (nowposition->lat_frac < desposition->lat_frac))) {
            direction = POSITION_WEST_NORTH;
        }
    }

    if ((nowposition->lon_whole < desposition->lon_whole) ||
        ((nowposition->lon_whole == desposition->lon_whole) &&
         (nowposition->lon_frac < desposition->lon_frac))) {
        direction = POSITION_EAST;
        if ((nowposition->lat_whole > desposition->lat_whole) ||
            ((nowposition->lat_whole == desposition->lat_whole) &&
             (nowposition->lat_frac > desposition->lat_frac))) {
            direction = POSITION_EAST_SOUTH;
        } else if ((nowposition->lat_whole < desposition->lat_whole) ||
                   ((nowposition->lat_whole == desposition->lat_whole) &&
                    (nowposition->lat_frac < desposition->lat_frac))) {
            direction = POSITION_EAST_NORTH;
        }
    }

    if (direction == 0U) {
        if ((nowposition->lat_whole > desposition->lat_whole) ||
            ((nowposition->lat_whole == desposition->lat_whole) &&
             (nowposition->lat_frac > desposition->lat_frac))) {
            direction = POSITION_SOUTH;
        } else {
            direction = POSITION_NORTH;
        }
    }
    return direction;
}

static u32 AutoDrive_CalDistanceLon(const AutoDrive_PointRaw_t *now,
                                    const AutoDrive_PointRaw_t *des)
{
    u16 dd1;
    u16 dd2;
    u32 sec1;
    u32 sec2;
    u16 ddiff;
    u32 sec_diff;

    dd1 = now->lon_whole / 100U;
    dd2 = des->lon_whole / 100U;
    sec1 = ((u32)(now->lon_whole % 100U) * AUTODRIVE_MINUTE_SCALE) + (u32)now->lon_frac;
    sec2 = ((u32)(des->lon_whole % 100U) * AUTODRIVE_MINUTE_SCALE) + (u32)des->lon_frac;

    if (dd1 == dd2) {
        sec_diff = AutoDrive_Abs32Diff(sec1, sec2);
        return (sec_diff * AUTODRIVE_METERS_PER_MINUTE) / AUTODRIVE_MINUTE_SCALE;
    }

    if (dd1 > dd2) {
        ddiff = (u16)(dd1 - dd2);
        if (sec1 > sec2) {
            sec_diff = sec1 - sec2;
        } else {
            ddiff--;
            sec1 += (AUTODRIVE_MINUTES_PER_DEG * AUTODRIVE_MINUTE_SCALE);
            sec_diff = sec1 - sec2;
        }
    } else {
        ddiff = (u16)(dd2 - dd1);
        if (sec2 > sec1) {
            sec_diff = sec2 - sec1;
        } else {
            ddiff--;
            sec2 += (AUTODRIVE_MINUTES_PER_DEG * AUTODRIVE_MINUTE_SCALE);
            sec_diff = sec2 - sec1;
        }
    }

    if (ddiff != 0U) {
        return ((u32)ddiff * AUTODRIVE_METERS_PER_DEG) +
               ((sec_diff * AUTODRIVE_METERS_PER_MINUTE) / AUTODRIVE_MINUTE_SCALE);
    }
    return (sec_diff * AUTODRIVE_METERS_PER_MINUTE) / AUTODRIVE_MINUTE_SCALE;
}

static u32 AutoDrive_CalDistanceLat(const AutoDrive_PointRaw_t *now,
                                    const AutoDrive_PointRaw_t *des)
{
    u16 dd1;
    u16 dd2;
    u32 sec1;
    u32 sec2;
    u16 ddiff;
    u32 sec_diff;

    dd1 = now->lat_whole / 100U;
    dd2 = des->lat_whole / 100U;
    sec1 = ((u32)(now->lat_whole % 100U) * AUTODRIVE_MINUTE_SCALE) + (u32)now->lat_frac;
    sec2 = ((u32)(des->lat_whole % 100U) * AUTODRIVE_MINUTE_SCALE) + (u32)des->lat_frac;

    if (dd1 == dd2) {
        sec_diff = AutoDrive_Abs32Diff(sec1, sec2);
        return (sec_diff * AUTODRIVE_METERS_PER_MINUTE) / AUTODRIVE_MINUTE_SCALE;
    }

    if (dd1 > dd2) {
        ddiff = (u16)(dd1 - dd2);
        if (sec1 > sec2) {
            sec_diff = sec1 - sec2;
        } else {
            ddiff--;
            sec1 += (AUTODRIVE_MINUTES_PER_DEG * AUTODRIVE_MINUTE_SCALE);
            sec_diff = sec1 - sec2;
        }
    } else {
        ddiff = (u16)(dd2 - dd1);
        if (sec2 > sec1) {
            sec_diff = sec2 - sec1;
        } else {
            ddiff--;
            sec2 += (AUTODRIVE_MINUTES_PER_DEG * AUTODRIVE_MINUTE_SCALE);
            sec_diff = sec2 - sec1;
        }
    }

    if (ddiff != 0U) {
        return ((u32)ddiff * AUTODRIVE_METERS_PER_DEG) +
               ((sec_diff * AUTODRIVE_METERS_PER_MINUTE) / AUTODRIVE_MINUTE_SCALE);
    }
    return (sec_diff * AUTODRIVE_METERS_PER_MINUTE) / AUTODRIVE_MINUTE_SCALE;
}

u16 AutoDrive_GetAngelNowToDestination(const u8 *nowpositionData,
                                       const u8 *despositionData)
{
    const AutoDrive_PointRaw_t *nowposition;
    const AutoDrive_PointRaw_t *desposition;
    u32 distance_width;
    u32 distance_height;
    u32 z_q10;
    u16 base;

    nowposition = (const AutoDrive_PointRaw_t *)nowpositionData;
    desposition = (const AutoDrive_PointRaw_t *)despositionData;
    distance_width = AutoDrive_CalDistanceLon(nowposition, desposition);
    distance_height = AutoDrive_CalDistanceLat(nowposition, desposition);

    if (distance_width == 0UL) {
        if (distance_height == 0UL) {
            return 65535U;
        }
        return 90U;
    }
    if (distance_height <= distance_width) {
        z_q10 = ((distance_height << 10) + (distance_width >> 1)) / distance_width;
        return AutoDrive_Atan01Deg((u16)z_q10);
    }
    z_q10 = ((distance_width << 10) + (distance_height >> 1)) / distance_height;
    base = AutoDrive_Atan01Deg((u16)z_q10);
    return (u16)(90U - base);
}

u16 AutoDrive_GetDistanceNowToDestination(const u8 *nowpositionData,
                                          const u8 *despositionData)
{
    const AutoDrive_PointRaw_t *nowposition;
    const AutoDrive_PointRaw_t *desposition;
    u32 distance_width;
    u32 distance_height;
    u32 max_distance;
    u32 min_distance;
    u32 distance;

    nowposition = (const AutoDrive_PointRaw_t *)nowpositionData;
    desposition = (const AutoDrive_PointRaw_t *)despositionData;
    distance_width = AutoDrive_CalDistanceLon(nowposition, desposition);
    distance_height = AutoDrive_CalDistanceLat(nowposition, desposition);
    if (distance_width >= distance_height) {
        max_distance = distance_width;
        min_distance = distance_height;
    } else {
        max_distance = distance_height;
        min_distance = distance_width;
    }
    distance = max_distance + ((min_distance * 3UL) >> 3);
    if (distance > 65535UL) {
        return 65535U;
    }
    return (u16)distance;
}

u16 AutoDrive_GetNorthAngel(u8 direction, u8 angel)
{
    switch (direction) {
    case POSITION_NORTH:
        return 0U;
    case POSITION_EAST_NORTH:
        return (u16)(90U - angel);
    case POSITION_EAST:
        return 90U;
    case POSITION_EAST_SOUTH:
        return (u16)(90U + angel);
    case POSITION_SOUTH:
        return 180U;
    case POSITION_WEST_SOUTH:
        return (u16)(270U - angel);
    case POSITION_WEST:
        return 270U;
    case POSITION_WEST_NORTH:
        return (u16)(270U + angel);
    default:
        return 0U;
    }
}
