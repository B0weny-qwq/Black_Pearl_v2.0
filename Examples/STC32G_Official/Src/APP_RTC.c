/*---------------------------------------------------------------------*/
/* --- Web: www.STCAI.com ---------------------------------------------*/
/*---------------------------------------------------------------------*/

#include	"APP.h"
#include	"STC32G_RTC.h"
#include	"STC32G_GPIO.h"
#include	"STC32G_UART.h"
#include	"STC32G_NVIC.h"
#include	"STC32G_Switch.h"

/*************	功能说明	**************

读写芯片内部集成的RTC模块.

用STC的MCU的IO方式驱动8位数码管。

使用Timer0的16位自动重装来产生1ms节拍,程序运行于这个节拍下, 用户修改MCU主时钟频率时,自动定时于1ms.

8位数码管显示时间(小时-分钟-秒).

行列扫描按键键码为25~32.

按键只支持单键按下, 不支持多键同时按下, 那样将会有不可预知的结果.

键按下超过1秒后,将以10键/秒的速度提供重键输出. 用户只需要检测KeyCode是否非0来判断键是否按下.

调整时间键:
键码25: 小时+.
键码26: 小时-.
键码27: 分钟+.
键码28: 分钟-.

下载时, 选择时钟 24MHz (可以在配置文件"config.h"中修改).

******************************************/

//========================================================================
//                               本地常量声明	
//========================================================================

#define SleepModeSet  0        //0:不进休眠模式，使用数码管显示时不能进休眠; 1:使能休眠模式

//========================================================================
//                               本地变量声明
//========================================================================


//========================================================================
//                               本地函数声明
//========================================================================

void IO_KeyScan(void);   //50ms call
void DisplayRTC(void);
void WriteRTC(void);

//========================================================================
//                            外部函数和变量声明
//========================================================================

extern bit B_1S;
extern bit B_Alarm;

//========================================================================
// 函数: RTC_init
// 描述: 用户初始化程序.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2020-09-25
//========================================================================
void RTC_init(void)
{
	u8  i;
	RTC_InitTypeDef		RTC_InitStructure;
	COMx_InitDefine		COMx_InitStructure;					//结构定义

	RTC_InitStructure.RTC_Clock  = RTC_IRC32KCR;	//RTC 时钟, RTC_IRC32KCR, RTC_X32KCR
	RTC_InitStructure.RTC_Enable = ENABLE;			//I2C功能使能,   ENABLE, DISABLE
	RTC_InitStructure.RTC_Year   = 21;					//RTC 年, 00~99, 对应2000~2099年
	RTC_InitStructure.RTC_Month  = 12;					//RTC 月, 01~12
	RTC_InitStructure.RTC_Day    = 31;					//RTC 日, 01~31
	RTC_InitStructure.RTC_Hour   = 23;					//RTC 时, 00~23
	RTC_InitStructure.RTC_Min    = 59;					//RTC 分, 00~59
	RTC_InitStructure.RTC_Sec    = 55;					//RTC 秒, 00~59
	RTC_InitStructure.RTC_Ssec   = 00;					//RTC 1/128秒, 00~127

	RTC_InitStructure.RTC_ALAHour= 00;					//RTC 闹钟时, 00~23
	RTC_InitStructure.RTC_ALAMin = 00;					//RTC 闹钟分, 00~59
	RTC_InitStructure.RTC_ALASec = 00;					//RTC 闹钟秒, 00~59
	RTC_InitStructure.RTC_ALASsec= 00;					//RTC 闹钟1/128秒, 00~127
	RTC_Inilize(&RTC_InitStructure);
	NVIC_RTC_Init(RTC_ALARM_INT|RTC_SEC_INT,Priority_0);		//中断使能, RTC_ALARM_INT/RTC_DAY_INT/RTC_HOUR_INT/RTC_MIN_INT/RTC_SEC_INT/RTC_SEC2_INT/RTC_SEC8_INT/RTC_SEC32_INT/DISABLE; 优先级(低到高) Priority_0,Priority_1,Priority_2,Priority_3

	COMx_InitStructure.UART_Mode      = UART_8bit_BRTx;		//模式,   UART_ShiftRight,UART_8bit_BRTx,UART_9bit,UART_9bit_BRTx
	COMx_InitStructure.UART_BRT_Use   = BRT_Timer2;			//选择波特率发生器, BRT_Timer2 (注意: 串口2固定使用BRT_Timer2, 所以不用选择)
	COMx_InitStructure.UART_BaudRate  = 115200ul;			//波特率,     110 ~ 115200
	COMx_InitStructure.UART_RxEnable  = DISABLE;			//接收禁止,   ENABLE 或 DISABLE
	UART_Configuration(UART1, &COMx_InitStructure);		//初始化串口2 UART1,UART2,UART3,UART4
	NVIC_UART1_Init(ENABLE,Priority_1);		//中断使能, ENABLE/DISABLE; 优先级(低到高) Priority_0,Priority_1,Priority_2,Priority_3
	
	P0_MODE_IO_PU(GPIO_Pin_All);		//P0 设置为准双向口
	P6_MODE_IO_PU(GPIO_Pin_All);		//P6 设置为准双向口
	P7_MODE_IO_PU(GPIO_Pin_All);		//P7 设置为准双向口
	display_index = 0;
	
	for(i=0; i<8; i++)  LED8[i] = 0x10; //上电消隐
    
	KeyHoldCnt = 0; //键按下计时
	KeyCode = 0;    //给用户使用的键码

	IO_KeyState = 0;
	IO_KeyState1 = 0;
	IO_KeyHoldCnt = 0;
	cnt50ms = 0;

	printf("STC32G RTC Test!\r\n");
}

