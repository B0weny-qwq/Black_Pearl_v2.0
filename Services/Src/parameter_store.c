#include "parameter_store.h"
#include "board_storage.h"

#define PARAMETER_STORE_ADDR_AUTODRIVE  0x0000UL
#define PARAMETER_STORE_ADDR_NCAL_A     0x0200UL
#define PARAMETER_STORE_ADDR_NCAL_B     0x0400UL
#define PARAMETER_STORE_MAGIC0          0x42U
#define PARAMETER_STORE_MAGIC1          0x50U
#define PARAMETER_STORE_VERSION         0x01U
#define PARAMETER_STORE_RECORD_LEN      (5U + PARAMETER_STORE_AUTODRIVE_MAX_LEN)
#define PARAMETER_STORE_NCAL_MAGIC0     0x4EU
#define PARAMETER_STORE_NCAL_MAGIC1     0x43U
#define PARAMETER_STORE_NCAL_VERSION    0x01U
#define PARAMETER_STORE_NCAL_RECORD_LEN (5U + PARAMETER_STORE_NORTH_CALIB_MAX_LEN)

static u8 parameter_store_ready;

static u8 parameter_store_checksum(const u8 *buf, u8 len)
{
    u8 i;
    u8 sum;

    sum = 0U;
    for (i = 0U; i < len; i++) {
        sum ^= buf[i];
    }
    return sum;
}

static u32 parameter_store_ncal_addr(u8 slot)
{
    return (slot == 1U) ? PARAMETER_STORE_ADDR_NCAL_B : PARAMETER_STORE_ADDR_NCAL_A;
}

int8 parameter_store_init(void)
{
    parameter_store_ready = 1U;
    return PARAMETER_STORE_OK;
}

int8 parameter_store_load_autodrive(u8 *buf, u8 len)
{
    u8 record[PARAMETER_STORE_RECORD_LEN];
    u8 checksum;
    u8 i;

    if ((buf == 0) || (len == 0U) || (len > PARAMETER_STORE_AUTODRIVE_MAX_LEN)) {
        return PARAMETER_STORE_ERR_PARAM;
    }
    if (parameter_store_ready == 0U) {
        (void)parameter_store_init();
    }

    if (board_storage_read(PARAMETER_STORE_ADDR_AUTODRIVE,
                           record,
                           PARAMETER_STORE_RECORD_LEN) != BOARD_STORAGE_OK) {
        return PARAMETER_STORE_ERR_IO;
    }

    if ((record[0] != PARAMETER_STORE_MAGIC0) ||
        (record[1] != PARAMETER_STORE_MAGIC1) ||
        (record[2] != PARAMETER_STORE_VERSION)) {
        return PARAMETER_STORE_ERR_EMPTY;
    }
    if (record[3] != len) {
        return PARAMETER_STORE_ERR_VERIFY;
    }

    checksum = parameter_store_checksum(record, (u8)(4U + len));
    if (checksum != record[4U + len]) {
        return PARAMETER_STORE_ERR_VERIFY;
    }

    for (i = 0U; i < len; i++) {
        buf[i] = record[4U + i];
    }
    return PARAMETER_STORE_OK;
}

int8 parameter_store_save_autodrive(const u8 *buf, u8 len)
{
    u8 record[PARAMETER_STORE_RECORD_LEN];
    u8 i;

    if ((buf == 0) || (len == 0U) || (len > PARAMETER_STORE_AUTODRIVE_MAX_LEN)) {
        return PARAMETER_STORE_ERR_PARAM;
    }
    if (parameter_store_ready == 0U) {
        (void)parameter_store_init();
    }

    for (i = 0U; i < PARAMETER_STORE_RECORD_LEN; i++) {
        record[i] = 0xFFU;
    }

    record[0] = PARAMETER_STORE_MAGIC0;
    record[1] = PARAMETER_STORE_MAGIC1;
    record[2] = PARAMETER_STORE_VERSION;
    record[3] = len;
    for (i = 0U; i < len; i++) {
        record[4U + i] = buf[i];
    }
    record[4U + len] = parameter_store_checksum(record, (u8)(4U + len));

    if (board_storage_erase_sector(PARAMETER_STORE_ADDR_AUTODRIVE) != BOARD_STORAGE_OK) {
        return PARAMETER_STORE_ERR_IO;
    }
    if (board_storage_write(PARAMETER_STORE_ADDR_AUTODRIVE,
                            record,
                            PARAMETER_STORE_RECORD_LEN) != BOARD_STORAGE_OK) {
        return PARAMETER_STORE_ERR_IO;
    }

    return PARAMETER_STORE_OK;
}

