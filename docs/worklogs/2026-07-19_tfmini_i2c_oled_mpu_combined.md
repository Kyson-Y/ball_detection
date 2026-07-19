# 2026-07-19 TFmini-S I2C、OLED 与 MPU6xxx 联合驱动

性质：normal

## 目标与边界

- 在当前 513X-4S 固件中完成 TFmini-S I2C、SSD1306 OLED 和 MPU6xxx 共享总线驱动。
- 所有电机目标、PWM、armed 和 output permitted 保持为 0，不执行运动输出。
- 开始 HEAD `3113074`，分支 `refs/heads/codex/513a-motor-bringup`。
- 保留用户已有 `docs/hardware/ECHO_WIRING_GUIDE.md` 修改与忽略的 `tmp/`，不覆盖或暂存。

## 硬件与根因

- TFmini-S：5 V 供电，黄线 SDA -> PA0，绿线 SCL -> PA1，地址 `0x10`。
- OLED：3.3 V，PA0/PA1，地址 `0x3C`。
- IMU：3.3 V，PA0/PA1，地址 `0x68`，`WHO_AM_I=0x70`。
- 初始总线 PA0/PA1 均为低，用户随后确认 SCL 误插 GND。SCL 改回 PA1 后 I2C 恢复，
  因此本次根因是接线，不是 I2C 控制器或协议驱动。

## 实现

- TFmini-S 一次性 UART 迁移发送 `5A 05 0A 01 6A` 与 `5A 04 11 6F`，均收到合法回包；
  正式开关恢复为 0。
- BSP 增加互斥、超时、恢复统计的 write-read API，并为 TFmini 单独保留 1 ms 延时版本。
- 移植 MPU6050/MPU6500-compatible 驱动与 ImuService：100 Hz、+/-500 dps、+/-4 g、
  42 Hz DLPF、300 点静止校准、连续 3 次失败重探测。
- 新增 type 11 / 64 B IMU payload、25 Hz 发布和主机解析；新增 Health issue 19/20。
- UART DMA 由 ServiceTask 统一启动，单块上限 160 B；TFmini 周期使用固定 20 ms 基准，
  避免等待静默窗口的延迟累加成频率漂移。
- OLED 运行期刷新改为保留进度的 7 字节 step 状态机，每步独立申请/释放 priority quiet window。
- ServiceTask 栈从 256 增至 320 words；最终最低余量 128 words。

## 验证

- 遥测、TFmini、供电 ADC 和新增 IMU 四份 PowerShell fixture 均通过。
- FreeRTOS/App full rebuild：0 Error / 0 Warning；Code=77,372，RO=3,428，RW=188，
  ZI=19,044；HEX SHA-256
  `FC49A2B41F82353129A71677E0ECADA2CA4EC5FF688FD16A61D199180220386A`。
- CMSIS-DAP `2b5d6f2a` / 500 kHz program/verify/reset 通过；OpenOCD 目标 CRC 算法仍偶发
  halt timeout，期间使用过逐字节回读回退并得到一致哈希，最终候选 fast verify 通过。
- 最终 60 秒：IMU sample rate `100.000 Hz`，TFmini device rate `49.993 Hz`；IMU
  `READY/300 of 300`、sample failure 0；TFmini checksum/timeout 0。
- I2C error 0，OLED online 且累计持续刷新，Health active/sticky 0，SystemTask period
  9998–10002 us，max jitter 2 us，deadline 0。
- 设备端 publish/transport/serial TX drop 与 RX overflow 均为 0；actuator output permitted 0。

## 剩余风险

- 主机 60 秒捕获有 338 CRC、421 sequence gap；设备端错误计数全零，且与既有 125 Hz
  红外地址线切换耦合一致。正式线束需串联阻尼、短线/共地复核或硬件隔离后重新验收。
- TFmini 当前目标位于盲区，距离为 0 cm；需把目标放到 10–30 cm 之外补非零 I2C 距离。
- IMU 三轴方向、六面标定、温度循环、物理断线恢复和长期航向精度未验收。
