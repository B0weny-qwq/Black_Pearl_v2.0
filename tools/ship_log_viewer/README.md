# Black Pearl 串口日志上位机使用说明

## 1. 这是什么

这个目录是船控板串口日志上位机。

用途不是“抓全协议”，而是给现场调试人员快速确认下面几类问题：

- 船控板是否正常启动
- 遥控器是否已经配对
- 遥控器是否真的在线
- 左右遥杆输入是否被固件正确收到
- 实际左右输出油门是否按预期下发
- AHRS / Heading / 地磁 / 陀螺仪是否稳定
- 船头朝向是否像手机指南针一样随船体旋转
- GPS 状态、卫星数、航向角、返航点、目标点、返航开关是否正常
- 按键动作是否已经被固件识别

---

## 2. 目录结构

本目录下主要文件：

- `ship_log_viewer.html`
  上位机主页面
- `start_ship_log_viewer.py`
  本地 HTTP 服务启动器
- `start_ship_log_viewer.bat`
  Windows 双击启动入口
- `start_ship_log_viewer.ps1`
  PowerShell 启动入口

---

## 3. 推荐打开方式

### 3.1 现场推荐

直接双击：

- `tools/ship_log_viewer/start_ship_log_viewer.bat`

### 3.2 PowerShell

在当前目录运行：

```powershell
.\start_ship_log_viewer.ps1
```

### 3.3 启动后会发生什么

启动脚本会自动：

1. 在本机 `127.0.0.1` 上起一个临时 HTTP 服务
2. 自动挑一个空闲端口，默认从 `8000` 开始
3. 自动打开浏览器
4. 跳转到上位机页面

终端里会打印类似：

```text
[viewer] Black Pearl serial log viewer is running
[viewer] Root: C:\...\Black_Pearl_v2.0
[viewer] URL : http://127.0.0.1:8000/tools/ship_log_viewer/ship_log_viewer.html
```

---

## 4. 为什么不要直接双击 HTML

不要直接双击 `ship_log_viewer.html`。

原因：

- 浏览器串口权限依赖 Web Serial
- Web Serial 必须运行在安全上下文
- `file://` 本地文件方式通常拿不到串口权限

所以必须通过启动脚本打开，让页面运行在：

- `http://127.0.0.1:xxxx`
- 或 `http://localhost:xxxx`

---

## 5. 使用前准备

### 5.1 浏览器

推荐：

- Chrome
- Edge

不建议用老旧浏览器或不支持 Web Serial 的浏览器。

### 5.2 Python

启动脚本依赖：

- `py`
- 或 `python`

任意一个能在命令行里调用即可。

### 5.3 串口线

确保：

- 船控板已经上电
- USB 转串口驱动已安装
- Windows 设备管理器里能看到对应 COM 口

---

## 6. 页面怎么用

### 6.1 顶部工具栏

- `选择串口`
  先选择 COM 口
- `连接`
  连接串口开始收日志
- `断开`
  停止读取
- `清空`
  清除当前页面状态和日志显示

串口参数默认：

- 波特率 `115200`
- 数据位 `8`
- 停止位 `1`
- 校验 `none`

如果固件没有改串口设置，就不要动这些参数。

---

## 7. 页面上每块看什么

## 7.1 联调摘要区

### `AHRS R/P/Y`

显示：

- roll
- pitch
- yaw

用途：

- 看姿态解算是否在刷新
- 看姿态是否出现突跳

### `陀螺仪数据`

显示 `gX / gY / gZ`

用途：

- 看 IMU 是否真的在输出角速度
- 看静止时 gyro 是否接近 0

说明：

- 某些 AHRS 日志帧可能不带 `g=`
- 页面不会因为单帧缺少 `g=` 就把旧数据清掉

### `Yaw 状态`

显示：

- `ys=...` 时表示还在等待锁定
- `yr=...` 时表示相对 yaw 已建立

用途：

- 看 yaw 是否已经进入可控状态

### `AHRS Flags`

显示 AHRS flag 字段和磁相关状态。

用途：

- 看 acc / mag / bias / ready 是否成立