int8 parameter_store_load_north_calib_slot(u8 slot, u8 *buf, u8 len)
{
    u8 record[PARAMETER_STORE_NCAL_RECORD_LEN];
    u8 checksum;
    u8 i;

    if ((buf == 0) || (len == 0U) || (len > PARAMETER_STORE_NORTH_CALIB_MAX_LEN)) {
        return PARAMETER_STORE_ERR_PARAM;
    }
    if (parameter_store_ready == 0U) {
        (void)parameter_store_init();
    }

    if (board_storage_read(parameter_store_ncal_addr(slot),
                           record,
                           PARAMETER_STORE_NCAL_RECORD_LEN) != BOARD_STORAGE_OK) {
        return PARAMETER_STORE_ERR_IO;
    }

    if ((record[0] != PARAMETER_STORE_NCAL_MAGIC0) ||
        (record[1] != PARAMETER_STORE_NCAL_MAGIC1) ||
        (record[2] != PARAMETER_STORE_NCAL_VERSION)) {
        for (i = 0U; i < len; i++) {
            buf[i] = record[i];
        }
        return PARAMETER_STORE_OK;
    }
    if (record[3] != len) {
        return PARAMETER_STORE_ERR_VERIFY;
    }

    checksum = parameter_store_checksum(record, (u8)(4U + len));
    if (checksum != record[4U + len]) {
        return PARAMETER_STORE_ERR_VERIFY;
    }

    for (i = 0U; i < len; i++) {
        buf[i] = record[4U + i];
    }
    return PARAMETER_STORE_OK;
}

int8 parameter_store_save_north_calib_slot(u8 slot, const u8 *buf, u8 len)
{
    u8 record[PARAMETER_STORE_NCAL_RECORD_LEN];
    u8 verify[PARAMETER_STORE_NCAL_RECORD_LEN];
    u8 i;

    if ((buf == 0) || (len == 0U) || (len > PARAMETER_STORE_NORTH_CALIB_MAX_LEN)) {
        return PARAMETER_STORE_ERR_PARAM;
    }
    if (parameter_store_ready == 0U) {
        (void)parameter_store_init();
    }

    for (i = 0U; i < PARAMETER_STORE_NCAL_RECORD_LEN; i++) {
        record[i] = 0xFFU;
    }

    record[0] = PARAMETER_STORE_NCAL_MAGIC0;
    record[1] = PARAMETER_STORE_NCAL_MAGIC1;
    record[2] = PARAMETER_STORE_NCAL_VERSION;
    record[3] = len;
    for (i = 0U; i < len; i++) {
        record[4U + i] = buf[i];
    }
    record[4U + len] = parameter_store_checksum(record, (u8)(4U + len));

    if (board_storage_erase_sector(parameter_store_ncal_addr(slot)) != BOARD_STORAGE_OK) {
        return PARAMETER_STORE_ERR_IO;
    }
    if (board_storage_write(parameter_store_ncal_addr(slot),
                            record,
                            PARAMETER_STORE_NCAL_RECORD_LEN) != BOARD_STORAGE_OK) {
        return PARAMETER_STORE_ERR_IO;
    }
    if (board_storage_read(parameter_store_ncal_addr(slot),
                           verify,
                           PARAMETER_STORE_NCAL_RECORD_LEN) != BOARD_STORAGE_OK) {
        return PARAMETER_STORE_ERR_IO;
    }
    for (i = 0U; i < PARAMETER_STORE_NCAL_RECORD_LEN; i++) {
        if (verify[i] != record[i]) {
            return PARAMETER_STORE_ERR_VERIFY;
        }
    }
    return PARAMETER_STORE_OK;
}
