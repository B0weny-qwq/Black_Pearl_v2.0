/*---------------------------------------------------------------------*/
/* --- Web: www.STCAI.com ---------------------------------------------*/
/*---------------------------------------------------------------------*/

#include	"APP.h"
#include	"STC32G_PWM.h"
#include	"STC32G_GPIO.h"
#include	"STC32G_NVIC.h"

/*************	功能说明	**************

高级PWM定时器 PWM5,PWM6,PWM7,PWM8 每个通道都可独立实现PWM输出.

4个通道PWM根据需要设置对应输出口，可通过示波器观察输出的信号.

PWM周期和占空比可以自定义设置，最高可达65535.

下载时, 选择时钟 24MHZ (用户可在"config.h"修改频率).

******************************************/


//========================================================================
//                               本地常量声明	
//========================================================================


//========================================================================
//                               本地变量声明
//========================================================================

PWMx_Duty PWMB_Duty;
bit PWM5_Flag;
bit PWM6_Flag;
bit PWM7_Flag;
bit PWM8_Flag;

//========================================================================
//                               本地函数声明
//========================================================================


//========================================================================
//                            外部函数和变量声明
//========================================================================


//========================================================================
// 函数: PWMB_Output_init
// 描述: 用户初始化程序.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2020-09-28
//========================================================================
void PWMB_Output_init(void)
{
	PWMx_InitDefine		PWMx_InitStructure;
	
	PWMB_Duty.PWM5_Duty = 128;
	PWMB_Duty.PWM6_Duty = 256;
	PWMB_Duty.PWM7_Duty = 512;
	PWMB_Duty.PWM8_Duty = 1024;

	PWMx_InitStructure.PWM_Mode    =	CCMRn_PWM_MODE1;	//模式,		CCMRn_FREEZE,CCMRn_MATCH_VALID,CCMRn_MATCH_INVALID,CCMRn_ROLLOVER,CCMRn_FORCE_INVALID,CCMRn_FORCE_VALID,CCMRn_PWM_MODE1,CCMRn_PWM_MODE2
	PWMx_InitStructure.PWM_Duty    = PWMB_Duty.PWM5_Duty;	//PWM占空比时间, 0~Period
	PWMx_InitStructure.PWM_EnoSelect   = ENO5P;					//输出通道选择,	ENO1P,ENO1N,ENO2P,ENO2N,ENO3P,ENO3N,ENO4P,ENO4N / ENO5P,ENO6P,ENO7P,ENO8P
	PWM_Configuration(PWM5, &PWMx_InitStructure);				//初始化PWM,  PWMA,PWMB

	PWMx_InitStructure.PWM_Mode    =	CCMRn_PWM_MODE1;	//模式,		CCMRn_FREEZE,CCMRn_MATCH_VALID,CCMRn_MATCH_INVALID,CCMRn_ROLLOVER,CCMRn_FORCE_INVALID,CCMRn_FORCE_VALID,CCMRn_PWM_MODE1,CCMRn_PWM_MODE2
	PWMx_InitStructure.PWM_Duty    = PWMB_Duty.PWM6_Duty;	//PWM占空比时间, 0~Period
	PWMx_InitStructure.PWM_EnoSelect   = ENO6P;					//输出通道选择,	ENO1P,ENO1N,ENO2P,ENO2N,ENO3P,ENO3N,ENO4P,ENO4N / ENO5P,ENO6P,ENO7P,ENO8P
	PWM_Configuration(PWM6, &PWMx_InitStructure);				//初始化PWM,  PWMA,PWMB

	PWMx_InitStructure.PWM_Mode    =	CCMRn_PWM_MODE1;	//模式,		CCMRn_FREEZE,CCMRn_MATCH_VALID,CCMRn_MATCH_INVALID,CCMRn_ROLLOVER,CCMRn_FORCE_INVALID,CCMRn_FORCE_VALID,CCMRn_PWM_MODE1,CCMRn_PWM_MODE2
	PWMx_InitStructure.PWM_Duty    = PWMB_Duty.PWM7_Duty;	//PWM占空比时间, 0~Period
	PWMx_InitStructure.PWM_EnoSelect   = ENO7P;					//输出通道选择,	ENO1P,ENO1N,ENO2P,ENO2N,ENO3P,ENO3N,ENO4P,ENO4N / ENO5P,ENO6P,ENO7P,ENO8P
	PWM_Configuration(PWM7, &PWMx_InitStructure);				//初始化PWM,  PWMA,PWMB

	PWMx_InitStructure.PWM_Mode    =	CCMRn_PWM_MODE1;	//模式,		CCMRn_FREEZE,CCMRn_MATCH_VALID,CCMRn_MATCH_INVALID,CCMRn_ROLLOVER,CCMRn_FORCE_INVALID,CCMRn_FORCE_VALID,CCMRn_PWM_MODE1,CCMRn_PWM_MODE2
	PWMx_InitStructure.PWM_Duty    = PWMB_Duty.PWM8_Duty;	//PWM占空比时间, 0~Period
	PWMx_InitStructure.PWM_EnoSelect   = ENO8P;					//输出通道选择,	ENO1P,ENO1N,ENO2P,ENO2N,ENO3P,ENO3N,ENO4P,ENO4N / ENO5P,ENO6P,ENO7P,ENO8P
	PWM_Configuration(PWM8, &PWMx_InitStructure);				//初始化PWM,  PWMA,PWMB

	PWMx_InitStructure.PWM_Period   = 2047;							//周期时间,   0~65535
	PWMx_InitStructure.PWM_DeadTime = 0;								//死区发生器设置, 0~255
	PWMx_InitStructure.PWM_MainOutEnable= ENABLE;				//主输出使能, ENABLE,DISABLE
	PWMx_InitStructure.PWM_CEN_Enable   = ENABLE;				//使能计数器, ENABLE,DISABLE
	PWM_Configuration(PWMB, &PWMx_InitStructure);				//初始化PWM通用寄存器,  PWMA,PWMB

	PWM5_USE_P74();
	PWM6_USE_P75();
	PWM7_USE_P76();
	PWM8_USE_P77();

	NVIC_PWM_Init(PWMB,DISABLE,Priority_0);
}

