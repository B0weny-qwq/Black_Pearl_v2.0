#include "board_motor.h"
#include "ef_board_resources.h"
#include "STC32G_GPIO.h"
#include "STC32G_NVIC.h"
#include "STC32G_PWM.h"

#ifndef BOARD_MOTOR_PWM_EDGE_MARGIN
#define BOARD_MOTOR_PWM_EDGE_MARGIN  40U
#endif

#define BOARD_MOTOR_DEFAULT_DEADTIME 0U

typedef struct
{
    PWMx_Duty pwm_duty;
    int16 left_speed;
    int16 right_speed;
    int16 left_target_speed;
    int16 right_target_speed;
    u8 ready;
} board_motor_context_t;

static board_motor_context_t board_motor_ctx;

static int16 board_motor_limit_speed(int16 speed)
{
    if (speed > BOARD_MOTOR_SPEED_MAX) {
        return BOARD_MOTOR_SPEED_MAX;
    }
    if (speed < -BOARD_MOTOR_SPEED_MAX) {
        return -BOARD_MOTOR_SPEED_MAX;
    }
    return speed;
}

static u16 board_motor_speed_to_duty(int16 speed)
{
    int32 duty;

    speed = board_motor_limit_speed(speed);
    duty = (int32)(BOARD_MOTOR_PWM_PERIOD / 2U);
    duty += ((int32)speed * (int32)(BOARD_MOTOR_PWM_PERIOD / 2U)) /
            (int32)BOARD_MOTOR_SPEED_MAX;

    if (duty < (int32)BOARD_MOTOR_PWM_EDGE_MARGIN) {
        duty = (int32)BOARD_MOTOR_PWM_EDGE_MARGIN;
    } else if (duty > (int32)(BOARD_MOTOR_PWM_PERIOD - BOARD_MOTOR_PWM_EDGE_MARGIN)) {
        duty = (int32)(BOARD_MOTOR_PWM_PERIOD - BOARD_MOTOR_PWM_EDGE_MARGIN);
    }

    return (u16)duty;
}

static u8 board_motor_is_valid_id(board_motor_id_t motor)
{
    if ((motor != BOARD_MOTOR_LEFT) && (motor != BOARD_MOTOR_RIGHT)) {
        return 0U;
    }
    return 1U;
}

static void board_motor_left_output_enable(u8 enable)
{
    if (enable != 0U) {
        PWMA_CC3E_Enable();
        PWMA_CC3NE_Enable();
        PWMA_ENO |= (ENO3P | ENO3N);
    } else {
        PWMA_ENO &= (u8)~(ENO3P | ENO3N);
        PWMA_CC3E_Disable();
        PWMA_CC3NE_Disable();
    }
}

static void board_motor_right_output_enable(u8 enable)
{
    if (enable != 0U) {
        PWMA_CC4E_Enable();
        PWMA_CC4NE_Enable();
        PWMA_ENO |= (ENO4P | ENO4N);
    } else {
        PWMA_ENO &= (u8)~(ENO4P | ENO4N);
        PWMA_CC4E_Disable();
        PWMA_CC4NE_Disable();
    }
}

static void board_motor_left_set_forward_polarity(void)
{
    PWMA_CC3P_LowValid();
    PWMA_CC3NP_LowValid();
}

static void board_motor_right_set_forward_polarity(void)
{
    PWMA_CC4P_HighValid();
    PWMA_CC4NP_HighValid();
}

