#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

fail=0

check_absent() {
  local label="$1"
  local pattern="$2"
  shift 2

  if rg -n "$pattern" "$@"; then
    echo "[boundary] FAIL: $label" >&2
    fail=1
  else
    echo "[boundary] OK: $label"
  fi
}

check_absent \
  "App/Components/Services must not include vendor, RTOS, chip, or board-private headers directly" \
  '#\s*include\s*[<"](STC32G|stc32g|DL_|main\.h|gpio\.h|usart\.h|syscfg|FreeRTOS|rtthread|zephyr|QMI8658|QMC6309|LT8920|KCT8206|gnss|config\.h)' \
  App Components Services -g'*.c' -g'*.h'

check_absent \
  "Components must remain pure algorithms without raw pins/registers/storage qualifiers" \
  '\b(STC32G|stc32g|P[0-9][0-9]|GPIO_|UART_|SPI_|I2C_|PWM_|NVIC_|EA\b|EAXSFR|xdata|idata|pdata)\b' \
  Components -g'*.c' -g'*.h'

check_absent \
  "App/Components/Services must not call old v1 hardware APIs directly" \
  'Task_GetTickMs|MainLoop_|Motor_|Wireless_|GPS_GetState|Get_ADCResult|STC32G_ADC|#include "config\.h"|xdata' \
  App Components Services -g'*.c' -g'*.h'

if [[ "$fail" -ne 0 ]]; then
  exit 1
fi

echo "[boundary] all checks passed"
