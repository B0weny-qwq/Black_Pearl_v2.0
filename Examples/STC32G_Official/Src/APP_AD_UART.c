/*---------------------------------------------------------------------*/
/* --- Web: www.STCAI.com ---------------------------------------------*/
/*---------------------------------------------------------------------*/

#include	"APP_AD_UART.h"
#include	"STC32G_GPIO.h"
#include	"STC32G_ADC.h"
#include	"STC32G_UART.h"
#include	"STC32G_NVIC.h"


/*************	功能说明	**************

本程序演示多路ADC查询采样，通过串口发送给上位机，波特率115200,N,8,1。

用定时器做波特率发生器，建议使用1T模式(除非低波特率用12T)，并选择可被波特率整除的时钟频率，以提高精度。

下载时, 选择时钟 22.1184MHz (可以在配置文件"config.h"中修改).

******************************************/


//========================================================================
//                               本地常量声明	
//========================================================================


//========================================================================
//                               本地变量声明
//========================================================================

u8 index = 0;

//========================================================================
//                               本地函数声明
//========================================================================


//========================================================================
//                            外部函数和变量声明
//========================================================================


//========================================================================
// 函数: ADtoUART_init
// 描述: 用户初始化程序.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2020-09-28
//========================================================================
void ADtoUART_init(void)
{
	ADC_InitTypeDef		ADC_InitStructure;		//结构定义
	COMx_InitDefine		COMx_InitStructure;					//结构定义

	P0_MODE_IN_HIZ(GPIO_Pin_LOW | GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6);		//P0.0~P0.6 设置为高阻输入
	P1_MODE_IN_HIZ(GPIO_Pin_All);		//P1.0~P1.7 设置为高阻输入
	P5_MODE_IN_HIZ(GPIO_Pin_4);			//P5.4 设置为高阻输入
	
	COMx_InitStructure.UART_Mode      = UART_8bit_BRTx;		//模式,   UART_ShiftRight,UART_8bit_BRTx,UART_9bit,UART_9bit_BRTx
	COMx_InitStructure.UART_BRT_Use   = BRT_Timer2;			//选择波特率发生器, BRT_Timer2 (注意: 串口2固定使用BRT_Timer2)
	COMx_InitStructure.UART_BaudRate  = 115200ul;			//波特率,     110 ~ 115200
	COMx_InitStructure.UART_RxEnable  = ENABLE;				//接收允许,   ENABLE或DISABLE
	UART_Configuration(UART1, &COMx_InitStructure);		//初始化串口 UART1,UART2,UART3,UART4
	NVIC_UART1_Init(ENABLE,Priority_1);		//中断使能, ENABLE/DISABLE; 优先级(低到高) Priority_0,Priority_1,Priority_2,Priority_3

	ADC_InitStructure.ADC_SMPduty   = 31;		//ADC 模拟信号采样时间控制, 0~31（注意： SMPDUTY 一定不能设置小于 10）
	ADC_InitStructure.ADC_CsSetup   = 0;		//ADC 通道选择时间控制 0(默认),1
	ADC_InitStructure.ADC_CsHold    = 1;		//ADC 通道选择保持时间控制 0,1(默认),2,3
	ADC_InitStructure.ADC_Speed     = ADC_SPEED_2X16T;		//设置 ADC 工作时钟频率	ADC_SPEED_2X1T~ADC_SPEED_2X16T
	ADC_InitStructure.ADC_AdjResult = ADC_RIGHT_JUSTIFIED;	//ADC结果调整,	ADC_LEFT_JUSTIFIED,ADC_RIGHT_JUSTIFIED
	ADC_Inilize(&ADC_InitStructure);		//初始化
	ADC_PowerControl(ENABLE);						//ADC电源开关, ENABLE或DISABLE
	NVIC_ADC_Init(DISABLE,Priority_0);		//中断使能, ENABLE/DISABLE; 优先级(低到高) Priority_0,Priority_1,Priority_2,Priority_3
}

//========================================================================
// 函数: Sample_ADtoUART
// 描述: 用户应用程序.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2020-09-24
//========================================================================
void Sample_ADtoUART(void)
{
	u16	j;

	j = Get_ADCResult(index);	//参数0~15,查询方式获取一次ADC, 返回值就是结果, == 4096 为错误
	printf("AD%02d=%04d ",index,j);
	if((index & 7) == 7)	printf("\r\n");

	index++;
	if(index > 15)
	{
		index = 0;
	}
}



