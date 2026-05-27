# BoardDevices/Inc 说明

这里放板级设备公开接口。每个头文件代表一种板级能力，例如 `board_motor`、`board_power`、`board_imu`。

接口应描述“能做什么”，不要暴露引脚、寄存器、地址和外设实例。新增硬件时先在这里定义 `board_xxx_init/read/set` 等 API，再由 `App/` 调用。
