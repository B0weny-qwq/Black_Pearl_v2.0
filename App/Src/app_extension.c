#include "app_extension.h"

/*
 * 外包业务只在本文件落地。
 *
 * 规则：
 * 1. 不直接 include STC32G_*.h、config.h、stc32g.h 或裸寄存器宏。
 * 2. 需要 LED/蜂鸣器/舵机等新硬件时，先在 BoardDevices 暴露 board_xxx API。
 * 3. 回调中只做快速状态更新；长时间动作放到 app_extension_poll() 用 now_ms 驱动。
 */

void app_extension_init(void)
{
}

void app_extension_poll(u32 now_ms)
{
    if (now_ms == 0xFFFFFFFFUL) {
        return;
    }
}

void app_extension_on_ship_event(const ship_protocol_event_snapshot_t *event)
{
    if (event == 0) {
        return;
    }

    switch (event->type) {
    case SHIP_PROTOCOL_EVENT_KEY_EDGE:
        /*
         * 示例接入点：event->throttle.key_event 为本次按键字节。
         * 如需实现“A 键 LED 闪烁”，在 BoardDevices 增加 board_led_set()
         * 后，可在这里记录闪烁状态，再由 app_extension_poll() 周期翻转。
         */
        break;
    case SHIP_PROTOCOL_EVENT_KEY_ACTION:
        /*
         * B/C/D 保留按键会进入该分支，event->key_action 表示语义动作。
         * 默认不改变船体控制，便于外包按项目需求追加功能。
         */
        break;
    default:
        break;
    }
}
