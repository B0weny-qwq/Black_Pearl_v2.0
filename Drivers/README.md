# Drivers 层说明

`Drivers/` 是 STC32G 官方外设驱动和库文件保留区。这里允许出现 STC 寄存器、官方外设结构体和 ISR 入口。

## 目录

- `Inc/`：STC32G 官方外设头文件。
- `Src/`：STC32G 官方外设源码。
- `Isr/`：官方 ISR 示例/适配文件。
- `Lib/`：STC 官方库文件。

## 使用规则

- App 和 Components 不直接 include 本目录头文件。
- 板级功能优先在 `McuAbstraction/` 或 `BoardDevices/` 中封装后再向上暴露。
- 除非移植芯片或修正官方驱动适配问题，否则不要修改本目录。