### `HDG Debug`

显示：

- `y` 或 `yr`
- `yg`
- `ym`

meta 里还会显示：

- `gz`
- `bz`
- `mv`
- `mu`
- `sf`
- `err`
- `hp`
- `hd`
- `hm`

用途：

- 现场判断磁航向是否参与修正
- 看 `mu` 是否真的在用磁修正
- 看 `ym` 是否漂
- 看 `err` 是否过大

### `船头朝向`

显示一个类似手机指南针的船头朝向盘：

- 主数值：`0..359.9°` 和 `N / NE / E / SE / S / SW / W / NW`
- 指针：按当前船头绝对航向旋转
- 小字：显示航向来源和关键稳定字段

数据优先级：

- 优先使用 `[HDG] I: y=...`，页面标记为 `HDG fused`
- `HDG fused` 是陀螺仪短期积分 + 稳定磁力计校正后的融合绝对航向，也是 GPS 定点巡航应使用的船头方向
- `[MAG] I: test ... compass=... stable=...` 只在还没有 `HDG fused` 时作为调试参考，页面会标记为 `MAG compass / debug only`
- `[MAG] I: raw=... yaw=...` 同样只作为低优先级调试参考

现场判断：

- 如果 `船头朝向` 显示 `HDG fused`，转动船体时指针应像手机指南针一样连续变化
- 当前船端已按实测写入磁罗盘修正：船头实际正北时原始罗盘约 `219.3°`，并且原方向与手机指南针相反；固件用 `MAG_COMPASS_DIRECTION_SIGN=-1` 和 `MAG_COMPASS_INSTALL_OFFSET_CD=21930` 统一修正
- 如果只显示 `MAG compass`，说明还没看到融合航向，不能直接证明 GPS 定点巡航可用
- `stable=1` 只表示磁力样本经过门限/IIR 后可作为修正参考，不代表单次磁力读数直接控制船头

### `配对状态`

只表示无线配对层状态。

注意：

- “已配对”不等于遥控器正在在线发控制

### `遥控链路`

显示层按遥控空口活动刷新：收到完整 `AA ... BB` 头尾包就认为遥控链路有活动，不要求必须是 `0x11`。

注意：

- `AA ... BB` 用于上位机显示在线/活动。
- 有效 `0x11` 用于控制输入和固件电机安全保活。
- 不要把“显示在线”和“控制帧保活”混成一个状态。

状态来源：

- `remote link online by aa-bb-frame cmd=...`
- `remote link timeout by aa-bb-frame, dt=...ms`
- `[WL] I: rx pkt ... data=AA ... BB`
- `[SHIP] I: rc input cmd=0x11 ...`
- `[SHIP] I: manual control online by cmd=0x11`
- 页面本地超时判定

这个比“配对状态”更重要。

日志到页面字段对照：

| 日志 | 页面更新 |
|------|----------|
| `[WL] I: rx pkt ... data=AA ... BB` | `遥控链路=在线`，刷新页面在线计时 |
| `[SHIP] I: remote link online by aa-bb-frame cmd=...` | `遥控链路=在线`，显示触发命令 |
| `[SHIP] I: pair ok by aa-bb-frame cmd=... work_rx=... key=.../...` | `配对状态=已配对`，更新工作信道/key，同时刷新遥控在线 |
| `[SHIP] I: aa-bb xor ok cmd=... data_len=...` | 更新最近收到命令，并刷新遥控在线 |
| `[SHIP] I: rc input cmd=0x11 ...` | 兼容旧日志：更新按键/动作推断，并刷新遥控在线 |
| `[SHIP] I: manual control online by cmd=0x11` | 标记手动控制上线，并刷新遥控在线 |
| `[SHIP] W: remote link timeout by aa-bb-frame, dt=...ms` | `遥控链路=离线` |
| `[SHIP] W: manual control timeout by cmd=0x11, dt=...ms` | 只提示 `0x11` 控制帧超时停机，不单独把页面遥控链路改离线 |

### `电机输出`

来源：

