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
  - `App/Src/app.c` 只负责 `board_console_init()` 和 `log_init()` 的高层顺序。
- 更新 Keil 工程分组和 include path，加入 `Services`、`BoardDevices`、
  `McuAbstraction` 和 `logger` 相关文件。
- 新增 `BoardDevices/Inc/board_imu.h`、`BoardDevices/Src/board_imu.c`，
  预留 QMI8658 板级 IMU 入口；底层传输未接入。
- 新增 `McuAbstraction/Inc/ef_iic.h`、`McuAbstraction/Src/ef_iic.c`，按
  STC DMA IIC 参考时序迁移寄存器连续读写封装，并加入超时返回。
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
    `board_spi_ps.c`。
