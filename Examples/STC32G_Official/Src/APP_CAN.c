/*---------------------------------------------------------------------*/
/* --- Web: www.STCAI.com ---------------------------------------------*/
/*---------------------------------------------------------------------*/

#include	"APP_CAN.h"
#include	"STC32G_CAN.h"
#include	"STC32G_GPIO.h"
#include	"STC32G_UART.h"
#include	"STC32G_NVIC.h"
#include	"STC32G_Switch.h"

/*************   功能说明   ***************

CAN1、CAN2总线同时使用收发测试用例.

DCAN是一个支持CAN2.0B协议的功能单元。

收到一帧数据后, 通过串口1（P3.0,P3.1）发送出去，串口参数：115200,N,8,1.

需要在"STC32G_UART.h"里设置： #define	PRINTF_SELECT  UART1

MCU每秒钟从CAN1、CAN2发送一帧固定数据.

默认CAN总线波特率500KHz, 用户可自行修改.

下载时, 选择时钟 24MHz (用户可在"config.h"修改频率).

******************************************/

//========================================================================
//                               本地常量声明	
//========================================================================


//========================================================================
//                               本地变量声明
//========================================================================


//========================================================================
//                               本地函数声明
//========================================================================


//========================================================================
//                            外部函数和变量声明
//========================================================================

extern u16 msecond;

//========================================================================
// 函数: CAN_init
// 描述: 用户初始化程序.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2022-03-27
//========================================================================
void CAN_init(void)
{
	COMx_InitDefine		COMx_InitStructure;					//结构定义
	CAN_InitTypeDef	CAN_InitStructure;					//结构定义

	CAN_InitStructure.CAN_Enable = ENABLE;		//CAN功能使能   ENABLE或DISABLE
	CAN_InitStructure.CAN_IMR    = CAN_ALLIM;	//CAN中断寄存器 	CAN_DOIM,CAN_BEIM,CAN_TIM,CAN_RIM,CAN_EPIM,CAN_EWIM,CAN_ALIM,CAN_ALLIM,DISABLE
	CAN_InitStructure.CAN_SJW    = 0;					//重新同步跳跃宽度  0~3
	CAN_InitStructure.CAN_SAM    = 0;					//总线电平采样次数  0:采样1次; 1:采样3次

	//CAN总线波特率=Fclk/((1+(TSG1+1)+(TSG2+1))*(BRP+1)*2)
	CAN_InitStructure.CAN_TSG1   = 2;					//同步采样段1       0~15
	CAN_InitStructure.CAN_TSG2   = 1;					//同步采样段2       1~7 (TSG2 不能设置为0)
	CAN_InitStructure.CAN_BRP    = 3;					//波特率分频系数    0~63
	//24000000/((1+3+2)*4*2)=500KHz

	CAN_InitStructure.CAN_ListenOnly = DISABLE;	//Listen Only模式   ENABLE,DISABLE
	CAN_InitStructure.CAN_Filter  = DUAL_FILTER;//滤波选择  DUAL_FILTER(双滤波),SINGLE_FILTER(单滤波)
	CAN_InitStructure.CAN_ACR0    = 0x00;			//总线验收代码寄存器 0~0xFF
	CAN_InitStructure.CAN_ACR1    = 0x00;
	CAN_InitStructure.CAN_ACR2    = 0x00;
	CAN_InitStructure.CAN_ACR3    = 0x00;
	CAN_InitStructure.CAN_AMR0    = 0xff;			//总线验收屏蔽寄存器 0~0xFF
	CAN_InitStructure.CAN_AMR1    = 0xff;
	CAN_InitStructure.CAN_AMR2    = 0xff;
	CAN_InitStructure.CAN_AMR3    = 0xff;
	CAN_Inilize(CAN1,&CAN_InitStructure);			//CAN1 初始化
	CAN_Inilize(CAN2,&CAN_InitStructure);			//CAN2 初始化
	
	NVIC_CAN_Init(CAN1,ENABLE,Priority_1);		//中断使能, CAN1/CAN2; ENABLE/DISABLE; 优先级(低到高) Priority_0,Priority_1,Priority_2,Priority_3
	NVIC_CAN_Init(CAN2,ENABLE,Priority_1);		//中断使能, CAN1/CAN2; ENABLE/DISABLE; 优先级(低到高) Priority_0,Priority_1,Priority_2,Priority_3
	
	P5_MODE_IO_PU(GPIO_Pin_LOW);		//P5.0~P5.3 设置为准双向口

	CAN1_SW(CAN1_P50_P51);				//CAN1_P00_P01,CAN1_P50_P51,CAN1_P42_P45,CAN1_P70_P71
	CAN2_SW(CAN2_P52_P53);				//CAN2_P02_P03,CAN2_P52_P53,CAN2_P46_P47,CAN2_P72_P73

	//====初始化数据=====
	CAN1_Tx.FF = STANDARD_FRAME;    //标准帧
	CAN1_Tx.RTR = 0;                //0：数据帧，1：远程帧
	CAN1_Tx.DLC = 0x08;             //数据长度
	CAN1_Tx.ID = 0x0567;            //CAN ID
	CAN1_Tx.DataBuffer[0] = 0x11;   //数据内容
	CAN1_Tx.DataBuffer[1] = 0x12;
	CAN1_Tx.DataBuffer[2] = 0x13;
	CAN1_Tx.DataBuffer[3] = 0x14;
	CAN1_Tx.DataBuffer[4] = 0x15;
	CAN1_Tx.DataBuffer[5] = 0x16;
	CAN1_Tx.DataBuffer[6] = 0x17;
	CAN1_Tx.DataBuffer[7] = 0x18;
	
	CAN2_Tx.FF = EXTENDED_FRAME;    //扩展帧
	CAN2_Tx.RTR = 0;                //0：数据帧，1：远程帧
	CAN2_Tx.DLC = 0x08;             //数据长度
	CAN2_Tx.ID = 0x03456789;        //CAN ID
	CAN2_Tx.DataBuffer[0] = 0x21;   //数据内容
	CAN2_Tx.DataBuffer[1] = 0x22;
	CAN2_Tx.DataBuffer[2] = 0x23;
	CAN2_Tx.DataBuffer[3] = 0x24;
	CAN2_Tx.DataBuffer[4] = 0x25;
	CAN2_Tx.DataBuffer[5] = 0x26;
	CAN2_Tx.DataBuffer[6] = 0x27;
	CAN2_Tx.DataBuffer[7] = 0x28;

    //打印串口初始化
	COMx_InitStructure.UART_Mode      = UART_8bit_BRTx;		//模式,   UART_ShiftRight,UART_8bit_BRTx,UART_9bit,UART_9bit_BRTx
	COMx_InitStructure.UART_BRT_Use   = BRT_Timer2;			//选择波特率发生器, BRT_Timer2 (注意: 串口2固定使用BRT_Timer2, 所以不用选择)
	COMx_InitStructure.UART_BaudRate  = 115200ul;			//波特率,     110 ~ 115200
	COMx_InitStructure.UART_RxEnable  = ENABLE;				//接收允许,   ENABLE或DISABLE
	UART_Configuration(UART1, &COMx_InitStructure);		//初始化串口 USART1,USART2,USART3,USART4
	NVIC_UART1_Init(ENABLE,Priority_1);		//中断使能, ENABLE/DISABLE; 优先级(低到高) Priority_0,Priority_1,Priority_2,Priority_3

	P3_MODE_IO_PU(GPIO_Pin_0 | GPIO_Pin_1);		//P3.0,P3.1 设置为准双向口
	UART1_SW(UART1_SW_P30_P31);		//UART1_SW_P30_P31,UART1_SW_P36_P37,UART1_SW_P16_P17,UART1_SW_P43_P44
}

