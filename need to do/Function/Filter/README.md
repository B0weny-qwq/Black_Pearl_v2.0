# Filter 低通滤波模块

`Code_boweny/Function/Filter/` 提供面向三轴陀螺仪和三轴地磁数据的软件低通滤波接口。

## 特点

- 使用一阶 IIR 低通滤波。
- 内部状态采用 Q8 定点整数。
- 不依赖浮点运算。
- 陀螺仪和地磁计各自维护独立三轴状态。
- 首帧有效数据直接写入状态，首次输出等于输入。
- 原始传感器读取接口保持不变，滤波读取通过新增 API 接入。

## 文件结构

```text
Code_boweny/Function/Filter/
├── Filter.h      # 宏定义、API 声明、Doxygen 注释
├── Filter.c      # 低通滤波实现
└── README.md     # 本文档
```

## 低通公式

模块内部使用一阶 IIR 公式：

```c
state += (((input << Q) - state) >> shift);
output = state >> Q;
```

默认配置：

```c
#define FILTER_LPF_STATE_Q       8
#define FILTER_GYRO_LPF_SHIFT    2
#define FILTER_MAG_LPF_SHIFT     2
```

说明：

- `Q=8` 表示内部状态保留 8 位小数。
- `shift` 越大，滤波越平滑，但响应越慢。
- 当前陀螺仪和地磁计默认都使用 `shift=2`。

## API

```c
void Filter_ResetGyroLowPass(void);
void Filter_ResetMagLowPass(void);

s8 Filter_GyroLowPass(int16 in_x, int16 in_y, int16 in_z,
                      int16 *out_x, int16 *out_y, int16 *out_z);

s8 Filter_MagLowPass(int16 in_x, int16 in_y, int16 in_z,
                     int16 *out_x, int16 *out_y, int16 *out_z);
```

返回值：

- `0`：成功
- `-1`：空指针或输入帧无效

输入帧无效判定：

- 三轴全为 `0`
- 三轴全为 `-1`

无效输入不会推进内部滤波状态。

## 调用链路

```text
QMI8658_ReadGyroFiltered()
  -> QMI8658_ReadGyro()
  -> Filter_GyroLowPass()

QMC6309_ReadXYZFiltered()
  -> QMC6309_ReadXYZ()
  -> Filter_MagLowPass()
```

## 注意事项

- STC32G 无 FPU，本模块严格使用定点整数实现。
- 传感器重新初始化后，应复位对应滤波状态，避免沿用旧状态。
- 批量调参时优先调整 `FILTER_GYRO_LPF_SHIFT` 和 `FILTER_MAG_LPF_SHIFT`。
