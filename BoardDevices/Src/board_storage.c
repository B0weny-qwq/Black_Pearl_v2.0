#include "board_storage.h"
#include "STC32G_EEPROM.h"

int8 board_storage_read(u32 address, u8 *buf, u16 len)
{
    if ((buf == 0) || (len == 0U)) {
        return BOARD_STORAGE_ERR_PARAM;
    }

    EEPROM_read_n(address, buf, len);
    return BOARD_STORAGE_OK;
}

int8 board_storage_write(u32 address, const u8 *buf, u16 len)
{
    if ((buf == 0) || (len == 0U)) {
        return BOARD_STORAGE_ERR_PARAM;
    }

    EEPROM_write_n(address, (u8 *)buf, len);
    return BOARD_STORAGE_OK;
}

int8 board_storage_erase_sector(u32 address)
{
    EEPROM_SectorErase(address);
    return BOARD_STORAGE_OK;
}
