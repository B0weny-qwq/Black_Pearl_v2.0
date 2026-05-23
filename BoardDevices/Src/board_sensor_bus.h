#ifndef __BOARD_SENSOR_BUS_H__
#define __BOARD_SENSOR_BUS_H__

#include "type_def.h"

int8 board_sensor_bus_init(void);
int8 board_sensor_bus_write_reg(u8 dev_addr, u8 reg_addr, u8 value);
int8 board_sensor_bus_read_regs(u8 dev_addr, u8 start_reg, u8 *buf, u8 len);
void board_sensor_bus_delay_ms(u16 ms);

#endif