static void board_motor_apply_speed(board_motor_id_t motor, int16 speed)
{
    u16 duty;

    speed = board_motor_limit_speed(speed);
    if (speed == 0) {
        if (motor == BOARD_MOTOR_LEFT) {
            board_motor_ctx.pwm_duty.PWM3_Duty = BOARD_MOTOR_PWM_PERIOD / 2U;
            UpdatePwm(PWM3, &board_motor_ctx.pwm_duty);
            board_motor_left_output_enable(0U);
            board_motor_ctx.left_speed = 0;
        } else {
            board_motor_ctx.pwm_duty.PWM4_Duty = BOARD_MOTOR_PWM_PERIOD / 2U;
            UpdatePwm(PWM4, &board_motor_ctx.pwm_duty);
            board_motor_right_output_enable(0U);
            board_motor_ctx.right_speed = 0;
        }
        return;
    }

    duty = board_motor_speed_to_duty(speed);
    if (motor == BOARD_MOTOR_LEFT) {
        board_motor_ctx.pwm_duty.PWM3_Duty = duty;
        UpdatePwm(PWM3, &board_motor_ctx.pwm_duty);
        board_motor_left_output_enable(1U);
        board_motor_ctx.left_speed = speed;
    } else {
        board_motor_ctx.pwm_duty.PWM4_Duty = duty;
        UpdatePwm(PWM4, &board_motor_ctx.pwm_duty);
        board_motor_right_output_enable(1U);
        board_motor_ctx.right_speed = speed;
    }
}

int8 board_motor_init(void)
{
    PWMx_InitDefine pwm_init;

    P2_MODE_OUT_PP(EF_BOARD_MOTOR_PWM3P_PIN_MASK |
                   EF_BOARD_MOTOR_PWM3N_PIN_MASK |
                   EF_BOARD_MOTOR_PWM4P_PIN_MASK |
                   EF_BOARD_MOTOR_PWM4N_PIN_MASK);
    P2_PULL_UP_DISABLE(EF_BOARD_MOTOR_PWM3P_PIN_MASK |
                       EF_BOARD_MOTOR_PWM3N_PIN_MASK |
                       EF_BOARD_MOTOR_PWM4P_PIN_MASK |
                       EF_BOARD_MOTOR_PWM4N_PIN_MASK);

    EF_BOARD_MOTOR_PWM3_MUX();
    EF_BOARD_MOTOR_PWM4_MUX();

    board_motor_ctx.pwm_duty.PWM1_Duty = 0U;
    board_motor_ctx.pwm_duty.PWM2_Duty = 0U;
    board_motor_ctx.pwm_duty.PWM3_Duty = BOARD_MOTOR_PWM_PERIOD / 2U;
    board_motor_ctx.pwm_duty.PWM4_Duty = BOARD_MOTOR_PWM_PERIOD / 2U;
    board_motor_ctx.pwm_duty.PWM5_Duty = 0U;
    board_motor_ctx.pwm_duty.PWM6_Duty = 0U;
    board_motor_ctx.pwm_duty.PWM7_Duty = 0U;
    board_motor_ctx.pwm_duty.PWM8_Duty = 0U;

    pwm_init.PWM_Mode = CCMRn_PWM_MODE1;
    pwm_init.PWM_Duty = BOARD_MOTOR_PWM_PERIOD / 2U;
    pwm_init.PWM_Period = BOARD_MOTOR_PWM_PERIOD;
    pwm_init.PWM_DeadTime = BOARD_MOTOR_DEFAULT_DEADTIME;
    pwm_init.PWM_MainOutEnable = DISABLE;
    pwm_init.PWM_CEN_Enable = DISABLE;

    pwm_init.PWM_EnoSelect = ENO3P | ENO3N;
    (void)PWM_Configuration(PWM3, &pwm_init);
    pwm_init.PWM_EnoSelect = ENO4P | ENO4N;
    (void)PWM_Configuration(PWM4, &pwm_init);

    pwm_init.PWM_MainOutEnable = ENABLE;
    pwm_init.PWM_CEN_Enable = ENABLE;
    (void)PWM_Configuration(PWMA, &pwm_init);

    PWMA_OC3_OUT_0();
    PWMA_OC3N_OUT_0();
    PWMA_OC4_OUT_0();
    PWMA_OC4N_OUT_0();

    board_motor_left_set_forward_polarity();
    board_motor_right_set_forward_polarity();
    board_motor_left_output_enable(0U);
    board_motor_right_output_enable(0U);

    board_motor_ctx.left_speed = 0;
    board_motor_ctx.right_speed = 0;
    board_motor_ctx.left_target_speed = 0;
    board_motor_ctx.right_target_speed = 0;
    board_motor_ctx.ready = 1U;

    NVIC_PWM_Init(PWMA, DISABLE, Priority_0);
    (void)board_motor_stop_all();
    return BOARD_MOTOR_OK;
}

