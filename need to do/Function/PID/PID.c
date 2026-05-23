/**
 * @file    PID.c
 * @brief   Fixed-point PID controller implementation.
 * @author  boweny
 * @date    2026-04-27
 * @version v1.0
 */

#include "PID.h"

#define PID_INT16_MAX_VALUE  32767
#define PID_INT16_MIN_VALUE  ((int16)(-32767 - 1))

static int32 PID_Clamp32(int32 value, int32 min_value, int32 max_value)
{
    if (min_value > max_value) {
        return value;
    }
    if (value > max_value) {
        return max_value;
    }
    if (value < min_value) {
        return min_value;
    }
    return value;
}

static int16 PID_Clamp16(int32 value, int16 min_value, int16 max_value)
{
    if (min_value > max_value) {
        if (value > 32767L) {
            return 32767;
        }
        if (value < -32768L) {
            return PID_INT16_MIN_VALUE;
        }
        return (int16)value;
    }

    if (value > (int32)max_value) {
        return max_value;
    }
    if (value < (int32)min_value) {
        return min_value;
    }
    return (int16)value;
}

void PID_Init(PID_Controller_t *pid,
              int16 kp, int16 ki, int16 kd,
              int16 output_min, int16 output_max,
              int32 integral_min, int32 integral_max)
{
    if (pid == 0) {
        return;
    }

    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->target = 0;
    pid->output_min = output_min;
    pid->output_max = output_max;
    pid->integral_min = integral_min;
    pid->integral_max = integral_max;

    PID_Reset(pid);
}

void PID_Reset(PID_Controller_t *pid)
{
    if (pid == 0) {
        return;
    }

    pid->integral = 0;
    pid->prev_error = 0;
    pid->initialized = 0;
}

void PID_SetTarget(PID_Controller_t *pid, int16 target)
{
    if (pid == 0) {
        return;
    }

    pid->target = target;
}

void PID_SetGains(PID_Controller_t *pid, int16 kp, int16 ki, int16 kd)
{
    if (pid == 0) {
        return;
    }

    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
}

void PID_SetOutputLimit(PID_Controller_t *pid, int16 output_min, int16 output_max)
{
    if (pid == 0) {
        return;
    }

    pid->output_min = output_min;
    pid->output_max = output_max;
}

void PID_SetIntegralLimit(PID_Controller_t *pid, int32 integral_min, int32 integral_max)
{
    if (pid == 0) {
        return;
    }

    pid->integral_min = integral_min;
    pid->integral_max = integral_max;
    pid->integral = PID_Clamp32(pid->integral, integral_min, integral_max);
}

int16 PID_Update(PID_Controller_t *pid, int16 measured)
{
    int16 error;
    int16 derivative;
    int32 output_q;
    int32 output;

    if (pid == 0) {
        return 0;
    }

    error = PID_Clamp16((int32)pid->target - (int32)measured,
                        PID_INT16_MIN_VALUE, PID_INT16_MAX_VALUE);

    if (!pid->initialized) {
        derivative = 0;
        pid->initialized = 1;
    } else {
        derivative = PID_Clamp16((int32)error - (int32)pid->prev_error,
                                 PID_INT16_MIN_VALUE, PID_INT16_MAX_VALUE);
    }

    pid->integral += error;
    pid->integral = PID_Clamp32(pid->integral, pid->integral_min, pid->integral_max);

    output_q = ((int32)pid->kp * error) +
               ((int32)pid->ki * pid->integral) +
               ((int32)pid->kd * derivative);
    output = output_q >> PID_GAIN_Q;

    pid->prev_error = error;

    return PID_Clamp16(output, pid->output_min, pid->output_max);
}

int16 PID_UpdateTarget(PID_Controller_t *pid, int16 target, int16 measured)
{
    PID_SetTarget(pid, target);
    return PID_Update(pid, measured);
}
