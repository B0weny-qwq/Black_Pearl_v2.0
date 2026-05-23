#include "board_sensor_bus.h"
#include "ef_iic.h"
#include "STC32G_Delay.h"

static u8 board_sensor_bus_ready = 0U;

int8 board_sensor_bus_init(void)
{
    ef_iic_config_t config;
    int8 ret;

    if (board_sensor_bus_ready != 0U) {
        return EF_IIC_OK;
    }

    config.pin_group = EF_IIC_PIN_P14_P15;
    config.speed = EF_IIC_SPEED_400K;
    config.timeout = 0U;

    ret = ef_iic_init(&config);
    if (ret == EF_IIC_OK) {
        board_sensor_bus_ready = 1U;
    }

    return ret;
}

int8 board_sensor_bus_write_reg(u8 dev_addr, u8 reg_addr, u8 value)
{
    return ef_iic_write_regs(dev_addr, reg_addr, &value, 1U);
}

int8 board_sensor_bus_read_regs(u8 dev_addr, u8 start_reg, u8 *buf, u8 len)
{
    return ef_iic_read_regs(dev_addr, start_reg, buf, len);
}

void board_sensor_bus_delay_ms(u16 ms)
{
    delay_ms(ms);
}