- `[CTRL] I: mode=... tgt=... err=... pid=... df=... th=... base=... l=... r=...`
- `[SHIP] I: pwm pins mla=... mlb=... mra=... mrb=... period=...`

用途：

- 看控制器最后到底下发了多少
- 看左右输出是否一致
- 看 yaw hold / manual hold 是否叠加了差速
- 主数值显示为左右两行百分比：`左 +18%` / `右 +18%`，负数表示反向输出

这个卡片显示的是最终输出，不再依赖遥控器连续采样日志。

### `控制模式`

来源：

- `[CTRL] I: mode=... tgt=... err=... in=... pid=... diff=... throttle=... base=... steer=... left=... right=...`
- `[DATA] I: cruise run req=... base=... l=... r=... err=... pid=... diff=... tgt=...`
- `key action=E cruise-high / cruise-stop`

用途：

- 看当前最终控制权在 `STOP / MANUAL_OPEN_LOOP / MANUAL_YAW_HOLD / CRUISE_HEADING_HOLD / GPS_NAV_HEADING_HOLD / FAILSAFE_STOP` 哪个模式
- 看 E 键是否真的进入定速巡航
- 看定速巡航是否按“再按 E”或“摇杆反向拉到底附近”退出
- 看 yaw 自稳目标角、误差、PID 输出、左右电机目标是否在变化

现场检验：

- 直推且左右输出差小于 20%，若航向 ready，应看到 `MANUAL_YAW_HOLD`
- 明显打方向，应回到 `MANUAL_OPEN_LOOP`
- 带符号油门 `>= +60` 时按 E，应看到 `CRUISE_HEADING_HOLD`
- 定速巡航中再按一次 E，应看到 `STOP`，时间线显示 `cruise-toggle-stop`
- 定速巡航中带符号油门 `<= -50`，应看到 `STOP`，时间线显示 `cruise-reverse-stop`

### `自动驾驶`

来源：

- `dispatch cmd=0x13/0x14/0x15`
- `[SHIP] I: tx cmd=0x16 state=... mode=... sw=... reason=... gps=... sat=... dist=...`

用途：

- 看返航点、目标点、返航开关命令是否被固件收到
- 看 AutoDrive 当前状态、模式、触发原因、GPS 是否 ready、卫星数和目标距离
- 判断“收到命令但没有跑起来”是 GPS 不 ready、卫星数不足、距离不满足，还是已经到点/超时

### `动作判定`

来源：

- `[SHIP] I: manual parse ... target=... speed=...`
- `[SHIP] I: manual motion=... left=... right=...`
- `[SHIP] I: manual motion=stop reason=...`

用途：

- 看当前被识别成 `forward / backward / left / right / stop`
- `[MOT]`、`pwm pins`、`yaw hold` 只更新“电机输出”，不再覆盖动作判定
- 小字字段按 `speed / throttle / steering` 或 `left / right` 分行显示，避免挤成一行
- 重复的 `stop/manual center` 会在时间线上做 1 秒限流，避免中位帧持续输入时刷屏

### `电量采样`

来源：

- `[SHIP] I: adc p0.0 raw=... adc_mv=... bat_mv=... power_level=...`
- `0x12` 的 `power=0x..` 字节

`power_level` 是 `0..4` 等级，不是原始 ADC。

### `地磁数据`

来源：

- `[MAG] I: test raw=... ... norm1=...`
- `[MAG] I: raw=... ... norm=... yaw=... self=...`
- `[HDG] I: ... ym=... mv=... mu=...`

当前固件启用 `ENABLE_MAG_STANDALONE_POLL=1`，每秒独立输出一次地磁原始读数，便于确认上位机能看到地磁。

### `按键状态`

来源：

- `0x11` 中的 key
- `key action=A/B/C/D/E`

特点：

- 收到按键相关日志后立即刷新
- 不等统一节拍

用途：

- 现场核对遥控器按键是否被正确识别
- 看长按、脉冲、巡航动作是否真的触发

### `状态回包`

来源：

- `tx cmd=0x12`

用途：

- 看当前发回遥控器的状态包是否正常

### `电量采样`

来源：

- `adc p0.0 raw=... adc_mv=... bat_mv=...`

