#include "autodrive_internal.h"

/* GPS ready 门槛保持旧工程的卫星数和非零坐标判断。 */
void AutoDrive_GetCurrentPointRaw(AutoDrive_PointRaw_t *point)
{
    const board_gps_state_t *gps;

    gps = board_gps_get_state();
    if ((point == 0) || (gps == 0)) {
        return;
    }
    AutoDrive_PointFromGps(point, gps);
}

u8 AutoDrive_GpsReady(void)
{
    const board_gps_state_t *gps;
    u8 sat_count;

    gps = board_gps_get_state();
    if (gps == 0) {
        return 0U;
    }
    sat_count = (gps->satellites_used_gsa > 0U) ?
                gps->satellites_used_gsa :
                gps->satellites_used;
    if (sat_count < 7U) {
        return 0U;
    }
    if ((gps->lat_deg1e7 == 0L) || (gps->lon_deg1e7 == 0L)) {
        return 0U;
    }
    return 1U;
}

u8 AutoDrive_GetSatCount(void)
{
    const board_gps_state_t *gps;

    gps = board_gps_get_state();
    if (gps == 0) {
        return 0U;
    }
    if (gps->satellites_used_gsa > 0U) {
        return gps->satellites_used_gsa;
    }
    return gps->satellites_used;
}

u8 AutoDrive_GetReadyCurrentPoint(AutoDrive_PointRaw_t *point)
{
    const board_gps_state_t *gps;

    if (point == 0) {
        return 0U;
    }
    if (AutoDrive_GpsReady() == 0U) {
        return 0U;
    }
    gps = board_gps_get_state();
    if (gps == 0) {
        return 0U;
    }
    AutoDrive_PointFromGps(point, gps);
    return AutoDrive_PointRawValid(point);
}
