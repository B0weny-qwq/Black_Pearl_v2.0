# doc 文档说明

`doc/` 保存工程迁移、结构和数据说明文档。

## 当前文档

- `total.md`：工程目录和分层职责总览。
- `data.md`：平台数据、时钟策略、构建边界和迁移数据。
- `CHANGELOG.md`：迁移和重构变更记录。
- `persistent_goal.md`：Black Pearl v2.0 跨会话持久目标、完成判定和状态机优先级。
- `state_machines.md`：协议、ShipControl、AutoDrive、NorthCalib 的状态机图、事件优先级和 v1.1/v2.0 行为对齐清单。
- `../graphify-out/README.md`：Graphify 本地图谱使用说明、关键查询命令和 v1.1/v2.0 对比摘要。

## 更新规则

- 板级 API、协议格式、上位机日志或 App 对接方式变化时，同步更新这里或根 `README.md`。
- 细节文档可以放在 `doc/`，交接入口和操作指南放在根 `README.md`。
