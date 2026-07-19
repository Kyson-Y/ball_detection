# MPU6050 / MPU6500-compatible IMU

最后核对：2026-07-19

## 1. 接线与身份

| IMU | 天猛星 MSPM0G3507 | 说明 |
| --- | --- | --- |
| VCC | 3.3 V | 禁止接 5 V 逻辑电平 |
| GND | GND | 与主控可靠共地 |
| SDA | `PA0 / I2C0_SDA` | 与 OLED、TFmini-S 共总线 |
| SCL | `PA1 / I2C0_SCL` | 400 kbit/s |

`AD0` 当前为低，7 位地址 `0x68`；`INT/XDA/XCL` 本阶段不接。实机
`WHO_AM_I=0x70`，因此按 MPU6500-compatible Profile 处理，不按 ICM42688 宣称。

## 2. 驱动配置

- 内部采样目标 100 Hz，陀螺量程 +/-500 dps，加速度量程 +/-4 g，DLPF 42 Hz。
- 上电状态机为 `PROBE -> RESET_WAIT -> SETTLING -> CALIBRATING -> READY`。
- 启动校准要求连续 300 个静止样本；移动或加速度模长越界会重新累计。
- 连续 3 次采样失败后清除有效快照并回到 1 秒周期重新探测，不在控制路径无限等待。
- `ImuService` 是快照唯一写入者；type 11 遥测以 25 Hz 发布，包含三轴加速度、加速度模长、
  滤波角速度、温度、校准进度、状态、地址、`WHO_AM_I` 和采样成功/失败计数。
- Health issue 19/20 分别表示 `IMU_OFFLINE` 和 `IMU_STALE`，不会复用编码器 issue ID。

## 3. 共享总线调度

IMU、TFmini-S 和 OLED 共用 I2C0。ServiceTask 优先处理到期 IMU/TFmini 事务，再启动下一块
UART DMA；UART DMA 块限制为 160 B。OLED 整屏刷新拆为可续传的 7 字节短块，每块之间释放
quiet window，避免原先一次约 40 ms 的整屏事务挤掉 IMU 样本。

## 4. 最终实测

- 60 秒无断点联合采集：IMU 实际 sample sequence 速率 `100.000 Hz`，sample failure 0。
- 状态 `READY`，地址 `0x68`，`WHO_AM_I=0x70`，校准 `300/300`。
- 最后样本：加速度 `[-0.15967, -0.00024, 1.00830] g`，模长 `1.02086 g`；
  滤波角速度 `[-0.01134, 0.01795, 0.02325] dps`；温度 `29.61 C`。
- 同次 I2C error、deadline、Health active/sticky、设备端 drop 均为 0；OLED online，
  actuator output permitted 0。
- 全量构建 FreeRTOS/App 为 0 Error / 0 Warning；最终 HEX SHA-256 为
  `FC49A2B41F82353129A71677E0ECADA2CA4EC5FF688FD16A61D199180220386A`。

## 5. 未关闭项

- 还未执行三轴正反转方向确认、六面加速度标定、温度循环和真实物理断线恢复。
- 六轴 IMU 没有外部航向观测时，长期 yaw 不可观测；不能把静态零漂测试等同于姿态或
  云台长期航向精度通过。后续需结合磁力计、视觉或其他航向参考。