//========================================================================
// 函数: Sample_RTC
// 描述: 用户应用程序.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2020-09-25
//========================================================================
void Sample_RTC(void)
{
	if(B_1S)
	{
		B_1S = 0;
		DisplayRTC();
		printf("Year=20%d,Month=%d,Day=%d,Hour=%d,Minute=%d,Second=%d\r\n",YEAR,MONTH,DAY,HOUR,MIN,SEC);
	}

	if(B_Alarm)
	{
		B_Alarm = 0;
		printf("RTC Alarm!\r\n");
	}

#if(SleepModeSet == 1)
		_nop_();
		_nop_();
		PD = 1;  //STC32G 芯片使用内部32K时钟，休眠无法唤醒
		_nop_();
		_nop_();
		_nop_();
		_nop_();
		_nop_();
		_nop_();
#else
	DisplayScan();
	
	if(++cnt50ms >= 50)     //50ms扫描一次行列键盘
	{
		cnt50ms = 0;
		IO_KeyScan();
	}
	
	if(KeyCode != 0)        //有键按下
	{
		if(KeyCode == 25)   //hour +1
		{
			if(++hour >= 24)    hour = 0;
			WriteRTC();
			DisplayRTC();
		}
		if(KeyCode == 26)   //hour -1
		{
			if(--hour >= 24)    hour = 23;
			WriteRTC();
			DisplayRTC();
		}
		if(KeyCode == 27)   //minute +1
		{
			second = 0;
			if(++minute >= 60)  minute = 0;
			WriteRTC();
			DisplayRTC();
		}
		if(KeyCode == 28)   //minute -1
		{
			second = 0;
			if(--minute >= 60)  minute = 59;
			WriteRTC();
			DisplayRTC();
		}

		KeyCode = 0;
	}
#endif
}

//========================================================================
// 函数: DisplayRTC
// 描述: 显示时钟函数.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2020-09-25
//========================================================================
void DisplayRTC(void)
{
    hour = HOUR;
    minute = MIN;

    if(HOUR >= 10)  LED8[0] = HOUR / 10;
    else            LED8[0] = DIS_BLACK;
    LED8[1] = HOUR % 10;
    LED8[2] = DIS_;
    LED8[3] = MIN / 10;
    LED8[4] = MIN % 10;
    LED8[5] = DIS_;
    LED8[6] = SEC / 10;
    LED8[7] = SEC % 10;
}


//========================================================================
// 函数: WriteRTC
// 描述: 写RTC函数.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2020-09-25
//========================================================================
void WriteRTC(void)
{
    INIYEAR = YEAR;   //继承当前年月日
    INIMONTH = MONTH;
    INIDAY = DAY;

    INIHOUR = hour;   //修改时分秒
    INIMIN = minute;
    INISEC = 0;
    INISSEC = 0;
    RTCCFG |= 0x01;   //触发RTC寄存器初始化
}


/*****************************************************
    行列键扫描程序
    使用XY查找4x4键的方法，只能单键，速度快

   Y     P04      P05      P06      P07
          |        |        |        |
X         |        |        |        |
P00 ---- K00 ---- K01 ---- K02 ---- K03 ----
          |        |        |        |
P01 ---- K04 ---- K05 ---- K06 ---- K07 ----
          |        |        |        |
P02 ---- K08 ---- K09 ---- K10 ---- K11 ----
          |        |        |        |
P03 ---- K12 ---- K13 ---- K14 ---- K15 ----
          |        |        |        |
******************************************************/


//========================================================================
// 函数: IO_KeyDelay
// 描述: 按键扫描延迟程序.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2020-09-25
//========================================================================
void IO_KeyDelay(void)
{
    u8 i;
    i = 60;
    while(--i)  ;
}

//========================================================================
// 函数: IO_KeyScan
// 描述: 按键扫描程序.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2020-09-25
//========================================================================
void IO_KeyScan(void)    //50ms call
{
    u8  j;

    j = IO_KeyState1;   //保存上一次状态

    P0 = 0xf0;  //X低，读Y
    IO_KeyDelay();
    IO_KeyState1 = P0 & 0xf0;

    P0 = 0x0f;  //Y低，读X
    IO_KeyDelay();
    IO_KeyState1 |= (P0 & 0x0f);
    IO_KeyState1 ^= 0xff;   //取反
    
    if(j == IO_KeyState1)   //连续两次读相等
    {
        j = IO_KeyState;
        IO_KeyState = IO_KeyState1;
        if(IO_KeyState != 0)    //有键按下
        {
            F0 = 0;
            if(j == 0)  F0 = 1; //第一次按下
            else if(j == IO_KeyState)
            {
                if(++IO_KeyHoldCnt >= 20)   //1秒后重键
                {
                    IO_KeyHoldCnt = 18;
                    F0 = 1;
                }
            }
            if(F0)
            {
                j = T_KeyTable[IO_KeyState >> 4];
                if((j != 0) && (T_KeyTable[IO_KeyState& 0x0f] != 0)) 
                    KeyCode = (j - 1) * 4 + T_KeyTable[IO_KeyState & 0x0f] + 16;    //计算键码，17~32
            }
        }
        else    IO_KeyHoldCnt = 0;
    }
    P0 = 0xff;
}

