#ifndef __BOARD_MOTOR_H__
#define __BOARD_MOTOR_H__

#include "type_def.h"

#define BOARD_MOTOR_OK                 0
#define BOARD_MOTOR_ERR_PARAM         -1
#define BOARD_MOTOR_ERR_NOT_READY     -2

#define BOARD_MOTOR_PWM_PERIOD      1000U
#define BOARD_MOTOR_SPEED_MAX       1000

typedef enum
{
    BOARD_MOTOR_LEFT = 0,
    BOARD_MOTOR_RIGHT = 1
} board_motor_id_t;

typedef struct
{
    u16 mla_duty;
    u16 mlb_duty;
    u16 mra_duty;
    u16 mrb_duty;
    u16 period;
} board_motor_pwm_snapshot_t;

/**
 * @brief 初始化板级双路电机 PWM 输出。
 *
 * 当前实现绑定 PWMA3/PWMA4 互补输出，其中左电机使用 P2.4/P2.5，
 * 右电机使用 P2.6/P2.7。该接口负责完成 GPIO 复用、PWM 初始化、
 * 输出极性配置和安全停机，不向 App 暴露底层寄存器或 STC 驱动细节。
 *
 * @return BOARD_MOTOR_OK 初始化成功。
 */
int8 board_motor_init(void);

/**
 * @brief 设置单路电机目标速度。
 *
 * 速度范围为 `-BOARD_MOTOR_SPEED_MAX` 到 `BOARD_MOTOR_SPEED_MAX`。
 * 该接口只更新目标值，真正的 PWM 刷新由 `board_motor_service()` 完成。
 *
 * @param motor 电机编号。
 * @param speed 目标速度。
 * @return BOARD_MOTOR_OK 设置成功。
 * @return BOARD_MOTOR_ERR_PARAM 电机编号非法。
 * @return BOARD_MOTOR_ERR_NOT_READY 模块尚未初始化。
 */
int8 board_motor_set_speed(board_motor_id_t motor, int16 speed);

/**
 * @brief 同时设置左右电机目标速度。
 *
 * @param left_speed 左电机目标速度。
 * @param right_speed 右电机目标速度。
 * @return BOARD_MOTOR_OK 设置成功。
 * @return BOARD_MOTOR_ERR_NOT_READY 模块尚未初始化。
 */
int8 board_motor_set_both_speed(int16 left_speed, int16 right_speed);

/**
 * @brief 将目标速度刷新到底层 PWM 输出。
 *
 * 该函数应在主循环中周期调用。调用前仅更新目标值不会立刻修改输出。
 *
 * @return BOARD_MOTOR_OK 刷新成功。
 * @return BOARD_MOTOR_ERR_NOT_READY 模块尚未初始化。
 */
int8 board_motor_service(void);

/**
 * @brief 停止指定电机。
 *
 * @param motor 电机编号。
 * @return BOARD_MOTOR_OK 设置成功。
 * @return BOARD_MOTOR_ERR_PARAM 电机编号非法。
 * @return BOARD_MOTOR_ERR_NOT_READY 模块尚未初始化。
 */
int8 board_motor_stop(board_motor_id_t motor);

/**
 * @brief 停止左右两路电机。
 *
 * @return BOARD_MOTOR_OK 设置成功。
 * @return BOARD_MOTOR_ERR_NOT_READY 模块尚未初始化。
 */
int8 board_motor_stop_all(void);

/**
 * @brief 获取指定电机当前已生效速度。
 *
 * @param motor 电机编号。
 * @param speed 输出速度指针，不能为 NULL。
 * @return BOARD_MOTOR_OK 获取成功。
 * @return BOARD_MOTOR_ERR_PARAM 参数非法。
 * @return BOARD_MOTOR_ERR_NOT_READY 模块尚未初始化。
 */
int8 board_motor_get_speed(board_motor_id_t motor, int16 *speed);

/**
 * @brief 获取当前 PWM 占空比分布快照。
 *
 * @param snapshot 输出快照指针，不能为 NULL。
 * @return BOARD_MOTOR_OK 获取成功。
 * @return BOARD_MOTOR_ERR_PARAM 参数非法。
 * @return BOARD_MOTOR_ERR_NOT_READY 模块尚未初始化。
 */
int8 board_motor_get_pwm_snapshot(board_motor_pwm_snapshot_t *snapshot);

/**
 * @brief 返回电机模块是否已完成初始化。
 *
 * @return 1 表示已就绪，0 表示未就绪。
 */
u8 board_motor_is_ready(void);

#endif
