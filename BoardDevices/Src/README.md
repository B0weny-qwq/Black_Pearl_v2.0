# BoardDevices/Src 说明

这里放板级设备实现，可以绑定真实引脚、外设实例、芯片地址和电平极性。

允许调用 `McuAbstraction/`、`ChipDrivers/` 和必要的 `Drivers/`。对 App 暴露的只能是 `BoardDevices/Inc` 中的 API。
