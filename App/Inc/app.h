#ifndef __APP_H_
#define __APP_H_

#include "type_def.h"
#include "AHRS.h"

void app_init(void);
void app_loop(void);
const AHRS_State_t *app_get_attitude_state(void);
u8 app_get_heading_ready(void);
u16 app_get_heading_deg100(void);
int16 app_get_heading_relative_deg100(void);

#endif
