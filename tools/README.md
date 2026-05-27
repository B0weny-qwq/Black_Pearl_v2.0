# tools 工具说明

`tools/` 保存工程辅助脚本和上位机调试工具。

## 子目录

- `ship_log_viewer/`：串口日志查看器，解析固件 `[SHIP]`、`[CTRL]`、`[AHRS]`、`[MAG]`、`[HDG]` 等日志并更新卡片。
- `ship_packet_builder/`：旧协议 `AA | len | cmd | payload | xor | BB` 造包工具。
- `checks/`：本地结构检查脚本，例如 App/Components 禁止直接 include 底层驱动。

## 对接规则

- 固件新增日志字段后，同步更新 `tools/ship_log_viewer/README.md` 和 HTML 解析规则。
- 协议命令格式变化后，同步更新 `tools/ship_packet_builder/README.md`。
