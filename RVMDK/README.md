# RVMDK 工程说明

`RVMDK/` 保存 Keil uVision 工程文件。

## 关键文件

- `STC32G-LIB.uvproj`：Keil 工程，包含 App、BoardDevices、ChipDrivers、Components、McuAbstraction、Platform、Services、Drivers 和 Startup 文件分组。
- `list/`：Keil 编译中间产物目录，被 `.gitignore` 忽略，不作为源码交接内容。

## 修改规则

- 新增 `.c/.h` 后同步更新 `STC32G-LIB.uvproj`。
- 不提交 `.uvopt/.uvguix`、`list/`、`.obj/.lst/.map/.hex` 等个人配置或编译产物。
- 命令行/CMake 结构更新时，也要检查 Keil 工程是否漏文件。
