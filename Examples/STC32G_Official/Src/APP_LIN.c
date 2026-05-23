/*---------------------------------------------------------------------*/
/* --- Web: www.STCAI.com ---------------------------------------------*/
/*---------------------------------------------------------------------*/

#include	"APP_LIN.h"
#include	"STC32G_LIN.h"
#include	"STC32G_GPIO.h"
#include	"STC32G_UART.h"
#include	"STC32G_NVIC.h"
#include	"STC32G_Switch.h"

/*************   功能说明   ***************

Lin主机模式收发测试：
按一下P32口按键, 主机发送完整一帧数据.
按一下P33口按键, 主机发送帧头并获取从机应答数据（合并成一串完整的帧）.

Lin从机模式收发测试：
收到一个非本机应答的完整帧后通过串口2输出, 并保存到数据缓存.
收到一个本机应答的帧头后(例如：ID=0x12), 发送缓存数据进行应答.

默认传输速率：9600波特率, 用户可自行修改.

下载时, 选择时钟 24MHz (用户可在"config.h"修改频率).

******************************************/

sbit SLP_N  = P5^2;     //0: Sleep

//========================================================================
//                               本地常量声明	
//========================================================================

#define	LIN_MASTER_MODE		1    //0: 从机模式; 1: 主机模式

//========================================================================
//                               本地变量声明
//========================================================================

u8 Lin_ID;
u8 TX_BUF[8];

//========================================================================
//                               本地函数声明
//========================================================================


//========================================================================
//                            外部函数和变量声明
//========================================================================

extern bit LinRxFlag;

extern u8 Key1_cnt;
extern u8 Key2_cnt;
extern bit Key1_Flag;
extern bit Key2_Flag;

//========================================================================
// 函数: LIN_init
// 描述: 用户初始化程序.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2022-03-27
//========================================================================
void LIN_init(void)
{
	LIN_InitTypeDef	LIN_InitStructure;				//结构定义
	COMx_InitDefine COMx_InitStructure;				//结构定义

	COMx_InitStructure.UART_Mode      = UART_8bit_BRTx;		//模式,   UART_ShiftRight,UART_8bit_BRTx,UART_9bit,UART_9bit_BRTx
//	COMx_InitStructure.UART_BRT_Use   = BRT_Timer2;			//选择波特率发生器, BRT_Timer2 (注意: 串口2固定使用BRT_Timer2, 所以不用选择)
	COMx_InitStructure.UART_BaudRate  = 115200ul;			//波特率,     110 ~ 115200
	COMx_InitStructure.UART_RxEnable  = ENABLE;				//接收允许,   ENABLE 或 DISABLE
	UART_Configuration(UART2, &COMx_InitStructure);		//初始化串口2 UART1,UART2,UART3,UART4
	NVIC_UART2_Init(ENABLE,Priority_1);		//中断使能, ENABLE/DISABLE; 优先级(低到高) Priority_0,Priority_1,Priority_2,Priority_3

#if(LIN_MASTER_MODE==0)
	LIN_InitStructure.LIN_IE       = LIN_ALLIE;	//LIN中断使能  	LIN_LIDE/LIN_RDYE/LIN_ERRE/LIN_ABORTE/LIN_ALLIE,DISABLE
#else
	LIN_InitStructure.LIN_IE       = DISABLE;	//LIN中断使能  	LIN_LIDE/LIN_RDYE/LIN_ERRE/LIN_ABORTE/LIN_ALLIE,DISABLE
#endif
	LIN_InitStructure.LIN_Enable   = ENABLE;	//LIN功能使能  	ENABLE,DISABLE
	LIN_InitStructure.LIN_Baudrate = 9600;		//LIN波特率
	LIN_InitStructure.LIN_HeadDelay = 1;			//帧头延时计数  	0~(65535*1000)/MAIN_Fosc
	LIN_InitStructure.LIN_HeadPrescaler = 1;	//帧头延时分频  	0~63
	LIN_Inilize(&LIN_InitStructure);					//LIN 初始化
	
	NVIC_LIN_Init(ENABLE,Priority_1);		//中断使能, ENABLE/DISABLE; 优先级(低到高) Priority_0,Priority_1,Priority_2,Priority_3
	
	P0_MODE_IO_PU(GPIO_Pin_2 | GPIO_Pin_3);		//P0.2,P0.3 设置为准双向口
	P4_MODE_IO_PU(GPIO_Pin_6 | GPIO_Pin_7);		//P4.6,P4.7 设置为准双向口
	P5_MODE_IO_PU(GPIO_Pin_2);		//P5.2 设置为准双向口
	
	P0_PULL_UP_ENABLE(GPIO_Pin_2 | GPIO_Pin_3);

	LIN_SW(LIN_P02_P03);				//LIN_P02_P03,LIN_P52_P53,LIN_P46_P47,LIN_P72_P73

	//====初始化数据=====
	SLP_N = 1;
	Lin_ID = 0x32;
	TX_BUF[0] = 0x81;
	TX_BUF[1] = 0x22;
	TX_BUF[2] = 0x33;
	TX_BUF[3] = 0x44;
	TX_BUF[4] = 0x55;
	TX_BUF[5] = 0x66;
	TX_BUF[6] = 0x77;
	TX_BUF[7] = 0x88;
}

//========================================================================
// 函数: Sample_LIN
// 描述: 用户应用程序.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2022-03-27
//========================================================================
void Sample_LIN(void)
{
	u8 i;
#if(LIN_MASTER_MODE==1)
	if(!P32)
	{
		if(!Key1_Flag)
		{
			Key1_cnt++;
			if(Key1_cnt > 50)
			{
				Key1_Flag = 1;
				LinSendFrame(Lin_ID, TX_BUF);  //发送一串完整数据
			}
		}
	}
	else
	{
		Key1_cnt = 0;
		Key1_Flag = 0;
	}

	if(!P33)
	{
		if(!Key2_Flag)
		{
			Key2_cnt++;
			if(Key2_cnt > 50)
			{
				Key2_Flag = 1;
				LinSendHeaderRead(0x13,TX_BUF);  //发送帧头，获取数据帧，组成一个完整的帧
				printf("接收如下：\r\n");
				for(i=0; i<FRAME_LEN; i++)    printf("%02x ", TX_BUF[i]);
				printf("\r\n");
			}
		}
	}
	else
	{
		Key2_cnt = 0;
		Key2_Flag = 0;
	}
#else
	u8 isr;

	if(LinRxFlag == 1)
	{
		LinRxFlag = 0;
		isr = LinReadReg(LID);
		if(isr == 0x12)		//判断是否从机响应ID
		{
			LinTxResponse(TX_BUF);	//返回响应数据
		}
		else
		{
			LinReadFrame(TX_BUF);			//接收Lin总线数据
			printf("ID=0x%02X, 接收内容：\r\n",isr);
			for(i=0; i<FRAME_LEN; i++)    printf("%02x ", TX_BUF[i]);
			printf("\r\n");
		}
	}
#endif
}