//========================================================================
// 函数: Sample_CAN
// 描述: 用户应用程序.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2022-03-27
//========================================================================
void Sample_CAN(void)
{
	u8 sr;
	u8 n,i,j;

		//------------------处理CAN1模块-----------------------
	if(++msecond >= 1000)   //1秒到
	{
		msecond = 0;

		CANSEL = CAN1;		//选择CAN1模块
		sr = CanReadReg(SR);

		if(sr & 0x01)		//判断是否有 BS:BUS-OFF状态
		{
			CANAR = MR;
			CANDR &= ~0x04;  //清除 Reset Mode, 从BUS-OFF状态退出
		}
		else
		{
            CanSendMsg(&CAN1_Tx);   //发送一帧数据
		}

		//------------------处理CAN2模块-----------------------
		CANSEL = CAN2;		//选择CAN2模块
		sr = CanReadReg(SR);

		if(sr & 0x01)		//判断是否有 BS:BUS-OFF状态
		{
			CANAR = MR;
			CANDR &= ~0x04;  //清除 Reset Mode, 从BUS-OFF状态退出
		}
		else
		{
            CanSendMsg(&CAN2_Tx);   //发送一帧数据
		}
	}

	if(B_Can1Read)
	{
		B_Can1Read = 0;
		
		CANSEL = CAN1;		//选择CAN1模块
        n = CanReadMsg(CAN1_Rx);    //读取接收内容
        if(n>0)
        {
            for(i=0;i<n;i++)
            {
//                CanSendMsg(&CAN1_Rx[i]);  //CAN总线原样返回
                    printf("CAN1 ID=0x%08lX DLC=%d FF=%d RTR=%d ",CAN1_Rx[i].ID,CAN1_Rx[i].DLC,CAN1_Rx[i].FF,CAN1_Rx[i].RTR);
                for(j=0;j<CAN1_Rx[i].DLC;j++)
                {
                    printf("0x%02X ",CAN1_Rx[i].DataBuffer[j]);    //从串口输出收到的数据
                }
                printf("\r\n");
            }
        }
	}

	if(B_Can2Read)
	{
		B_Can2Read = 0;
		
		CANSEL = CAN2;		//选择CAN2模块
        n = CanReadMsg(CAN2_Rx);    //读取接收内容
        if(n>0)
        {
            for(i=0;i<n;i++)
            {
//                CanSendMsg(CAN2_Rx);  //CAN总线原样返回
                    printf("CAN2 ID=0x%08lX DLC=%d FF=%d RTR=%d ",CAN2_Rx[i].ID,CAN2_Rx[i].DLC,CAN2_Rx[i].FF,CAN2_Rx[i].RTR);
                for(j=0;j<CAN2_Rx[i].DLC;j++)
                {
                    printf("0x%02X ",CAN2_Rx[i].DataBuffer[j]);    //从串口输出收到的数据
                }
                printf("\r\n");
            }
        }
	}
}

