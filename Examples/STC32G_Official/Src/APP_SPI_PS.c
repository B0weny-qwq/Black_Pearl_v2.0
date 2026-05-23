/*---------------------------------------------------------------------*/
/* --- Web: www.STCAI.com ---------------------------------------------*/
/*---------------------------------------------------------------------*/

#include	"APP.h"
#include	"STC32G_GPIO.h"
#include	"STC32G_SPI.h"
#include	"STC32G_UART.h"
#include	"STC32G_NVIC.h"

/*************	功能说明	**************

通过串口发送数据给MCU1，MCU1将接收到的数据由SPI发送给MCU2，MCU2再通过串口发送出去.

设置方法 2：
两个设备初始化时都设置 SSIG 为 0，MSTR 设置为0，此时两个设备都是不忽略 SS 的从机模式。
当其中一个设备需要启动传输时，先检测 SS 管脚的电平，如果时候高电平，
就将自己设置成忽略 SS 的主模式，自己的 SS 脚输出低电平，拉低对方的 SS 脚，即可进行数据传输。

         MCU1                          MCU2
  |-----------------|           |-----------------|
  |            MISO |-----------| MISO            |
--| TX         MOSI |-----------| MOSI         TX |--
  |            SCLK |-----------| SCLK            |
--| RX           SS |-----------| SS           RX |--
  |-----------------|           |-----------------|


下载时, 选择时钟 24MHz (可以在配置文件"config.h"中修改).

******************************************/

//========================================================================
//                               本地常量声明	
//========================================================================


//========================================================================
//                               本地变量声明
//========================================================================

bit UartReceived=0;

//========================================================================
//                               本地函数声明
//========================================================================


//========================================================================
//                            外部函数和变量声明
//========================================================================


//========================================================================
// 函数: SPI_PS_init
// 描述: 用户初始化程序.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2020-09-27
//========================================================================
void SPI_PS_init(void)
{
	SPI_InitTypeDef		SPI_InitStructure;
	COMx_InitDefine		COMx_InitStructure;					//结构定义

	COMx_InitStructure.UART_Mode      = UART_8bit_BRTx;		//模式,   UART_ShiftRight,UART_8bit_BRTx,UART_9bit,UART_9bit_BRTx
//	COMx_InitStructure.UART_BRT_Use   = BRT_Timer2;			//选择波特率发生器, BRT_Timer2 (注意: 串口2固定使用BRT_Timer2, 所以不用选择)
	COMx_InitStructure.UART_BaudRate  = 115200ul;			//波特率,     110 ~ 115200
	COMx_InitStructure.UART_RxEnable  = ENABLE;				//接收允许,   ENABLE 或 DISABLE
	UART_Configuration(UART2, &COMx_InitStructure);		//初始化串口2 UART1,UART2,UART3,UART4
	NVIC_UART2_Init(ENABLE,Priority_1);		//中断使能, ENABLE/DISABLE; 优先级(低到高) Priority_0,Priority_1,Priority_2,Priority_3

	SPI_InitStructure.SPI_Enable    = ENABLE;				//SPI启动    ENABLE, DISABLE
	SPI_InitStructure.SPI_SSIG      = DISABLE;				//片选位     ENABLE(忽略SS引脚功能), DISABLE(SS确定主机从机)
	SPI_InitStructure.SPI_FirstBit  = SPI_MSB;				//移位方向   SPI_MSB, SPI_LSB
	SPI_InitStructure.SPI_Mode      = SPI_Mode_Slave;		//主从选择   SPI_Mode_Master, SPI_Mode_Slave
	SPI_InitStructure.SPI_CPOL      = SPI_CPOL_Low;			//时钟相位   SPI_CPOL_High,   SPI_CPOL_Low
	SPI_InitStructure.SPI_CPHA      = SPI_CPHA_2Edge;		//数据边沿   SPI_CPHA_1Edge,  SPI_CPHA_2Edge
	SPI_InitStructure.SPI_Speed     = SPI_Speed_4;			//SPI速度    SPI_Speed_4, SPI_Speed_8, SPI_Speed_16, SPI_Speed_2
	SPI_Init(&SPI_InitStructure);
	NVIC_SPI_Init(ENABLE,Priority_3);		//中断使能, ENABLE/DISABLE; 优先级(低到高) Priority_0,Priority_1,Priority_2,Priority_3

	P2_MODE_IO_PU(GPIO_Pin_All);		//P2 设置为准双向口
	P4_MODE_IO_PU(GPIO_Pin_6 | GPIO_Pin_7);		//P4.6,P4.7 设置为准双向口

	SPI_SS_2 = 1;
}

//========================================================================
// 函数: Sample_SPI_PS
// 描述: 用户应用程序.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2020-09-27
//========================================================================
void Sample_SPI_PS(void)
{
	u8 i;
	
	if(COM2.RX_TimeOut > 0)
	{
		if(--COM2.RX_TimeOut == 0)
		{
			if(COM2.RX_Cnt > 0)
			{
				UartReceived = 1;   //设置串口接收标志
			}
		}
	}
	if((UartReceived) && (SPI_SS_2))
	{
		SPI_SS_2 = 0;     //拉低从机 SS 管脚
		SPI_SetMode(SPI_Mode_Master);
		for(i=0;i<COM2.RX_Cnt;i++)
		{
			SPI_WriteByte(RX2_Buffer[i]); //发送串口数据
		}
		SPI_SS_2 = 1;    //拉高从机的 SS 管脚
		SPI_SetMode(SPI_Mode_Slave);
		COM2.RX_Cnt = 0;
		UartReceived = 0;
	}
	
	if(SPI_RxTimerOut > 0)
	{
		if(--SPI_RxTimerOut == 0)
		{
			if(SPI_RxCnt > 0)
			{
				for(i=0; i<SPI_RxCnt; i++)	TX2_write2buff(SPI_RxBuffer[i]);
			}
			SPI_RxCnt = 0;
		}
	}
}