int8 board_motor_set_speed(board_motor_id_t motor, int16 speed)
{
    if (board_motor_ctx.ready == 0U) {
        return BOARD_MOTOR_ERR_NOT_READY;
    }
    if (board_motor_is_valid_id(motor) == 0U) {
        return BOARD_MOTOR_ERR_PARAM;
    }

    speed = board_motor_limit_speed(speed);
    if (motor == BOARD_MOTOR_LEFT) {
        board_motor_ctx.left_target_speed = speed;
    } else {
        board_motor_ctx.right_target_speed = speed;
    }
    return BOARD_MOTOR_OK;
}

int8 board_motor_set_both_speed(int16 left_speed, int16 right_speed)
{
    if (board_motor_ctx.ready == 0U) {
        return BOARD_MOTOR_ERR_NOT_READY;
    }

    board_motor_ctx.left_target_speed = board_motor_limit_speed(left_speed);
    board_motor_ctx.right_target_speed = board_motor_limit_speed(right_speed);
    return BOARD_MOTOR_OK;
}

int8 board_motor_service(void)
{
    if (board_motor_ctx.ready == 0U) {
        return BOARD_MOTOR_ERR_NOT_READY;
    }

    board_motor_apply_speed(BOARD_MOTOR_LEFT, board_motor_ctx.left_target_speed);
    board_motor_apply_speed(BOARD_MOTOR_RIGHT, board_motor_ctx.right_target_speed);
    return BOARD_MOTOR_OK;
}

int8 board_motor_stop(board_motor_id_t motor)
{
    if (board_motor_ctx.ready == 0U) {
        return BOARD_MOTOR_ERR_NOT_READY;
    }
    if (board_motor_is_valid_id(motor) == 0U) {
        return BOARD_MOTOR_ERR_PARAM;
    }

    if (motor == BOARD_MOTOR_LEFT) {
        board_motor_ctx.left_target_speed = 0;
    } else {
        board_motor_ctx.right_target_speed = 0;
    }
    return BOARD_MOTOR_OK;
}

int8 board_motor_stop_all(void)
{
    if (board_motor_ctx.ready == 0U) {
        return BOARD_MOTOR_ERR_NOT_READY;
    }

    board_motor_ctx.left_target_speed = 0;
    board_motor_ctx.right_target_speed = 0;
    return board_motor_service();
}

int8 board_motor_get_speed(board_motor_id_t motor, int16 *speed)
{
    if (board_motor_ctx.ready == 0U) {
        return BOARD_MOTOR_ERR_NOT_READY;
    }
    if ((speed == 0) || (board_motor_is_valid_id(motor) == 0U)) {
        return BOARD_MOTOR_ERR_PARAM;
    }

    if (motor == BOARD_MOTOR_LEFT) {
        *speed = board_motor_ctx.left_speed;
    } else {
        *speed = board_motor_ctx.right_speed;
    }
    return BOARD_MOTOR_OK;
}

int8 board_motor_get_pwm_snapshot(board_motor_pwm_snapshot_t *snapshot)
{
    if (board_motor_ctx.ready == 0U) {
        return BOARD_MOTOR_ERR_NOT_READY;
    }
    if (snapshot == 0) {
        return BOARD_MOTOR_ERR_PARAM;
    }

    snapshot->period = BOARD_MOTOR_PWM_PERIOD;
    snapshot->mla_duty = board_motor_ctx.pwm_duty.PWM3_Duty;
    snapshot->mlb_duty = (u16)(BOARD_MOTOR_PWM_PERIOD - board_motor_ctx.pwm_duty.PWM3_Duty);
    snapshot->mra_duty = (u16)(BOARD_MOTOR_PWM_PERIOD - board_motor_ctx.pwm_duty.PWM4_Duty);
    snapshot->mrb_duty = board_motor_ctx.pwm_duty.PWM4_Duty;
    return BOARD_MOTOR_OK;
}

u8 board_motor_is_ready(void)
{
    return board_motor_ctx.ready;
}
