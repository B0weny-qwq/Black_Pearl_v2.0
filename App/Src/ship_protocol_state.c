#include "ship_protocol_internal.h"

ship_protocol_runtime_t ship_protocol_rt;
u8 ship_protocol_initialized;
u32 ship_protocol_last_tick_ms;
u8 ship_protocol_parse_buffer[SHIP_PROTO_MAX_FRAME_LEN];
u8 ship_protocol_tx_frame[SHIP_PROTO_MAX_FRAME_LEN];
u8 ship_protocol_rx_payload[BOARD_WIRELESS_MAX_PAYLOAD_LEN];
u8 ship_protocol_gps_payload[SHIP_GPS_REPORT_PAYLOAD_LEN];
u8 ship_protocol_diag_payload[SHIP_AUTODRIVE_DIAG_PAYLOAD_LEN];
u8 ship_protocol_parse_index;
u8 ship_protocol_parse_expected_len;
ship_protocol_parse_state_t ship_protocol_parse_state;
ship_protocol_pair_params_t ship_protocol_pair_params;

/* 旧遥控帧工具：所有 AA..BB 组包/拆包共用这些整数函数。 */
u8 ship_protocol_xor(const u8 *buf, u8 len)
{
    u8 i;
    u8 value;

    value = 0U;
    for (i = 0U; i < len; i++) {
        value ^= buf[i];
    }
    return value;
}

void ship_protocol_get_pair_seed(u8 *seed)
{
    if (seed == 0) {
        return;
    }
    seed[0] = SHIP_PAIR_SEED0;
    seed[1] = SHIP_PAIR_SEED1;
    seed[2] = SHIP_PAIR_SEED2;
    seed[3] = SHIP_PAIR_SEED3;
}

void ship_protocol_calc_pair_params(ship_protocol_pair_params_t *params)
{
    u8 key0;
    u8 key1;
    u8 channel;

    if (params == 0) {
        return;
    }
    ship_protocol_get_pair_seed(params->seed);
    key0 = (u8)((params->seed[0] & 0x0FU) +
                ((params->seed[3] >> 2) + (params->seed[3] % 0x03U)));
    key1 = (u8)((params->seed[1] & 0x0FU) +
                ((params->seed[2] >> 3) + (params->seed[0] % 0x06U)));
    channel = (u8)((((params->seed[3] + 0x06U) % 0x40U) +
                    ((params->seed[2] >> 3) * 0x08U) +
                    (((params->seed[1] | params->seed[0]) % 0x08U) / 2U)) % 0x40U);
    params->work_rx_channel = channel;
    params->work_tx_channel = (u8)(channel + 0x40U);
    params->key0 = key0;
    params->key1 = key1;
    params->reg36 = (u16)(((u16)key0 << 8) | key0);
    params->reg39 = (u16)(((u16)key1 << 8) | key1);
}

void ship_protocol_apply_default_rf(void)
{
    ship_protocol_calc_pair_params(&ship_protocol_pair_params);
    ship_protocol_rt.rf_channel[0] = ship_protocol_pair_params.work_rx_channel;
    ship_protocol_rt.rf_channel[1] = ship_protocol_pair_params.work_rx_channel;
    ship_protocol_rt.rf_channel[2] = ship_protocol_pair_params.work_tx_channel;
    ship_protocol_rt.rf_send_key[0] = ship_protocol_pair_params.key0;
    ship_protocol_rt.rf_send_key[1] = ship_protocol_pair_params.key1;
}

void ship_protocol_put_u16_be(u8 *buf, u16 value)
{
    buf[0] = (u8)(value >> 8);
    buf[1] = (u8)(value & 0x00FFU);
}

u16 ship_protocol_read_u16_be(const u8 *buf)
{
    return (u16)(((u16)buf[0] << 8) | buf[1]);
}

u32 ship_protocol_abs_int32(int32 value)
{
    if (value < 0L) {
        return (u32)(0L - value);
    }
    return (u32)value;
}

void ship_protocol_to_legacy_nmea_coord(u32 abs_deg1e7, u16 *coord1, u16 *coord2)
{
    u32 degrees;
    u32 minutes_scaled1e4;

    degrees = abs_deg1e7 / 10000000UL;
    minutes_scaled1e4 = (((abs_deg1e7 % 10000000UL) * 6UL) + 50UL) / 100UL;
    if (minutes_scaled1e4 >= 600000UL) {
        degrees++;
        minutes_scaled1e4 = 0UL;
    }
    *coord1 = (u16)((degrees * 100UL) + (minutes_scaled1e4 / 10000UL));
    *coord2 = (u16)(minutes_scaled1e4 % 10000UL);
}
