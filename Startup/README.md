# Startup 层说明

`Startup/` 保存 STC/Keil 启动汇编入口。

## 当前文件

- `isr.asm`：中断向量和启动相关汇编文件。

## 使用规则

- 一般业务开发不修改这里。
- 只有调整启动向量、中断入口、存储模型或 Keil 启动配置时才需要改。
- App、BoardDevices、Components 的业务逻辑不要放入 Startup。
