/*---------------------------------------------------------------------*/
/* --- Web: www.STCAI.com ---------------------------------------------*/
/*---------------------------------------------------------------------*/

#include	"APP.h"
#include	"STC32G_PWM.h"
#include	"STC32G_GPIO.h"
#include	"STC32G_NVIC.h"

/*************	功能说明	**************

高级PWM定时器 PWM1P/PWM1N,PWM2P/PWM2N,PWM3P/PWM3N,PWM4P/PWM4N 每个通道都可独立实现PWM输出，或者两两互补对称输出.

8个通道PWM设置对应P6的8个端口.

通过P6口上连接的8个LED灯，利用PWM实现呼吸灯效果.

PWM周期和占空比可以根据需要自行设置，最高可达65535.

下载时, 选择时钟 24MHZ (用户可在"config.h"修改频率).

******************************************/


//========================================================================
//                               本地常量声明	
//========================================================================


//========================================================================
//                               本地变量声明
//========================================================================

PWMx_Duty PWMA_Duty;
bit PWM1_Flag;
bit PWM2_Flag;
bit PWM3_Flag;
bit PWM4_Flag;

//========================================================================
//                               本地函数声明
//========================================================================


//========================================================================
//                            外部函数和变量声明
//========================================================================


//========================================================================
// 函数: PWMA_Output_init
// 描述: 用户初始化程序.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2020-09-28
//========================================================================
void PWMA_Output_init(void)
{
	PWMx_InitDefine		PWMx_InitStructure;
	
	PWMA_Duty.PWM1_Duty = 128;
	PWMA_Duty.PWM2_Duty = 256;
	PWMA_Duty.PWM3_Duty = 512;
	PWMA_Duty.PWM4_Duty = 1024;

	PWMx_InitStructure.PWM_Mode    =	CCMRn_PWM_MODE1;	//模式,		CCMRn_FREEZE,CCMRn_MATCH_VALID,CCMRn_MATCH_INVALID,CCMRn_ROLLOVER,CCMRn_FORCE_INVALID,CCMRn_FORCE_VALID,CCMRn_PWM_MODE1,CCMRn_PWM_MODE2
	PWMx_InitStructure.PWM_Duty    = PWMA_Duty.PWM1_Duty;	//PWM占空比时间, 0~Period
	PWMx_InitStructure.PWM_EnoSelect   = ENO1P | ENO1N;	//输出通道选择,	ENO1P,ENO1N,ENO2P,ENO2N,ENO3P,ENO3N,ENO4P,ENO4N / ENO5P,ENO6P,ENO7P,ENO8P
	PWM_Configuration(PWM1, &PWMx_InitStructure);				//初始化PWM,  PWMA,PWMB

	PWMx_InitStructure.PWM_Mode    =	CCMRn_PWM_MODE1;	//模式,		CCMRn_FREEZE,CCMRn_MATCH_VALID,CCMRn_MATCH_INVALID,CCMRn_ROLLOVER,CCMRn_FORCE_INVALID,CCMRn_FORCE_VALID,CCMRn_PWM_MODE1,CCMRn_PWM_MODE2
	PWMx_InitStructure.PWM_Duty    = PWMA_Duty.PWM2_Duty;	//PWM占空比时间, 0~Period
	PWMx_InitStructure.PWM_EnoSelect   = ENO2P | ENO2N;	//输出通道选择,	ENO1P,ENO1N,ENO2P,ENO2N,ENO3P,ENO3N,ENO4P,ENO4N / ENO5P,ENO6P,ENO7P,ENO8P
	PWM_Configuration(PWM2, &PWMx_InitStructure);				//初始化PWM,  PWMA,PWMB

	PWMx_InitStructure.PWM_Mode    =	CCMRn_PWM_MODE1;	//模式,		CCMRn_FREEZE,CCMRn_MATCH_VALID,CCMRn_MATCH_INVALID,CCMRn_ROLLOVER,CCMRn_FORCE_INVALID,CCMRn_FORCE_VALID,CCMRn_PWM_MODE1,CCMRn_PWM_MODE2
	PWMx_InitStructure.PWM_Duty    = PWMA_Duty.PWM3_Duty;	//PWM占空比时间, 0~Period
	PWMx_InitStructure.PWM_EnoSelect   = ENO3P | ENO3N;	//输出通道选择,	ENO1P,ENO1N,ENO2P,ENO2N,ENO3P,ENO3N,ENO4P,ENO4N / ENO5P,ENO6P,ENO7P,ENO8P
	PWM_Configuration(PWM3, &PWMx_InitStructure);				//初始化PWM,  PWMA,PWMB

	PWMx_InitStructure.PWM_Mode    =	CCMRn_PWM_MODE1;	//模式,		CCMRn_FREEZE,CCMRn_MATCH_VALID,CCMRn_MATCH_INVALID,CCMRn_ROLLOVER,CCMRn_FORCE_INVALID,CCMRn_FORCE_VALID,CCMRn_PWM_MODE1,CCMRn_PWM_MODE2
	PWMx_InitStructure.PWM_Duty    = PWMA_Duty.PWM4_Duty;	//PWM占空比时间, 0~Period
	PWMx_InitStructure.PWM_EnoSelect   = ENO4P | ENO4N;	//输出通道选择,	ENO1P,ENO1N,ENO2P,ENO2N,ENO3P,ENO3N,ENO4P,ENO4N / ENO5P,ENO6P,ENO7P,ENO8P
	PWM_Configuration(PWM4, &PWMx_InitStructure);				//初始化PWM,  PWMA,PWMB

	PWMx_InitStructure.PWM_Period   = 2047;							//周期时间,   0~65535
	PWMx_InitStructure.PWM_DeadTime = 0;								//死区发生器设置, 0~255
	PWMx_InitStructure.PWM_MainOutEnable= ENABLE;				//主输出使能, ENABLE,DISABLE
	PWMx_InitStructure.PWM_CEN_Enable   = ENABLE;				//使能计数器, ENABLE,DISABLE
	PWM_Configuration(PWMA, &PWMx_InitStructure);				//初始化PWM通用寄存器,  PWMA,PWMB

	PWM1_USE_P60P61();
	PWM2_USE_P62P63();
	PWM3_USE_P64P65();
	PWM4_USE_P66P67();

	P4_MODE_IO_PU(GPIO_Pin_0);			//P4.0 设置为准双向口
//	P6_MODE_IO_PU(GPIO_Pin_All);		//P6 设置为准双向口（启动PWM功能后输出脚自动设置为推挽输出模式）
	NVIC_PWM_Init(PWMA,DISABLE,Priority_0);
	P40 = 0;		//打开LED电源
}

