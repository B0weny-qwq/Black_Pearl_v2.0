/*---------------------------------------------------------------------*/
/* --- Web: www.STCAI.com ---------------------------------------------*/
/*---------------------------------------------------------------------*/

#include	"APP_DMA_I2C.h"
#include	"STC32G_I2C.h"
#include	"STC32G_GPIO.h"
#include	"STC32G_UART.h"
#include	"STC32G_DMA.h"
#include	"STC32G_NVIC.h"
#include	"STC32G_Delay.h"
#include	"STC32G_Switch.h"

/*************	本程序功能说明	**************

通过串口2(P4.6 P4.7)发指令通过I2C DMA读写AT24C02数据.

默认波特率:  115200,N,8,1. 

串口命令设置: (命令字母不区分大小写)
    W 0x12 1234567890 --> 写入操作  十六进制地址  写入内容.
    R 0x12 10         --> 读出操作  十六进制地址  读出字节数.

下载时, 选择时钟 24MHz (可以在配置文件"config.h"中修改).

******************************************/


//========================================================================
//                               本地常量声明	
//========================================================================

#define SLAW    0xA0
#define SLAR    0xA1

#define EE_BUF_LENGTH       255          //

//========================================================================
//                               本地变量声明
//========================================================================

u8 EEPROM_addr;
u8 xdata I2cTxBuffer[EE_BUF_LENGTH+1];
u8 xdata I2cRxBuffer[EE_BUF_LENGTH+1];

//========================================================================
//                               本地函数声明
//========================================================================

void WriteNbyte(u8 addr, u8 number);
void ReadNbyte( u8 addr, u8 number);

//========================================================================
//                            外部函数和变量声明
//========================================================================


//========================================================================
// 函数: DMA_I2C_init
// 描述: 用户初始化程序.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2020-09-28
//========================================================================
void DMA_I2C_init(void)
{
	I2C_InitTypeDef		I2C_InitStructure;
	COMx_InitDefine		COMx_InitStructure;		//结构定义
	DMA_I2C_InitTypeDef		DMA_I2C_InitStructure;		//结构定义

	I2C_SW(I2C_P24_P25);					//I2C_P14_P15,I2C_P24_P25,I2C_P76_P77,I2C_P33_P32
	P2_MODE_IO_PU(GPIO_Pin_4 | GPIO_Pin_5);		//P2.4,P2.5 设置为准双向口
	P4_MODE_IO_PU(GPIO_Pin_6 | GPIO_Pin_7);		//P4.6,P4.7 设置为准双向口
	
	COMx_InitStructure.UART_Mode      = UART_8bit_BRTx;		//模式,   UART_ShiftRight,UART_8bit_BRTx,UART_9bit,UART_9bit_BRTx
//	COMx_InitStructure.UART_BRT_Use   = BRT_Timer2;			//选择波特率发生器, BRT_Timer2 (注意: 串口2固定使用BRT_Timer2, 所以不用选择)
	COMx_InitStructure.UART_BaudRate  = 115200ul;			//波特率,     110 ~ 115200
	COMx_InitStructure.UART_RxEnable  = ENABLE;				//接收允许,   ENABLE 或 DISABLE
	UART_Configuration(UART2, &COMx_InitStructure);		//初始化串口2 UART1,UART2,UART3,UART4
	NVIC_UART2_Init(ENABLE,Priority_1);		//中断使能, ENABLE/DISABLE; 优先级(低到高) Priority_0,Priority_1,Priority_2,Priority_3

	I2C_InitStructure.I2C_Mode      = I2C_Mode_Master;	//主从选择   I2C_Mode_Master, I2C_Mode_Slave
	I2C_InitStructure.I2C_Enable    = ENABLE;						//I2C功能使能,   ENABLE, DISABLE
	I2C_InitStructure.I2C_MS_WDTA   = DISABLE;					//主机使能自动发送,  ENABLE, DISABLE
	I2C_InitStructure.I2C_Speed     = 63;								//总线速度=Fosc/2/(Speed*2+4),      0~63
	I2C_Init(&I2C_InitStructure);
	NVIC_I2C_Init(I2C_Mode_Master,DISABLE,Priority_0);		//主从模式, I2C_Mode_Master, I2C_Mode_Slave; 中断使能, ENABLE/DISABLE; 优先级(低到高) Priority_0,Priority_1,Priority_2,Priority_3

	DMA_I2C_InitStructure.DMA_TX_Length = EE_BUF_LENGTH;	//DMA传输总字节数  	(0~65535) + 1
	DMA_I2C_InitStructure.DMA_TX_Buffer = (u16)I2cTxBuffer;	//发送数据存储地址
	DMA_I2C_InitStructure.DMA_RX_Length = EE_BUF_LENGTH;	//DMA传输总字节数  	(0~65535) + 1
	DMA_I2C_InitStructure.DMA_RX_Buffer = (u16)I2cRxBuffer;	//接收数据存储地址
	DMA_I2C_InitStructure.DMA_TX_Enable = ENABLE;		//DMA使能  	ENABLE,DISABLE
	DMA_I2C_InitStructure.DMA_RX_Enable = ENABLE;		//DMA使能  	ENABLE,DISABLE
	DMA_I2C_Inilize(&DMA_I2C_InitStructure);	//初始化

	NVIC_DMA_I2CT_Init(ENABLE,Priority_0,Priority_0);		//中断使能, ENABLE/DISABLE; 优先级(低到高) Priority_0~Priority_3; 总线优先级(低到高) Priority_0~Priority_3
	NVIC_DMA_I2CR_Init(ENABLE,Priority_0,Priority_0);		//中断使能, ENABLE/DISABLE; 优先级(低到高) Priority_0~Priority_3; 总线优先级(低到高) Priority_0~Priority_3
	DMA_I2CR_CLRFIFO();		//清空 DMA FIFO

	printf("命令设置:\r\n");
	printf("W 0x12 1234567890 --> 写入操作  十六进制地址  写入内容\r\n");
	printf("R 0x12 10         --> 读出操作  十六进制地址  读出字节内容\r\n");
}

