# McuAbstraction 层说明

`McuAbstraction/` 是 MCU 外设抽象层，把 STC 官方 UART/IIC/SPI 驱动包装成工程内部统一接口。它位于 `BoardDevices -> McuAbstraction -> Drivers` 链路中。

## 当前接口

- `ef_uart`：UART 初始化、发送、接收轮询/缓冲。
- `ef_iic`：IIC 读写事务，供传感器总线复用。
- `ef_spi`：SPI 传输和 SPI-PS 相关收发基础能力。

## 对接规则

- App 不直接使用本层，优先通过 BoardDevices。
- 新增 MCU 外设抽象时，先把官方驱动细节藏在 `Src/`，再给 BoardDevices 使用。
- 不把板级设备语义写到这里，例如“左电机”“GPS”“LED”。
