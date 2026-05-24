#include "board_power.h"
#include "STC32G_ADC.h"
#include "STC32G_GPIO.h"

#define BOARD_POWER_ADC_REF_MV      3300UL
#define BOARD_POWER_BAT_DIV_NUM     1UL
#define BOARD_POWER_BAT_DIV_DEN     1UL

#ifdef BOARD_12V
#define BOARD_POWER_FULL_RAW        2000U
#define BOARD_POWER_LEVEL3_RAW      1900U
#define BOARD_POWER_LEVEL2_RAW      1730U
#define BOARD_POWER_LEVEL1_RAW      1620U
#else
#define BOARD_POWER_FULL_RAW        1710U
#define BOARD_POWER_LEVEL3_RAW      1630U
#define BOARD_POWER_LEVEL2_RAW      1530U
#define BOARD_POWER_LEVEL1_RAW      1420U
#endif

typedef struct
{
    u8 ready;
    u8 level;
    board_power_sample_t last;
} board_power_context_t;

static board_power_context_t board_power_ctx;

static u16 board_power_raw_to_mv(u16 raw)
{
    return (u16)(((u32)raw * BOARD_POWER_ADC_REF_MV) / 4095UL);
}

static u32 board_power_adc_to_bat_mv(u16 adc_mv)
{
    if (BOARD_POWER_BAT_DIV_DEN == 0UL) {
        return (u32)adc_mv;
    }
    return (((u32)adc_mv * BOARD_POWER_BAT_DIV_NUM) / BOARD_POWER_BAT_DIV_DEN);
}

static u8 board_power_raw_to_level(u16 raw)
{
    if (raw >= BOARD_POWER_FULL_RAW) {
        return BOARD_POWER_LEVEL_4;
    }
    if (raw >= BOARD_POWER_LEVEL3_RAW) {
        return BOARD_POWER_LEVEL_3;
    }
    if (raw >= BOARD_POWER_LEVEL2_RAW) {
        return BOARD_POWER_LEVEL_2;
    }
    if (raw >= BOARD_POWER_LEVEL1_RAW) {
        return BOARD_POWER_LEVEL_1;
    }
    return BOARD_POWER_LEVEL_0;
}

int8 board_power_init(void)
{
    ADC_InitTypeDef adc_init;

    if (board_power_ctx.ready != 0U) {
        return BOARD_POWER_OK;
    }

    P0_MODE_IN_HIZ(GPIO_Pin_0);
    P0_PULL_UP_DISABLE(GPIO_Pin_0);
    P0_DIGIT_IN_DISABLE(GPIO_Pin_0);

    adc_init.ADC_SMPduty = 31U;
    adc_init.ADC_CsSetup = 0U;
    adc_init.ADC_CsHold = 1U;
    adc_init.ADC_Speed = ADC_SPEED_2X1T;
    adc_init.ADC_AdjResult = ADC_RIGHT_JUSTIFIED;

    board_power_ctx.ready = (ADC_Inilize(&adc_init) == SUCCESS) ? 1U : 0U;
    ADC_PowerControl(ENABLE);
    board_power_ctx.level = BOARD_POWER_LEVEL_0;
    board_power_ctx.last.raw = 0U;
    board_power_ctx.last.adc_mv = 0U;
    board_power_ctx.last.bat_mv = 0UL;
    board_power_ctx.last.level = BOARD_POWER_LEVEL_0;
    board_power_ctx.last.valid = 0U;

    return (board_power_ctx.ready != 0U) ? BOARD_POWER_OK : BOARD_POWER_ERR_SAMPLE;
}

int8 board_power_read(board_power_sample_t *sample)
{
    u16 raw;

    if (sample == 0) {
        return BOARD_POWER_ERR_PARAM;
    }
    if (board_power_ctx.ready == 0U) {
        *sample = board_power_ctx.last;
        return BOARD_POWER_ERR_NOT_READY;
    }

    raw = Get_ADCResult(ADC_CH8);
    sample->raw = raw;
    sample->adc_mv = 0U;
    sample->bat_mv = 0UL;
    sample->level = board_power_ctx.level;
    sample->valid = 0U;

    if (raw > 4095U) {
        board_power_ctx.last = *sample;
        return BOARD_POWER_ERR_SAMPLE;
    }

    sample->adc_mv = board_power_raw_to_mv(raw);
    sample->bat_mv = board_power_adc_to_bat_mv(sample->adc_mv);
    sample->level = board_power_raw_to_level(raw);
    sample->valid = 1U;

    board_power_ctx.level = sample->level;
    board_power_ctx.last = *sample;
    return BOARD_POWER_OK;
}

u8 board_power_get_level(void)
{
    return board_power_ctx.level;
}

u8 board_power_is_ready(void)
{
    return board_power_ctx.ready;
}
