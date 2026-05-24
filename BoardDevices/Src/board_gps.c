#include "board_gps.h"
#include "ef_board_resources.h"
#include "gnss_nmea.h"
#include "ef_uart.h"
#include "STC32G_GPIO.h"
#include "STC32G_Switch.h"

static gnss_nmea_t board_gps_parser;
static board_gps_state_t board_gps_state;
static u8 board_gps_ready = 0U;
static u8 board_gps_rx_read_index = 0U;

static void board_gps_sync_state(void);

static void board_gps_sync_state(void)
{
    const gnss_nmea_state_t *state;

    state = gnss_nmea_get_state(&board_gps_parser);
    if (state == 0) {
        return;
    }

    board_gps_state.talker[0] = state->talker[0];
    board_gps_state.talker[1] = state->talker[1];
    board_gps_state.talker[2] = state->talker[2];
    board_gps_state.talker_id = state->talker_id;
    board_gps_state.fix_valid = state->fix_valid;
    board_gps_state.fix_quality = state->fix_quality;
    board_gps_state.rmc_status = state->rmc_status;
    board_gps_state.fix_mode = state->fix_mode;

    board_gps_state.utc_hour = state->utc_hour;
    board_gps_state.utc_minute = state->utc_minute;
    board_gps_state.utc_second = state->utc_second;
    board_gps_state.utc_millisecond = state->utc_millisecond;

    board_gps_state.date_day = state->date_day;
    board_gps_state.date_month = state->date_month;
    board_gps_state.date_year = state->date_year;

    board_gps_state.lat_deg1e7 = state->lat_deg1e7;
    board_gps_state.lon_deg1e7 = state->lon_deg1e7;
    board_gps_state.legacy_coord_valid = state->legacy_coord_valid;
    board_gps_state.legacy_lat_dir = state->legacy_lat_dir;
    board_gps_state.legacy_lon_dir = state->legacy_lon_dir;
    board_gps_state.legacy_lat1 = state->legacy_lat1;
    board_gps_state.legacy_lat2 = state->legacy_lat2;
    board_gps_state.legacy_lon1 = state->legacy_lon1;
    board_gps_state.legacy_lon2 = state->legacy_lon2;

    board_gps_state.speed_knots_x100 = state->speed_knots_x100;
    board_gps_state.speed_kmh_x100 = state->speed_kmh_x100;
    board_gps_state.course_deg_x100 = state->course_deg_x100;

    board_gps_state.satellites_used = state->satellites_used;
    board_gps_state.satellites_used_gsa = state->satellites_used_gsa;
    board_gps_state.satellites_view = state->satellites_view;
    board_gps_state.hdop_x100 = state->hdop_x100;
    board_gps_state.pdop_x100 = state->pdop_x100;
    board_gps_state.vdop_x100 = state->vdop_x100;
    board_gps_state.altitude_cm = state->altitude_cm;
    board_gps_state.max_snr = state->max_snr;

    board_gps_state.update_sequence = state->update_sequence;
    board_gps_state.sentence_ok_count = state->sentence_ok_count;
    board_gps_state.checksum_error_count = state->checksum_error_count;
    board_gps_state.parse_error_count = state->parse_error_count;
    board_gps_state.uart_overflow_count = state->source_overflow_count;
    board_gps_state.fifo_overflow_count = state->fifo_overflow_count;
    board_gps_state.sentence_overflow_count = state->sentence_overflow_count;
}

int8 board_gps_init(void)
{
    ef_uart_config_t config;

    P1_MODE_IN_HIZ(EF_BOARD_GNSS_RX_PIN_MASK);
    P1_MODE_IO_PU(EF_BOARD_GNSS_TX_PIN_MASK);
    UART2_SW(EF_BOARD_GNSS_UART_MUX);

    config.port = EF_BOARD_GNSS_UART_PORT;
    config.baudrate = EF_BOARD_GNSS_UART_BAUDRATE;
    config.rx_enable = ENABLE;

    if (ef_uart_init(&config) != SUCCESS) {
        board_gps_ready = 0U;
        return BOARD_GPS_ERR_DRIVER;
    }

    gnss_nmea_init(&board_gps_parser);
    board_gps_sync_state();
    board_gps_rx_read_index = 0U;
    board_gps_ready = 1U;
    return BOARD_GPS_OK;
}

void board_gps_reset(void)
{
    if (board_gps_ready == 0U) {
        return;
    }

    gnss_nmea_reset(&board_gps_parser);
    board_gps_rx_read_index = 0U;
    board_gps_sync_state();
}

void board_gps_poll(void)
{
    ef_uart_rx_view_t view;
    u8 rx_byte;

    if (board_gps_ready == 0U) {
        return;
    }
    if (ef_uart_get_rx_view(EF_BOARD_GNSS_UART_PORT, &view) != SUCCESS) {
        return;
    }
    if (view.rx_buffer_size == 0U) {
        return;
    }

    if (view.write_index < board_gps_rx_read_index) {
        while (board_gps_rx_read_index < view.rx_buffer_size) {
            if (ef_uart_read_rx_byte(EF_BOARD_GNSS_UART_PORT, board_gps_rx_read_index, &rx_byte) != SUCCESS) {
                break;
            }
            (void)gnss_nmea_feed_byte(&board_gps_parser, rx_byte);
            board_gps_rx_read_index++;
        }
        board_gps_rx_read_index = 0U;
        gnss_nmea_note_source_overflow(&board_gps_parser);
    }

    while (board_gps_rx_read_index != view.write_index) {
        if (ef_uart_read_rx_byte(EF_BOARD_GNSS_UART_PORT, board_gps_rx_read_index, &rx_byte) != SUCCESS) {
            break;
        }
        (void)gnss_nmea_feed_byte(&board_gps_parser, rx_byte);
        board_gps_rx_read_index++;
    }

    board_gps_sync_state();
}

u8 board_gps_is_ready(void)
{
    return board_gps_ready;
}

const board_gps_state_t *board_gps_get_state(void)
{
    if (board_gps_ready == 0U) {
        return 0;
    }

    return &board_gps_state;
}