//========================================================================
// 函数: Sample_PWMA_Output
// 描述: 用户应用程序.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2020-09-28
//========================================================================
void Sample_PWMA_Output(void)
{
	if(!PWM1_Flag)
	{
		PWMA_Duty.PWM1_Duty++;
		if(PWMA_Duty.PWM1_Duty >= 2047) PWM1_Flag = 1;
	}
	else
	{
		PWMA_Duty.PWM1_Duty--;
		if(PWMA_Duty.PWM1_Duty <= 0) PWM1_Flag = 0;
	}

	if(!PWM2_Flag)
	{
		PWMA_Duty.PWM2_Duty++;
		if(PWMA_Duty.PWM2_Duty >= 2047) PWM2_Flag = 1;
	}
	else
	{
		PWMA_Duty.PWM2_Duty--;
		if(PWMA_Duty.PWM2_Duty <= 0) PWM2_Flag = 0;
	}

	if(!PWM3_Flag)
	{
		PWMA_Duty.PWM3_Duty++;
		if(PWMA_Duty.PWM3_Duty >= 2047) PWM3_Flag = 1;
	}
	else
	{
		PWMA_Duty.PWM3_Duty--;
		if(PWMA_Duty.PWM3_Duty <= 0) PWM3_Flag = 0;
	}

	if(!PWM4_Flag)
	{
		PWMA_Duty.PWM4_Duty++;
		if(PWMA_Duty.PWM4_Duty >= 2047) PWM4_Flag = 1;
	}
	else
	{
		PWMA_Duty.PWM4_Duty--;
		if(PWMA_Duty.PWM4_Duty <= 0) PWM4_Flag = 0;
	}
	
	UpdatePwm(PWMA, &PWMA_Duty);
}



