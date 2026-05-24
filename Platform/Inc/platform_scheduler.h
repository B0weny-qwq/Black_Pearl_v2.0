/*---------------------------------------------------------------------*/
/* --- Web: www.STCAI.com ---------------------------------------------*/
/*---------------------------------------------------------------------*/

#ifndef __PLATFORM_SCHEDULER_H_
#define __PLATFORM_SCHEDULER_H_

#include "type_def.h"

void platform_scheduler_tick(void);
void platform_scheduler_run(void);
u32 platform_scheduler_get_tick_ms(void);

#endif