//========================================================================
// 函数: I2cCheckData
// 描述: 数据校验函数.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2020-09-28
//========================================================================
static u8	I2cCheckData(u8 dat)
{
	if((dat >= '0') && (dat <= '9'))		return (dat-'0');
	if((dat >= 'A') && (dat <= 'F'))		return (dat-'A'+10);
	if((dat >= 'a') && (dat <= 'f'))		return (dat-'a'+10);
	return 0xff;
}

//========================================================================
// 函数: I2cGetAddress
// 描述: 计算各种输入方式的地址.
// 参数: 无.
// 返回: 8位EEPROM地址.
// 版本: V1.0, 2013-6-6
//========================================================================
static u8 I2cGetAddress(void)
{
    u8 address;
    u8  i,j;
    
    address = 0;
    if((RX2_Buffer[2] == '0') && (RX2_Buffer[3] == 'X'))
    {
        for(i=4; i<6; i++)
        {
            j = I2cCheckData(RX2_Buffer[i]);
            if(j >= 0x10)   return 0;  //error
            address = (address << 4) + j;
        }
        return (address);
    }
    return  0; //error
}

//========================================================================
// 函数: I2cGetDataLength
// 描述: 获取要读出数据的字节数.
// 参数: 无.
// 返回: 1要读出数据的字节数.
// 版本: V1.0, 2013-6-6
//========================================================================
static u8 I2cGetDataLength(void)
{
	u8  i;
	u8  length;
	
	length = 0;
	for(i=7; i<COM2.RX_Cnt; i++)
	{
		if(I2cCheckData(RX2_Buffer[i]) >= 10)  break;
		length = length * 10 + I2cCheckData(RX2_Buffer[i]);
	}
	return (length);
}

