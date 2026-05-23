# 变更日志

## Unreleased

- 将工程整理为 Black Pearl v2.0，作为 v1.0 的移植、解耦和重构基线。
- 移除旧版本中混杂的 `User/` 层。
- 将真实应用入口移动到 `App/Src/main.c`。
- 在 `App/Src/app.c` 中加入最小应用生命周期钩子。
- 将平台启动、时钟配置和调度胶水迁移到 `Platform/`。
- 将 STC 启动汇编移动到 `Startup/`。
- 将官方 STC 驱动目录从 `Driver/` 重命名为 `Drivers/`。
- 将自定义 MCU 统一抽象层从 `Drivers/` 中拆出为 `McuAbstraction/`，
  避免与 STC 官方 SDK 文件混放。
- 更新 Keil 工程分组和 include path，以匹配新目录结构。
- 增加显式平台时钟配置：
  - 使用 `EAXSFR()` 开启扩展 SFR 访问。
  - 使用 `HIRCClkConfig(0)` 选择 HIRC 不分频。
  - 保持 `MAIN_Fosc = 24000000L`，兼容官方示例分频语义。
- 增加中文工程文档：
  - 根目录 `README.md`
  - `doc/total.md`
  - `doc/data.md`
  - `doc/CHANGELOG.md`
- 迁移旧版 `Code_boweny/Function/Log/`：
  - 新增 `McuAbstraction/Inc/ef_uart.h`、`McuAbstraction/Src/ef_uart.c`，封装 STC UART。
  - 新增 `BoardDevices/Inc/board_console.h`、`BoardDevices/Src/board_console.c`，
    固定 UART1/P3.1/P3.0/115200 8N1。
  - 新增 `Services/Inc/logger.h`、`Services/Src/logger.c`，提供
    `LOGI/LOGW/LOGE/LOGD/log_printf`。
  - 新增 `Services/Inc/Log.h` 作为旧 include 兼容入口。
  - `logger` 使用 128 字节有界缓冲，超长日志截断，不再依赖 `vsprintf()`。
  - `App/Src/app.c` 当前负责 `board_console_init()`、`log_init()`、设备 bring-up
    和 GPS 初始化的高层顺序。
- 更新 Keil 工程分组和 include path，加入 `Services`、`BoardDevices`、
  `McuAbstraction` 和 `logger` 相关文件。
- 新增 `McuAbstraction/Inc/ef_iic.h`、`McuAbstraction/Src/ef_iic.c`，按
  STC DMA IIC 参考时序迁移寄存器连续读写封装，并加入超时返回。
- 新增 `BoardDevices/Src/board_sensor_bus.c`、`BoardDevices/Src/board_sensor_bus.h`，
  在 BoardDevices 私有层固定 P1.4/P1.5、400 kHz，复用 DMA IIC 访问。
- 新增 `ChipDrivers/Inc/QMI8658.h`、`ChipDrivers/Src/QMI8658.c`，
  完成 QMI8658 地址探测、最小初始化、状态读取和原始采样读取。
- 将 `BoardDevices/Inc/board_imu.h`、`BoardDevices/Src/board_imu.c` 从占位推进为
  实际板级绑定，初始化、周期服务和读数路径均返回显式状态码。
- 新增 `BoardDevices/Inc/board_mag.h`、`BoardDevices/Src/board_mag.c`，
  完成 QMC6309 板级磁力计绑定并复用 `board_sensor_bus`。
- 将 `QMC6309`、`LT8920`、`KCT8206` 芯片驱动整理进 `ChipDrivers/`，
  板级资源绑定收敛到 `BoardDevices/`。
- 新增 `BoardDevices/Inc/board_lt8920.h`、`BoardDevices/Src/board_lt8920.c`，
  完成 LT8920 + KCT8206 板级 bring-up、SPI 路由和寄存器校验诊断。
- `McuAbstraction/Inc/ef_spi.h`、`McuAbstraction/Src/ef_spi.c` 补充
  `ef_spi_transfer_byte()`，供 LT8920 全双工寄存器访问使用。
- `App/Src/app.c` 在日志初始化后输出版本号，并串口打印 QMI8658、QMC6309、
  LT8920 的初始化结果；LT8920 校验失败时追加寄存器诊断信息。
- 从本地待迁移参考代码迁移低通滤波模块：
  - 新增 `Components/Inc/Filter.h`、`Components/Src/Filter.c`。
  - 保留旧 `Filter_*` API，供后续 QMI8658/QMC6309 读数路径直接接入。
  - 去除对 `config.h` 的依赖，改为仅依赖 `type_def.h`，保持 Components
    层纯算法边界。
- 迁移 SPI-PS 参考实现：
  - 新增 `McuAbstraction/Inc/ef_spi.h`、`McuAbstraction/Src/ef_spi.c`，封装 STC SPI 初始化、
    主从切换、字节收发和从机 ISR 接收缓冲。
  - 新增 `BoardDevices/Inc/board_spi_ps.h`、`BoardDevices/Src/board_spi_ps.c`，
    固定 P2.2/P2.3/P2.4/P2.5，并保留“默认从机、SS 空闲抢主发送、发完退回
    从机”的官方 SPI-PS 行为。
  - 参考代码里的 UART2 桥接回显逻辑未迁入抽象层。
  - `Examples/` 和 `need to do/` 改为本地参考材料，不再纳入版本管理。
  - 更新 Keil 工程分组，将 `ef_spi.c` 放入 `McuAbstraction`，并加入
    `board_spi_ps.c`、`board_sensor_bus.c`、`board_mag.c`、`board_lt8920.c`、
    `QMI8658.c` 以及迁移后的 `QMC6309`、`LT8920`、`KCT8206`。
