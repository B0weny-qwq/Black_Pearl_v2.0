# Components 层说明

`Components/` 是纯算法层。代码应只依赖输入数据和自身状态，不访问硬件、不调用 BoardDevices、不包含 STC 或平台寄存器头文件。

## 当前组件

- `AHRS`：IMU/MAG 姿态解算，输出 roll/pitch/yaw 和 gyro 状态。
- `HeadingEstimator`：gyro 积分 + 磁航向融合的船头航向估计。
- `MagCompass`：QMC6309 磁航向计算、安装偏角、稳定门限。
- `Filter`：IMU/MAG 低通滤波。
- `PID`：定点 PID 控制器。

## 对接规则

- 新增算法放在这里，输入输出用普通结构体或基础类型。
- 算法参数优先由 App 配置或初始化函数传入。
- 不在这里读 GPS、IMU、MAG、电机或无线硬件。