//========================================================================
// 函数: Sample_PWMB_Output
// 描述: 用户应用程序.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2020-09-28
//========================================================================
void Sample_PWMB_Output(void)
{
	if(!PWM5_Flag)
	{
		PWMB_Duty.PWM5_Duty++;
		if(PWMB_Duty.PWM5_Duty >= 2047) PWM5_Flag = 1;
	}
	else
	{
		PWMB_Duty.PWM5_Duty--;
		if(PWMB_Duty.PWM5_Duty <= 0) PWM5_Flag = 0;
	}

	if(!PWM6_Flag)
	{
		PWMB_Duty.PWM6_Duty++;
		if(PWMB_Duty.PWM6_Duty >= 2047) PWM6_Flag = 1;
	}
	else
	{
		PWMB_Duty.PWM6_Duty--;
		if(PWMB_Duty.PWM6_Duty <= 0) PWM6_Flag = 0;
	}

	if(!PWM7_Flag)
	{
		PWMB_Duty.PWM7_Duty++;
		if(PWMB_Duty.PWM7_Duty >= 2047) PWM7_Flag = 1;
	}
	else
	{
		PWMB_Duty.PWM7_Duty--;
		if(PWMB_Duty.PWM7_Duty <= 0) PWM7_Flag = 0;
	}

	if(!PWM8_Flag)
	{
		PWMB_Duty.PWM8_Duty++;
		if(PWMB_Duty.PWM8_Duty >= 2047) PWM8_Flag = 1;
	}
	else
	{
		PWMB_Duty.PWM8_Duty--;
		if(PWMB_Duty.PWM8_Duty <= 0) PWM8_Flag = 0;
	}
	
	UpdatePwm(PWMB, &PWMB_Duty);
}



