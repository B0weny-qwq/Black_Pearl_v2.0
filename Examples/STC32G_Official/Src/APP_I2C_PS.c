/*---------------------------------------------------------------------*/
/* --- Web: www.STCAI.com ---------------------------------------------*/
/*---------------------------------------------------------------------*/

#include	"APP.h"
#include	"STC32G_GPIO.h"
#include	"STC32G_I2C.h"
#include	"STC32G_Soft_I2C.h"
#include	"STC32G_NVIC.h"
#include	"STC32G_Switch.h"

/*************	功能说明	**************

内部集成I2C串行总线控制器做从机模式，SCL->P3.2, SDA->P3.3;
IO口模拟I2C做主机模式，SCL->P0.0, SDA->P0.1;
通过外部飞线连接 P0.0->P3.2, P0.1->P3.3，实现I2C自发自收功能。

用STC的MCU的IO方式驱动8位数码管。
使用Timer0的16位自动重装来产生1ms节拍,程序运行于这个节拍下,用户修改MCU主时钟频率时,自动定时于1ms.
计数器每秒钟加1, 计数范围为0~9999.

显示效果为: 上电后主机每秒钟发送一次计数数据，并在左边4个数码管上显示发送内容；从机接收到数据后在右边4个数码管显示。

下载时, 选择时钟 24MHz (可以在配置文件"config.h"中修改).

******************************************/

//========================================================================
//                               本地常量声明	
//========================================================================


//========================================================================
//                               本地变量声明
//========================================================================

u8  temp[4];     //通用数组

//========================================================================
//                               本地函数声明
//========================================================================


//========================================================================
//                            外部函数和变量声明
//========================================================================


//========================================================================
// 函数: I2C_PS_init
// 描述: 用户初始化程序.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2020-09-25
//========================================================================
void I2C_PS_init(void)
{
	u8  i;
	I2C_InitTypeDef		I2C_InitStructure;

	P0_MODE_IO_PU(GPIO_Pin_0 | GPIO_Pin_1);		//P0.0,P0.1 设置为准双向口
	P3_MODE_IO_PU(GPIO_Pin_3);			//P3.3 设置为准双向口
	P6_MODE_IO_PU(GPIO_Pin_All);		//P6 设置为准双向口
	P7_MODE_IO_PU(GPIO_Pin_All);		//P7 设置为准双向口
	I2C_SW(I2C_P33_P32);					//I2C_P14_P15,I2C_P24_P25,I2C_P76_P77,I2C_P33_P32

	I2C_InitStructure.I2C_Mode      = I2C_Mode_Slave;		//主从选择   I2C_Mode_Master, I2C_Mode_Slave
	I2C_InitStructure.I2C_Enable    = ENABLE;						//I2C功能使能,   ENABLE, DISABLE
	I2C_InitStructure.I2C_SL_MA     = ENABLE;						//使能从机地址比较功能,   ENABLE, DISABLE
	I2C_InitStructure.I2C_SL_ADR    = 0x2d;							//从机设备地址,  0~127  (0x2d<<1 = 0x5a)
	I2C_Init(&I2C_InitStructure);
	NVIC_I2C_Init(I2C_Mode_Slave,I2C_ESTAI|I2C_ERXI|I2C_ETXI|I2C_ESTOI,Priority_0);	//主从模式, I2C_Mode_Master, I2C_Mode_Slave; 中断使能, I2C_ESTAI/I2C_ERXI/I2C_ETXI/I2C_ESTOI/DISABLE; 优先级(低到高) Priority_0,Priority_1,Priority_2,Priority_3

	display_index = 0;
	DisplayFlag = 0;
	for(i=0; i<8; i++)  LED8[i] = 0x10; //上电消隐
}

//========================================================================
// 函数: Sample_I2C_PS
// 描述: 用户应用程序.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2020-09-25
//========================================================================
void Sample_I2C_PS(void)
{
	DisplayScan();
	
	if(DisplayFlag)
	{
		DisplayFlag = 0;
		LED8[4] = I2C_Buffer[0];
		LED8[5] = I2C_Buffer[1];
		LED8[6] = I2C_Buffer[2];
		LED8[7] = I2C_Buffer[3];
	}
	
	if(++msecond >= 1000)   //1秒到
	{
		msecond = 0;        //清1000ms计数
		second++;         //秒计数+1
		if(second >= 200)    second = 0;   //秒计数范围为0~199

		temp[0] = second / 1000;
		temp[1] = (second % 1000) / 100;
		temp[2] = (second % 100) / 10;
		temp[3] = second % 10;
		LED8[0] = temp[0];
		LED8[1] = temp[1];
		LED8[2] = temp[2];
		LED8[3] = temp[3];

		SI2C_WriteNbyte(SLAW, 0, temp, 4);
	}
}
