/**
 * @file    PID.h
 * @brief   定点 PID 控制器接口。
 * @author  boweny
 * @date    2026-05-05
 * @version v1.0
 *
 * @details
 * 提供可复用的位置式 PID 控制器。比例、积分、微分增益使用 Q10
 * 定点格式，支持输出限幅和积分限幅，用于避免积分饱和。
 *
 * 本文件属于算法组件层，不依赖板级设备、驱动或芯片寄存器定义。
 */

#ifndef __PID_H__
#define __PID_H__

#include "type_def.h"

#define PID_GAIN_Q              10                      /**< PID 增益的小数位数，Q10 格式。 */
#define PID_GAIN_SCALE          (1L << PID_GAIN_Q)     /**< PID 增益缩放系数。 */

#define PID_GAIN_FROM_INT(x)    ((int16)((x) << PID_GAIN_Q)) /**< 将整数增益转换为 Q10 增益。 */
#define PID_GAIN_FROM_Q10(x)    ((int16)(x))                 /**< 直接使用已按 Q10 表示的增益。 */

/**
 * @brief   PID 控制器状态与参数。
 */
typedef struct
{
    int16 kp;            /**< 比例增益，Q10 格式。 */
    int16 ki;            /**< 积分增益，Q10 格式。 */
    int16 kd;            /**< 微分增益，Q10 格式。 */

    int16 target;        /**< 控制目标值。 */
    int16 output_min;    /**< 输出下限。 */
    int16 output_max;    /**< 输出上限。 */
    int32 integral_min;  /**< 积分累加下限。 */
    int32 integral_max;  /**< 积分累加上限。 */

    int32 integral;      /**< 当前积分累加值。 */
    int16 prev_error;    /**< 上一次控制误差，用于微分项计算。 */
    u8 initialized;      /**< 初始化标志，1=已初始化。 */
} PID_Controller_t;

/**
 * @brief      初始化 PID 控制器。
 * @param[out] pid           PID 控制器对象指针。
 * @param[in]  kp            比例增益，Q10 格式。
 * @param[in]  ki            积分增益，Q10 格式。
 * @param[in]  kd            微分增益，Q10 格式。
 * @param[in]  output_min    输出下限。
 * @param[in]  output_max    输出上限。
 * @param[in]  integral_min  积分累加下限。
 * @param[in]  integral_max  积分累加上限。
 */
void PID_Init(PID_Controller_t *pid,
              int16 kp, int16 ki, int16 kd,
              int16 output_min, int16 output_max,
              int32 integral_min, int32 integral_max);

/**
 * @brief      复位 PID 运行状态。
 * @param[out] pid  PID 控制器对象指针。
 *
 * @details
 * 清空积分项、上一次误差和初始化标志，不改变增益和限幅参数。
 */
void PID_Reset(PID_Controller_t *pid);

/**
 * @brief      设置 PID 控制目标值。
 * @param[out] pid     PID 控制器对象指针。
 * @param[in]  target  新目标值。
 */
void PID_SetTarget(PID_Controller_t *pid, int16 target);

/**
 * @brief      设置 PID 三项增益。
 * @param[out] pid  PID 控制器对象指针。
 * @param[in]  kp   比例增益，Q10 格式。
 * @param[in]  ki   积分增益，Q10 格式。
 * @param[in]  kd   微分增益，Q10 格式。
 */
void PID_SetGains(PID_Controller_t *pid, int16 kp, int16 ki, int16 kd);

/**
 * @brief      设置 PID 输出限幅。
 * @param[out] pid         PID 控制器对象指针。
 * @param[in]  output_min  输出下限。
 * @param[in]  output_max  输出上限。
 */
void PID_SetOutputLimit(PID_Controller_t *pid, int16 output_min, int16 output_max);

/**
 * @brief      设置 PID 积分限幅。
 * @param[out] pid           PID 控制器对象指针。
 * @param[in]  integral_min  积分累加下限。
 * @param[in]  integral_max  积分累加上限。
 */
void PID_SetIntegralLimit(PID_Controller_t *pid, int32 integral_min, int32 integral_max);

/**
 * @brief      根据当前测量值更新 PID 输出。
 * @param[out] pid       PID 控制器对象指针。
 * @param[in]  measured  当前测量值。
 * @return     限幅后的 PID 输出；pid 为 NULL 时返回 0。
 */
int16 PID_Update(PID_Controller_t *pid, int16 measured);

/**
 * @brief      设置新目标并立即更新 PID 输出。
 * @param[out] pid       PID 控制器对象指针。
 * @param[in]  target    新目标值。
 * @param[in]  measured  当前测量值。
 * @return     限幅后的 PID 输出；pid 为 NULL 时返回 0。
 */
int16 PID_UpdateTarget(PID_Controller_t *pid, int16 target, int16 measured);

#endif