用途：

- 看 ADC 是否正常
- 看电池电压是否过低

### `地磁数据`

优先来源：

- `HDG` 里的 `ym / mv / mu / err`

辅助来源：

- `[MAG] I: test raw=...`

用途：

- 看地磁当前是否有效
- 看磁修正是否真的参与
- 看磁航向是否稳定

---

## 7.2 GPS 摘要区

### `定位有效`

来源：

- `gps state fix=...`

### `卫星数`

来源：

- `gps state`
- `gps sat source`

### `经度 / 纬度`

来源：

- `gps state lon=... lat=...`

### `航向角`

来源：

- `gps state angle=...`
- `tx cmd=0x12 angle=...`

### `最近 0x12`

显示最近回包：

- 通道
- payload 长度
- 卫星数
- angle
- power

### `遥控器旧格式`

显示老协议拆分后的：

- `lon1/lon2`
- `lat1/lat2`
- payload bytes

用途：

- 和旧遥控器显示做对照

### `返航点`

来源：

- `dispatch cmd=0x13(return-home)`
- 后续 `coord ...`

### `目标点`

来源：

- `dispatch cmd=0x14(goto-point)`
- 后续 `coord ...`

### `返航开关`

来源：

- `dispatch cmd=0x15(return-switch)`
- 后续 `coord ...`
- 或 `return-switch short len=...`

用途：

- 看上位机是否收到了巡航/返航相关点位

### `GPS 源数据`

显示最原始的 `fix / legacy / sat / lon / lat / angle / seq`。

---

## 7.3 事件时间线

这里不是全量原始日志，而是提炼后的关键事件区。

适合看：

- 配对
- 遥控上线/超时
- 按键动作
- yaw hold
- GPS 状态变化
- 错误和告警

支持：

- 自动滚动
- 限制可见条数

---

## 7.4 离线日志解析

支持两种方式：

1. 直接把日志粘贴进文本框
2. 选择 `.log / .txt / .csv` 文件

适合：

- 现场回传日志复盘
- 不接串口时分析问题

---

## 7.5 原始日志区

这是尽量保留原貌的原始串口日志。

支持：

- 自动滚动
- 只看关键事件
- 限制可见条数

用途：

- 对照固件真实输出
- 页面解析不对时回看原始日志

---

## 8. 页面刷新策略

为了避免高频日志把页面刷卡，页面不是所有东西都同频刷新。

### 8.1 固定节拍刷新

这些数值卡片按固定节拍批量刷新：

- AHRS
- HDG
- 控制模式
- 电机输出
- 电量
- GPS 数值
- 地磁

### 8.2 即时刷新

这些事件一收到就立刻刷新：

- 按键状态
- key action
- 遥控在线 / 超时
- 配对状态变化
- 告警 / 错误

这样做的目的：

- 数值不卡顿
- 按键和状态变化不延迟

---

## 9. 当前兼容日志

### 9.1 系统类

- `[SYS] I/W/E: ...`
- `[WL] I/W/E: ...`
- `[GPS] I/W/E: ...`
- `[IMU] I/W/E: ...`

### 9.2 姿态和航向类

- `[AHRS] I: ...`
- `[HDG] I: ...`
- `[MAG] I: test raw=...`

### 9.3 遥控与动作类

- `[SHIP] I: rc11 on`
- `[SHIP] I: manual parse ...`
- `[SHIP] I: manual motion=...`
- `[SHIP] I: key action=...`
- `[SHIP] I: key pulse ...`
- `[SHIP] I: yaw hold ...`
- `[SHIP] I: pwm pins ...`
- `[MOT] I: ...`

### 9.4 GPS / 协议类

- `[SHIP] I: tx cmd=0x12 ...`
- `[SHIP] I: gps state ...`
- `[SHIP] I: gps sat source ...`
- `[SHIP] I: gps payload oldfmt ...`
- `[SHIP] I: gps payload bytes=...`
- `[SHIP] I: dispatch cmd=0x13/0x14/0x15 ...`
- `[SHIP] I: coord ...`

---

## 10. 现场推荐排查顺序

