/**
 * @file    wireless_port.c
 * @brief   无线模块板级端口适配实现。
 * @author  boweny
 * @date    2026-05-06
 * @version v1.1
 *
 * @details
 * 本文件集中管理 LT8920/KCT8206L 所需的 GPIO、SPI、复位、天线选择、
 * RXEN/TXEN 和延时接口。芯片层只通过本文件访问硬件引脚，避免
 * 业务协议代码直接操作 `Pxx` 端口。
 *
 * @note
 * 默认引脚以当前 Black Pearl v1.1 硬件为准：
 * SCLK=P3.2、MISO=P3.3、MOSI=P3.4、CS=P3.5、
 * RST=P5.0、ANT_SEL=P5.1、RXEN=P1.3、TXEN=P5.4。
 */
#include "wireless_port.h"
#include "..\..\..\Driver\inc\STC32G_Delay.h"
#include "..\..\..\Driver\inc\STC32G_GPIO.h"
#include "..\..\..\Driver\inc\STC32G_NVIC.h"
#include "..\..\..\Driver\inc\STC32G_SPI.h"
#include "..\..\..\Driver\inc\STC32G_Switch.h"

static u8 g_wireless_port_initialized = 0U;


static void WirelessPort_SoftSpiClock(u8 level)
{
    P32 = (level != 0U) ? 1 : 0;
}

static void WirelessPort_SoftSpiMosi(u8 level)
{
    P34 = (level != 0U) ? 1 : 0;
}

s8 WirelessPort_Init(void)
{
#if !WIRELESS_SPI_USE_SOFT
    SPI_InitTypeDef spi_init;
#endif

    EAXSFR();

#if !WIRELESS_FRONTEND_BYPASS_TEST
    P1_MODE_OUT_PP(GPIO_Pin_3);
    P1_PULL_UP_DISABLE(GPIO_Pin_3);
    P1_SPEED_HIGH(GPIO_Pin_3);
#endif

    P3_MODE_OUT_PP(GPIO_Pin_2 | GPIO_Pin_4 | GPIO_Pin_5);
    P3_MODE_IN_HIZ(GPIO_Pin_3);
    P3_PULL_UP_ENABLE(GPIO_Pin_3 | GPIO_Pin_5);
    P3_SPEED_HIGH(GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5);

    P5_MODE_OUT_PP(GPIO_Pin_0);
    P5_PULL_UP_DISABLE(GPIO_Pin_0);
    P5_SPEED_HIGH(GPIO_Pin_0);
#if !WIRELESS_FRONTEND_BYPASS_TEST
    P5_MODE_OUT_PP(GPIO_Pin_1 | GPIO_Pin_4);
    P5_PULL_UP_DISABLE(GPIO_Pin_1 | GPIO_Pin_4);
    P5_SPEED_HIGH(GPIO_Pin_1 | GPIO_Pin_4);
#endif

    WirelessPort_SetCs(1U);
    WirelessPort_SetRst(1U);
#if !WIRELESS_FRONTEND_BYPASS_TEST
    WirelessPort_SetTxEn(0U);
    WirelessPort_SetRxEn(0U);
    WirelessPort_SetAntSel(WIRELESS_PORT_ANT1);
#endif

#if WIRELESS_SPI_USE_SOFT
    WirelessPort_SoftSpiClock(0U);
    WirelessPort_SoftSpiMosi(0U);
#else
    SPI_SW(SPI_P35_P34_P33_P32);

    spi_init.SPI_Enable = ENABLE;
    spi_init.SPI_SSIG = ENABLE;
    spi_init.SPI_FirstBit = SPI_MSB;
    spi_init.SPI_Mode = SPI_Mode_Master;
    spi_init.SPI_CPOL = SPI_CPOL_Low;
    spi_init.SPI_CPHA = SPI_CPHA_2Edge;
    spi_init.SPI_Speed = WIRELESS_HW_SPI_SPEED_CFG;
    SPI_Init(&spi_init);
    NVIC_SPI_Init(DISABLE, Priority_0);
#endif

    g_wireless_port_initialized = 1U;
    return SUCCESS;
}

s8 WirelessPort_Deinit(void)
{
    if (!g_wireless_port_initialized) {
        return SUCCESS;
    }

    WirelessPort_SetTxEn(0U);
    WirelessPort_SetRxEn(0U);
    WirelessPort_SetCs(1U);
    g_wireless_port_initialized = 0U;
    return SUCCESS;
}

void WirelessPort_SetCs(u8 level)
{
    P35 = (level != 0U) ? 1 : 0;
}

void WirelessPort_SetRst(u8 level)
{
    P50 = (level != 0U) ? 1 : 0;
}

#if !WIRELESS_FRONTEND_BYPASS_TEST
void WirelessPort_SetAntSel(u8 ant_sel)
{
    P51 = (ant_sel == WIRELESS_PORT_ANT2) ? 1 : 0;
}

void WirelessPort_SetRxEn(u8 level)
{
    P13 = (level != 0U) ? 1 : 0;
}

void WirelessPort_SetTxEn(u8 level)
{
    P54 = (level != 0U) ? 1 : 0;
}
#else
void WirelessPort_SetAntSel(u8 ant_sel)
{
}

void WirelessPort_SetRxEn(u8 level)
{
}

void WirelessPort_SetTxEn(u8 level)
{
}
#endif

u8 WirelessPort_GetAntSel(void)
{
    return (P51 != 0U) ? 1U : 0U;
}

u8 WirelessPort_GetRxEn(void)
{
    return (P13 != 0U) ? 1U : 0U;
}

u8 WirelessPort_GetTxEn(void)
{
    return (P54 != 0U) ? 1U : 0U;
}

void WirelessPort_DelayMs(u16 ms)
{
    delay_ms(ms);
}

void WirelessPort_DelayUs(u16 us)
{
    u8 dly;

    while (us > 0U) {
        dly = (u8)(MAIN_Fosc / 2000000UL);
        while (--dly) {
        }
        us--;
    }
}

u8 WirelessPort_SpiTransfer(u8 value)
{
#if WIRELESS_SPI_USE_SOFT
    u8 bit_mask;
    u8 recv;

    recv = 0U;
    for (bit_mask = 0x80U; bit_mask != 0U; bit_mask >>= 1) {
        WirelessPort_SoftSpiClock(1U);
        WirelessPort_SoftSpiMosi((value & bit_mask) ? 1U : 0U);
#if (WIRELESS_SOFT_SPI_DELAY_US > 0)
        WirelessPort_DelayUs(WIRELESS_SOFT_SPI_DELAY_US);
#endif
        WirelessPort_SoftSpiClock(0U);
        if (P33 != 0U) {
            recv |= bit_mask;
        }
#if (WIRELESS_SOFT_SPI_DELAY_US > 0)
        WirelessPort_DelayUs(WIRELESS_SOFT_SPI_DELAY_US);
#endif
    }
    return recv;
#else
    SPDAT = value;
    while (SPIF == 0) {
    }
    SPI_ClearFlag();
    return SPDAT;
#endif
}