//========================================================================
// 函数: Sample_DMA_I2C
// 描述: 用户应用程序.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2020-09-28
//========================================================================
void Sample_DMA_I2C(void)
{
	u8  i,j;
	u8  addr;
	u8  status;

	if(COM2.RX_TimeOut > 0)		//超时计数
	{
		if(--COM2.RX_TimeOut == 0)
		{
//			printf("收到内容如下： ");
//			for(i=0; i<COM2.RX_Cnt; i++)    printf("%c", RX2_Buffer[i]);    //把收到的数据原样返回,用于测试
//			printf("\r\n");

			status = 0xff;  //状态给一个非0值
			if((COM2.RX_Cnt >= 8) && (RX2_Buffer[1] == ' ')) //最短命令为8个字节
			{
				for(i=0; i<6; i++)
				{
					if((RX2_Buffer[i] >= 'a') && (RX2_Buffer[i] <= 'z'))    RX2_Buffer[i] = RX2_Buffer[i] - 'a' + 'A';  //小写转大写
				}
				addr = I2cGetAddress();
				//if(addr <= 255)    //限制地址范围
				{
					if((RX2_Buffer[0] == 'W')&& (RX2_Buffer[6] == ' '))   //写入N个字节
					{
						j = COM2.RX_Cnt - 7;
						if(j > EE_BUF_LENGTH)  j = EE_BUF_LENGTH; //越界检测

						for(i=0; i<j; i++)  I2cTxBuffer[i+2] = RX2_Buffer[i+7];
						WriteNbyte(addr, j);     //写N个字节 
						printf("已写入%d字节内容!\r\n",j);
						delay_ms(5);

						ReadNbyte(addr, j);
						printf("读出%d个字节内容如下：\r\n",j);
						for(i=0; i<j; i++)    printf("%c", I2cRxBuffer[i]);
						printf("\r\n");

						status = 0; //命令正确
					}
          else if((RX2_Buffer[0] == 'R') && (RX2_Buffer[6] == ' '))   //读出N个字节
					{
						j = I2cGetDataLength();
						if(j > EE_BUF_LENGTH)  j = EE_BUF_LENGTH; //越界检测
						if(j > 0)
						{
							ReadNbyte(addr, j);
							printf("读出%d个字节内容如下：\r\n",j);
							for(i=0; i<j; i++)    printf("%c", I2cRxBuffer[i]);
							printf("\r\n");

							status = 0; //命令正确
						}
					}
				}
			}
			if(status != 0) printf("命令错误！\r\n");
			COM2.RX_Cnt = 0;
		}
	}
}

void WriteNbyte(u8 addr, u8 number)  /*  WordAddress,First Data Address,Byte lenth   */
{
	while(Get_MSBusy_Status());    //检查I2C控制器忙碌状态

	DmaI2CTFlag = 1;
	I2cTxBuffer[0] = SLAW;
	I2cTxBuffer[1] = addr;

	I2C_MSCMD(0x89);			//起始命令+发送数据+接收ACK
	I2C_DMA_Enable();
	SET_I2CT_DMA_LEN(number+1);	//设置传输总字节数：n+1
	SET_I2C_DMA_ST(number+1);		//设置需要传输字节数：n+1
	DMA_I2CT_TRIG();

	while(DmaI2CTFlag);         //DMA忙检测
	I2C_DMA_Disable();
}

void ReadNbyte(u8 addr, u8 number)   /*  WordAddress,First Data Address,Byte lenth   */
{
	while(Get_MSBusy_Status());    //检查I2C控制器忙碌状态
	I2C_DMA_Disable();

	//发送起始信号+设备地址+写信号
	SendCmdData(0x09,SLAW);

	//发送存储器地址
	SendCmdData(0x0a,addr);
    
	//发送起始信号+设备地址+读信号
	SendCmdData(0x09,SLAR);

	DmaI2CRFlag = 1;

	I2C_MSCMD(0x8b);			//起始命令+发送数据+接收ACK
	I2C_DMA_Enable();
	SET_I2CR_DMA_LEN(number-1);	//设置传输总字节数：n+1
	SET_I2C_DMA_ST(number-1);		//设置需要传输字节数：n+1
	DMA_I2CR_TRIG();

	while(DmaI2CRFlag);         //DMA忙检测
	I2C_DMA_Disable();
}
