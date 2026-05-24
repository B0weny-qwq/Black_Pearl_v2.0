/*---------------------------------------------------------------------*/
/* --- Web: www.STCAI.com ---------------------------------------------*/
/*---------------------------------------------------------------------*/

#include	"config.h"
#include	"platform_scheduler.h"

//========================================================================
//                               本地变量声明
//========================================================================

typedef struct 
{
	u8 Run;               //任务状态：Run/Stop
	u16 TIMCount;         //定时计数器
	u16 TRITime;          //重载计数器
	void (*Hook) (void);  //任务函数
} PLATFORM_TASK_COMPONENTS;

static PLATFORM_TASK_COMPONENTS Platform_Tasks[1];

static u8 Platform_Tasks_Max = 0;
static volatile u32 Platform_TickMs = 0UL;

//========================================================================
// 函数: platform_scheduler_tick
// 描述: 任务标记函数.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2012-10-22
//========================================================================
void platform_scheduler_tick(void)
{
	u8 i;
	Platform_TickMs++;
	for(i=0; i<Platform_Tasks_Max; i++)
	{
		if(Platform_Tasks[i].TIMCount)    /* If the time is not 0 */
		{
			Platform_Tasks[i].TIMCount--;  /* Time counter decrement */
			if(Platform_Tasks[i].TIMCount == 0)  /* If time arrives */
			{
				/*Resume the timer value and try again */
				Platform_Tasks[i].TIMCount = Platform_Tasks[i].TRITime;  
				Platform_Tasks[i].Run = 1;    /* The task can be run */
			}
		}
	}
}

//========================================================================
// 函数: platform_scheduler_run
// 描述: 任务处理函数.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2012-10-22
//========================================================================
void platform_scheduler_run(void)
{
	u8 i;
	for(i=0; i<Platform_Tasks_Max; i++)
	{
		if(Platform_Tasks[i].Run) /* If task can be run */
		{
			Platform_Tasks[i].Run = 0;    /* Flag clear 0 */
			Platform_Tasks[i].Hook();  /* Run task */
		}
	}
}

u32 platform_scheduler_get_tick_ms(void)
{
	u32 now_ms;
	u8 ea_state;

	ea_state = EA;
	EA = 0;
	now_ms = Platform_TickMs;
	EA = ea_state;
	return now_ms;
}