## 10.1 上电后先看启动

先看：

- `[SYS]`
- `[WL]`
- `[GPS]`
- `[IMU]`

如果这里已经报错，先别看控制。

## 10.2 看无线配对

看：

- `pair req`
- `pair ok`
- `pair success`

如果没有配对成功，先解决无线链路。

## 10.3 看遥控在线

看：

- `remote link online by aa-bb-frame cmd=...`
- `rx pkt ... data=AA ... BB`
- `remote link timeout by aa-bb-frame, dt=...ms`
- `manual control timeout by cmd=0x11, dt=...ms` 只表示控制帧超时停机，不等于页面必须显示遥控离线

如果只有“已配对”但没有“遥控在线”，说明上位机最近没有看到完整 `AA ... BB` 遥控包。

如果“遥控在线”但电机停机，再看 `0x11` 是否持续有效；显示在线不等于控制帧保活。

## 10.4 看控制模式

看：

- `控制模式`
- `按键状态`
- `动作 / 巡航`

确认 E 键、定速巡航和 CTRL 模式切换是否按预期发生。

## 10.5 看电机输出

看：

- `电机输出`
- `[CTRL]`
- `动作 / 巡航`

确认输入和输出是否一致，是否被自动控制叠加。

## 10.6 看姿态与航向

看：

- `AHRS`
- `HDG`
- `地磁数据`

重点确认：

- 静止时 gyro 是否接近 0
- `ym` 是否稳定
- `mu` 是否符合预期

## 10.7 看 GPS

看：

- `fix`
- `sat`
- `lon / lat`
- `0x12`
- `返航点 / 目标点 / 返航开关`

---

## 11. 常见现象解释

### 11.1 “已配对”但“遥控离线”

说明：

- 无线已经建立工作信道
- 但上位机最近没有看到完整 `AA ... BB` 遥控包

优先检查：

- 遥控器是否开机
- 遥控器是否真的在发
- 是否配对后未进入正常控制状态
- 如果能看到 `AA ... BB` 但仍显示离线，优先修上位机在线判定，不要只用 `0x11` 当在线依据

### 11.2 遥控输入有变化，但实际输出不变

说明可能是：

- 被自动控制接管
- 当前逻辑门控未满足
- 固件限幅
- 输出被 stop / timeout / safety gate 压住

优先看：

- `动作判定`
- `[MOT]`
- `yaw hold`
- `warn / error`

### 11.3 GPS 有卫星，但定位无效

说明可能是：

- 有 NMEA 数据，但没有有效 fix
- 室内环境或天线条件差

优先看：

- `gps state fix=0`
- `sat source`

### 11.4 地磁漂，但船还能控

说明：

- 当前主要靠 gyro / yaw 闭环
- 磁修正可能被 gate 掉了

优先看：

- `mv`
- `mu`
- `ym`
- `err`

---

## 12. 离线复盘建议

如果现场日志很多，建议：

1. 先用“只看关键事件”
2. 看时间线里有没有配对、遥控上线、按键、超时、错误
3. 再对照原始日志查具体数值

这样效率最高。

---

## 13. 交付给现场人员时建议怎么说

你可以直接告诉对方：

1. 双击 `tools/ship_log_viewer/start_ship_log_viewer.bat`
2. 选串口后点连接
3. 先看“配对状态”和“遥控链路”
4. 再看“控制模式”和“电机输出”
5. 按键是否触发直接看“按键状态”
6. GPS 看右侧摘要区
7. 出问题时把原始日志导出或直接粘贴回来

---

## 14. 相关入口

- 上位机主页面：
  [ship_log_viewer.html](tools/ship_log_viewer/ship_log_viewer.html)
- Windows 启动脚本：
  [start_ship_log_viewer.bat](tools/ship_log_viewer/start_ship_log_viewer.bat)
- PowerShell 启动脚本：
  [start_ship_log_viewer.ps1](tools/ship_log_viewer/start_ship_log_viewer.ps1)
- 兼容跳转页：
  [doc/tools/ship_log_viewer.html](doc/tools/ship_log_viewer.html)
