# TFmini-S 激光测距模块

最后核对：2026-07-19

## 1. 当前型号结论

用户照片中的板内丝印为 `TFmini V1.8.1`，该字符串是 PCB/硬件版本，不是完整产品名。
外壳、四针布局、线序、默认 9 字节输出和 `0x5A` 配置回包均与北醒 TFmini-S 一致。
实机固件版本查询返回原始字节 `07 03 02`，按手册的反向显示顺序为 `2.3.7`。

当前工程因此按 TFmini-S 协议驱动。它不是 TFmini Plus，也不是使用 RS-485/CAN 的
TFmini-i。

## 2. 电气与当前 I2C 接线

TFmini-S 没有反接和过压保护。供电必须为 `5.0 V +/-0.1 V`，不能直接接 4S 电池；
通信电平为 3.3 V LVTTL。

| 模块线色 | TFmini-S 功能 | 当前 MCU | 方向 |
| --- | --- | --- | --- |
| 红 | +5 V | 稳压 5 V | 电源 |
| 黑 | GND | GND | 必须共地 |
| 黄（新版手册为白） | RXD / SDA | `PA0 / I2C0_SDA` | 双向开漏 |
| 绿 | TXD / SCL | `PA1 / I2C0_SCL` | MCU 时钟 |

当前模块已永久切换并保存为 I2C 模式，7 位地址 `0x10`，与 OLED `0x3C` 共用
`PA0/PA1 @ 400 kbit/s`。SDA/SCL 上拉只能接 3.3 V。UART0 的 DAPLink/上位机遥测仍为
`PA10/PA11 @ 230400`；UART1 已不再被 LiDAR 占用。

## 3. UART 数据协议

默认测距帧固定 9 字节：

```text
59 59 Dist_L Dist_H Strength_L Strength_H Temp_L Temp_H Checksum
```

- 多字节字段为小端；默认距离单位是 cm。
- 芯片温度为 `Temp / 8 - 256` 摄氏度，不是环境温度。
- `Checksum` 是前 8 字节累加和的低 8 位。
- 距离 `65535` 表示信号弱，`65534` 表示信号饱和，`65532` 表示环境光饱和。
- 工程会流式重同步帧头，并分别统计测距帧和 `0x5A` 命令回包的校验错误。

固件版本只读命令为 `5A 04 01 5F`。UART bring-up 阶段查询得到 `2.3.7`；切换 I2C 后，
正式固件不会再次自动发送接口切换或保存命令。一次性迁移逻辑由
`TFMINI_S_ENABLE_UART_TO_I2C_MIGRATION` 控制，正式构建固定为 0。

UART0 telemetry frame type 10 以 20 Hz 输出距离、强度、芯片温度、设备原始帧率、在线状态、
无效值、校验错误、超时、UART 溢出和固件版本。

## 4. 已完成的 I2C 迁移

TFmini-S 的 UART 与 I2C 复用同一对模块信号线；I2C 默认 7 位地址为 `0x10`，最高
400 kbit/s。迁移时已在 UART1 接线下发送接口切换 `5A 05 0A 01 6A` 和保存
`5A 04 11 6F`，两条命令均收到合法回包。掉电后改接 PA0/PA1，冷启动 I2C 读取成功。

当前共总线分配：

| 功能 | MCU | 现有设备 |
| --- | --- | --- |
| SDA | `PA0 / I2C0_SDA` | OLED `0x3C` + TFmini-S `0x10` |
| SCL | `PA1 / I2C0_SCL` | 400 kbit/s 共用总线 |

已执行的迁移步骤：

1. 当前 UART 接线下发送接口切换 `5A 05 0A 01 6A`。
2. 发送保存设置 `5A 04 11 6F`，至少等待 1 秒。
3. 断电，把黄线改接 PA0/SDA、绿线改接 PA1/SCL；上拉只能到 3.3 V。
4. 重新上电，以地址 `0x10` 发送取数命令 `5A 05 00 01 60`，读取同样的 9 字节结果。

TFmini-S 的 I2C 取数命令写入后必须等待 1 ms 再读取 9 字节结果。普通寄存器设备（例如
MPU6xxx）使用无额外延时的 repeated transaction API，不能共用 TFmini 的 1 ms 延时语义。

## 5. 当前实测基线

- I2C + MPU + OLED 最终 60 秒联合运行：TFmini 设备帧率 `49.993 Hz`，累计测距帧全部通过
  checksum，timeout 0；I2C error 0，OLED online 且持续刷新。
- 当前激光正对近距离目标，I2C 样本为 0 cm、强度约 670，符合落入约 10 cm 盲区时的现象；
  仍需把目标放到 10–30 cm 以外补一次非零距离值验收。
- 同次 IMU 100 Hz、SystemTask 周期 9998–10002 us、deadline 0、Health active/sticky 0，
  设备端 telemetry/serial drop 0，actuator output permitted 0。
- COM9 主机采集仍有 CRC/gap；设备端计数全零，且与既有红外 AD0/AD1/AD2 高频切换串扰
  一致。正式线束必须修复该电气耦合，不能把主机端链路描述为生产可用。

- 20 秒不间断运行：2007 个测距帧，2007 个有效值，0 无效、0 校验错误、0 超时、
  0 UART 溢出；设备帧率约 97.9 Hz。
- 版本查询：1 次发送、1 次合法回包，固件 `2.3.7`。
- 当前目标静态 10 个一秒间隔样本：距离 103–104 cm，均值 103.9 cm；强度
  2328–2337；芯片温度 56.00 C。
- 10 次 SWD 强制暂停采样的最后一次累计 1 个校验错误，该错误受调试暂停干扰，不作为
  不间断链路错误；生产验收以 20 秒无断点结果为准。
- 同一 20 秒测试中 SystemTask 2041 次、ServiceTask 20415 次，deadline/fault 为 0；
  左右 PWM、执行器输出和 output permitted 全为 0。

资料来源：`E:\嵌入式\数据手册\TFmini-S` 下的用户手册、规格书和 I2C Pixhawk 应用说明。
