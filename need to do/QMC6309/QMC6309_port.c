#include "QMC6309_port.h"

s8 QMC6309Port_Init(void)
{
    return QMI8658Port_Init();
}

char *QMC6309Port_BackendName(void)
{
    return QMI8658Port_BackendName();
}

void QMC6309Port_DelayMs(u16 ms)
{
    QMI8658Port_DelayMs(ms);
}

u8 QMC6309Port_BusNeedsRecover(void)
{
    return QMI8658Port_BusNeedsRecover();
}

void QMC6309Port_BusRecover(void)
{
    QMI8658Port_BusRecover();
}

u8 QMC6309Port_WriteReg(u8 addr, u8 reg_addr, u8 reg_val, u8 *err_code)
{
    return QMI8658Port_WriteReg(addr, reg_addr, reg_val, err_code);
}

u8 QMC6309Port_ReadN(u8 addr, u8 start_reg, u8 *buf, u8 len, u8 *err_code)
{
    return QMI8658Port_ReadN(addr, start_reg, buf, len, err_code);
}
