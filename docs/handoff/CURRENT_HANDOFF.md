# ECHO 当前交接

```yaml
handoff_schema: 1
updated_at: 2026-07-19T23:04:40+08:00
updated_by: Codex
status: code_complete_hardware_followup
```

## 0.000000 当前任务：TFmini-S I2C + OLED + MPU6xxx 联合驱动收尾

- worktree `C:\Users\Auror\ECHO-513a-work`，分支
  `refs/heads/codex/513a-motor-bringup`，开始/当前未提交 HEAD `3113074`。
- 用户已有 dirty `docs/hardware/ECHO_WIRING_GUIDE.md`，其中 PB8 行为用户内容，禁止覆盖、
  还原或暂存；`tmp/` 保持忽略且不进入 Git。
- TFmini-S 已永久迁移到 I2C：黄线 PA0/SDA、绿线 PA1/SCL、红线受控 5 V、黑线 GND，
  地址 `0x10`；OLED `0x3C` 与 IMU `0x68` 共用 I2C0 400 kHz。
- 初始失败根因为用户把 SCL 插到 GND；改回 PA1 后总线恢复。禁止把该问题归因于驱动。
- IMU 实机 `WHO_AM_I=0x70`，按 MPU6500-compatible 驱动；100 Hz、+/-500 dps、+/-4 g、
  42 Hz DLPF、300 点启动静止校准。type 11 以 25 Hz 发布，Health issue 19/20 已接入。
- OLED 使用可续传 7 字节 step 刷新；UART DMA 由 ServiceTask 启动、单块最多 160 B；
  TFmini 使用固定 20 ms 基准并保留写后读 1 ms 延时。
- 最终全量构建 0 Error / 0 Warning，Code=77,372，ZI=19,044；HEX SHA-256
  `FC49A2B41F82353129A71677E0ECADA2CA4EC5FF688FD16A61D199180220386A`；
  CMSIS-DAP `2b5d6f2a` / 500 kHz program/verify/reset 通过。
- 最终 60 秒：IMU `100.000 Hz`、TFmini `49.993 Hz`；IMU sample failure 0，TFmini
  checksum/timeout 0，I2C error 0，OLED online/持续刷新，Health active/sticky 0，
  deadline/设备端 drop 0，actuator output permitted 0。
- 最后 IMU：READY、300/300、`0x68/WHO_AM_I 0x70`、29.61 C、accel norm 1.02086 g、
  filtered gyro `[-0.01134, 0.01795, 0.02325] dps`。
- 未关闭：主机 COM9 因红外地址线切换耦合仍有 CRC/gap；TFmini 目前对着盲区目标为 0 cm；
  IMU 方向/六面/温漂/物理断开未测。下一步先把目标移出盲区，再处理 UART 电气串扰。
- 四份 host fixture 通过，PowerShell parser 语法和 `git diff --check` 通过。本次只显式暂存
  任务文件并创建 scoped commit；禁止 `git add .`、禁止 push。

## 0.00000 当前任务：TFmini-S I2C 实机测试

- 用户确认已接好 I2C：TFmini-S `SDA -> PA0/I2C0_SDA`、`SCL -> PA1/I2C0_SCL`，
  红线为受控 5.0 V、黑线共地；OLED 继续共享 I2C0，总线地址 `0x3C`。
- 当前 HEAD：`3113074`，分支：`refs/heads/codex/513a-motor-bringup`；
  用户已有 dirty 文件 `docs/hardware/ECHO_WIRING_GUIDE.md`，禁止覆盖、还原或暂存，`tmp/` 保持未跟踪。
- 本次目标：先只读探测 TFmini-S 默认 7 位地址 `0x10`，再以保守周期发送
  `5A 05 00 01 60` 并读取 9 字节测距帧；记录 ACK/NACK、超时、校验和帧率，同时验证 OLED 在线。
- 不发送 UART/I2C 模式切换或保存命令；若 `0x10` NACK，则判定模块仍处于 UART 模式并停止 I2C 读取。
- 两路电机目标、PWM、armed 和 output permitted 必须保持为零；本次不执行任何运动输出。
- BSP 需要补充互斥保护的 I2C 写后读 API，复用现有超时、错误统计和总线恢复；
  ServiceTask 改为 I2C 传输，保留现有 TFmini-S 解析器和 type 10 遥测格式。
- 已完成 I2C 写后读 BSP、20 ms TFmini-S I2C 轮询和命令构造夹具；UART1 自动查询已停用。
- FreeRTOS/App 全量重建均为 0 Error / 0 Warning；Code=73,088，ZI=18,508。
- HEX SHA-256 为 `934E635D95620E55F6676610FABAAFC678A3FC0CEE858CE337896B54E3FA0B64`；
  CMSIS-DAP `2b5d6f2a` / 500 kHz program/reset 通过，76,488 B Flash 逐字节回读
  SHA-256 `D64187C95B313AB98A0F5C9F7E4E11D2EE7BA9D0CFE9373138BCDBCDA5E20E2D` 与构建一致。
- 20 秒同一 OpenOCD 会话实测：`0x10` 写后读 1019 次、写成功 0、读成功 0，
  先出现 44 次 NACK，随后因 TFmini-S 的 UART TX 接在 SCL 上产生 1017 次 bus-busy；
  TFmini 数据帧/有效帧/校验错误均为 0。确认模块内部仍处于 UART 模式，不是 I2C 接口。
- 同次 OLED 初始化 21 次、成功 0，地址为 0，确认共享总线被 UART TX 拖住；
  Health active/sticky `0x00001800`，对应 I2C error + OLED offline。
- 同次 SystemTask/ServiceTask 为 2040/20400，系统周期 9999--10001 us、最大 jitter 1 us、
  deadline/fault 均为 0；左右目标、实际 PWM、normalized output、armed/output permitted 全为 0。
- 当前阻塞于物理接线：需临时恢复 UART1，`PA8/TX -> 黄线/RXD`、`PA9/RX <- 绿线/TXD`，
  然后发送切 I2C `5A 05 0A 01 6A`、保存 `5A 04 11 6F`，等待至少 1 秒并掉电重启；
  再接回 `PA0/SDA`、`PA1/SCL` 复测。永久切换命令尚未发送。
- 21:32 用户回复 `OK`，按 UART1 已恢复接线处理。已加入一次性迁移构建开关和状态机：
  必须先收到至少 5 个有效 UART 测距帧，才发送切 I2C；100 ms 后发送保存，500 ms 后停止，
  不循环发送。迁移固件尚未构建、烧录，永久命令仍未发送。
- 一次性迁移固件 0 Error / 0 Warning，76,248 B Flash 逐字节回读一致；运行遥测确认迁移前
  收到 11 个 UART 测距帧（9 个有效），随后收到 2 个合法命令回包、0 命令校验错误，
  UART 测距流停止且 timeout 置位。确认切 I2C 和保存命令均被模块接受。
- 迁移开关已恢复为 0；正式 I2C 固件正在重新构建，尚未完成烧录和 I2C 复测。
- 正式 I2C 固件 0 Error / 0 Warning，HEX SHA-256
  `9970064D58F5C55234A72CDDA0079D99ED71C2AFD50B511C7D714AD7044A9E6B`；
  已烧录，76,488 B Flash 回读 SHA-256
  `9E247703564C941DABA3389F5C885DE002C2089AFD2123323F23B05D852169C1` 与构建一致。
- 当前板上为正式 I2C 固件。下一步需要用户断电，把黄线从 PA8 移到 PA0/SDA、绿线从 PA9
  移到 PA1/SCL，红线 5 V、黑线 GND 不变，再重新上电；随后补 I2C 持久化冷启动和连续稳定性实测。
- 用户重新上电后的多轮遥测仍为 I2C success 0、TFmini frame 0、OLED offline；其中两轮因用户说明
  模块未上电而作废。模块确认上电后的 5 秒样本仍无 ACK。SWD 读取 `GPIOA.DIN31_0` 显示
  PA0/PA1 空闲均为低，定位为总线无有效上拉或被硬件拉低。
- 正在构建诊断固件：PA0/PA1 启用内部弱上拉，I2C 从 400 kHz 降到 100 kHz，并按 TFmini-S
  手册在测距写命令后等待 1 ms 再读。内部上拉仅用于诊断，正式硬件仍应使用外部约 4.7 kOhm 上拉。
- SysConfig 明确拒绝 I2C 开漏复用下的片内上拉，诊断构建判为无效并已撤销该配置；不能绕过工具
  强行依赖内部上拉。保留 100 kHz 和 1 ms 写后读延时。硬件必须在 SDA、SCL 各增加约 4.7 kOhm
  到 3.3 V 的外部上拉，或接入已确认带上拉且已供电的 I2C 扩展板后再测。
- 用户确认此前把 SCL 误插到 GND；改回 `PA1/SCL` 后根因关闭。100 kHz 快测 TFmini-S 约 50 Hz、
  7003 帧累计有效且 0 校验，OLED init/refresh 全成功。恢复 400 kHz 并保留手册要求的 1 ms
  写后读延时后，20 秒联合验收：TFmini 遥测 386 帧、设备累计 2960/2960 有效、0 checksum、
  0 timeout、50.0 Hz；I2C success 20,248/error 0，OLED online，Health active/sticky 0。
- 同次 SystemTask 周期 9997--10003 us、max jitter 3 us、deadline 0；设备端 serial/telemetry drop 0，
  actuator output permitted 0。当前距离 0 cm、强度约 1070，需把目标移出 10 cm 盲区再做距离值验收。
- 用户追加要求今晚完成测距、OLED、陀螺仪驱动。测距与 OLED 链路已通过，正在把已完成 27000 秒
  soak 的 MPU6050/MPU6500-compatible 驱动从独立 spike 语义移植到当前 513X + TFmini 固件。
- 已新增 `mpu6050`、`ImuService`，拆分普通 I2C 写后读与 TFmini 1 ms 延时读，并接入已验证的
  UART priority quiet window；当前尚未构建、烧录或板测 IMU 组合固件。

## 0.0000 当前任务：TFmini-S 激光测距接入

- 当前目标：识别用户照片中的北醒 LiDAR，并在现有 513X 固件中完成低功率测距驱动；
  电机、云台和其他执行器不是本任务目标，保持输出为零。
- 照片板内丝印为 `TFmini V1.8.1`。外壳、四针布局和线序与 TFmini-S 手册一致；
  最终型号仍以 UART 固件版本回包和实机协议响应为准。
- 当前接线按 UART1：MCU `PA8/TX -> LiDAR 黄线/RXD`，
  `PA9/RX <- LiDAR 绿线/TXD`；红线必须为 `5.0 V +/-0.1 V`，黑线共地。
  通信电平为 3.3 V LVTTL，默认 `115200 8N1`。
- TFmini-S 支持 UART、I2C 和 I/O；UART/I2C 复用同一对信号线。计划先用 UART
  读取 9 字节测距帧并查询固件版本，不自动发送保存设置或 I2C 切换命令。
- 后续 I2C 模式默认地址 `0x10`、最高 400 kbit/s；切换后黄线为 SDA、绿线为 SCL，
  需要重新配置 MCU 引脚/上拉并掉电重启，不能在当前 UART 接线下直接切换。
- 开始 HEAD：`e99c464`，分支：`refs/heads/codex/513a-motor-bringup`。
  用户已有 dirty 文件 `docs/hardware/ECHO_WIRING_GUIDE.md`，禁止覆盖、还原或暂存；
  `tmp/` 只保存忽略的资料提取和实测原始证据。
- 已新增 UART1 BSP、TFmini-S 流式解析、固件版本只读查询和 UART0 type 10 遥测；
  SysConfig 生成 0 error / 1 个既有 ProjectConfig warning，FreeRTOS/App full rebuild 为
  0 Error / 0 Warning，Code=72,656，ZI=18,508。
- 旧遥测夹具和新增 TFmini type 10 夹具均运行通过；TFmini C 解析夹具已通过 ArmClang
  `-Werror` 编译，但主机原生运行环境不可用，合成向量执行状态仍为 `not run`。
- 最终 HEX SHA-256 为 `9FDEE670AA28F049945DE41AE510654FC9CF010BF8A656B792947F25AA463DFF`；
  76,056 B 二进制 SHA-256 为
  `7B5FE3ABA8FD12EE37E82819C0FD85A7A8E3F16E61EBE3F93EFA64F64E2A29D9`，
  DAPLink `2b5d6f2a` / 500 kHz Flash 逐字节回读一致。
- 20 秒同一 OpenOCD 会话内无断点实测：2007/2007 有效帧、0 无效、0 校验、0 超时、
  0 UART 溢出，设备帧率约 97.895 Hz；固件版本 `2.3.7`，当前距离 104 cm、强度 2328、
  芯片温度 55.75 C。
- 同次 SystemTask/ServiceTask 为 2041/20415，deadline/fault 为 0；左右 PWM、
  applied/normalized output、armed/output permitted 全为 0。最终固件继续运行，未发送电机命令。
- 10 个一秒静态样本为 103–104 cm、强度 2328–2337、芯片温度 56.00 C；每秒主动 halt
  采样最终累计 1 个校验错误，只记录为调试暂停干扰，不覆盖无断点 20 秒的零错误结论。
- UART0 `COM9` 仍被浏览器侧串口占用，因此真实 type 10 串口端到端采集 deferred；
  释放端口后应补一次 30 秒采集。当前未发送 I2C 切换或保存设置命令。

## 0.000 当前任务：15.9 V 工况 PID 网页实测

- 当前板上为 Profile ID 5 / `MG513X-4S v11`，母线以用户表测 `15.9 V` 为准；
  PB17 分压链路仍未验收，未启用电压自动补偿。
- 默认 PID 已固化为 `Kp=6`、`Ki=8`、`Kd=0`。前馈由实测稳态总 PWM 回归为
  左 `388.5 + 1.93*rpm`、右 `375 + 2.70*rpm`；升速目标斜坡 150 rpm/s，
  降速目标斜坡独立为 90 rpm/s。
- 用户 CSV 的主要问题是两次 `10 rpm` 起步峰值 20.04/21.86 rpm，以及右轮运行中
  `0 -> 60 rpm` 峰值 69 rpm。独立轮启动恢复和助推退出跟踪修复后，v9 完整复测中
  `10 rpm` 两次峰值均为 12.43 rpm，右轮 `0 -> 60` 峰值 61.71 rpm、t90 260 ms。
- 最终 `Kp=6/Kd=0` A/B：`30 -> 60` t90 320/190 ms、峰值 61.82/62.16 rpm；
  `60 -> 30` t90 550/450 ms、最低 27.10/27.00 rpm、尾段 29.99/30.30 rpm；
  `30 -> 70` t90 300/250 ms、峰值 71.46/72.00 rpm、尾段 70.04/70.10 rpm。
- 70 rpm 稳态积分由旧 CSV 约 `+38/+47 permille` 降至 `+1.1/-4.1 permille`。
  `Kd=0.1` 未改善右轮下冲且把左轮 `30 -> 60` t90 从 320 ms 拖到 380 ms，已拒绝。
- 速度模式停机后的 telemetry 继续保持 rpm/PWM 语义；原先约 `278/-70 rpm` 的假尖峰已消失。
- Keil App 0 Error / 0 Warning；12 V v5 与 4S v11 Profile 分支均通过 ArmClang 编译检查。
  最终 HEX SHA-256 为 `E9E9EE2756116538987D83AABDC3DDAB306ECE5596BA39A0C73B5E98E622F839`；
  有线 CMSIS-DAP `2b5d6f2a`、500 kHz program/verify/reset 通过。
- 最终冷启动 telemetry 为 apply sequence 0、`6/8/0`、目标/速度/PWM `0/0`，
  Health active/sticky/deadline 为零，`ActuatorOutputPermitted=0`。
- 主机链路仍受红外地址切换耦合影响：最终 Kp A/B 有 38 个主机 CRC 错误；
  设备端控制周期、Health 和停机输出正常。该串口硬件问题未由 PID 修改掩盖。
- 后续仍需落地直线、带载扰动、电流、温升和电池压降验收；当前结论只覆盖架空轮组。
- 详细证据：`docs/worklogs/2026-07-19_513x_4s_pid_web_retune_15v9.md`。

## 0.00 最新任务：513X-4S PID 实机调参

- 当前板上已烧录 `MG513X-4S v4`，Profile ID 5；默认 PID 已持久化为 `Kp=4`、`Ki=10`、`Kd=0`。
- 电机 Profile、断言和调参记录已提交为 `294893d tune: calibrate 513x 4s speed pid`，尚未 push。
- `30->70 rpm`：t90 570/580 ms，峰值 71.679/71.150 rpm，尾段 70.004/70.059 rpm。
- `70->30 rpm`：t90 620/620 ms，下冲 8.84%/4.29%，尾段 30.09/30.32 rpm。
- `Ki=12/14` 和 `Kd=0.1` 已实测拒绝；`Ki>=12` 会放大降速下冲，D 项没有一致收益。
- Keil App 0 Error / 0 Warning；HEX `6642AE8A449EDFA9C6FA09C44501B7B458E3814838A417242089BD2E4715ADF8` 已用 `2b5d6f2a`、500 kHz 烧录/校验/复位。
- 冷启动遥测确认 Profile v4、apply sequence 0、默认 `4/10/0`，左右目标/转速/PWM 为 0，`ActuatorOutputPermitted=0`，Health active/sticky/deadline 为 0。
- 主机 UART 仍受 125 Hz 红外地址切换耦合影响而有 CRC/gap；设备端控制时序和 drop/overflow 正常。落地负载、直线同步、电流和温升仍未验收。
- 原始数据在 `tmp/pid-live-*`；摘要见 `docs/worklogs/2026-07-19_513x_4s_pid_tuning.md`。

## 0.0 最新任务：513X 更换 4S 电池

- 原 `MG513X v5` 12 V Profile 保留为 ID 2；当前板上为独立 `513X-4S v3`，Profile ID 5。
- 旧参数在 4S 下下发 30 rpm 后约为左 37.1、右 35.1 rpm，左右积分均卡在 -90 permille，确认前馈失配。
- 4S 最终参数：启动 600 permille、最大 650 permille、Kp/Ki/Kd=3/8/0；左前馈 `409 + 1.09*rpm`，右前馈 `401 + 1.62*rpm`。
- 最终独立 30 rpm 尾段 30.865/30.111 rpm，PWM 449.57/475.25，积分 +10.42/+25.91；停机后输出为零。
- 最终 30->70 rpm：t90 670/600 ms，峰值 73.286/71.143 rpm，70 rpm 尾段 70.129/70.059 rpm，积分 +42.88/+68.26；Health、deadline、DMA stall、encoder late、输出残留均为零。
- 最终 HEX SHA-256 `D110D1B5DAC13BEDFFC3744CF93412CA949CEF1FC9267713D58061968F7BF0F2`，Horco CMSIS-DAP v2 `2b5d6f2a` 以 500 kHz 烧录/校验/复位通过；UART 为 COM9 230400 8N1。
- 组合运行时主机链路仍受红外 AD0/1/2 以 125 Hz 切换的电气耦合影响：最终阶跃 40 CRC/40 gap，最终 30 rpm 为 63/63；设备端传输计数与 Health 仍为零。未修硬件前不得宣称串口链路生产可用。
- 电池实际电压、负载电流、压降和温升未测；4S 超过 70 rpm、落地负载及直线同步未验收。
- 详细记录：`docs/worklogs/2026-07-18_513x_4s_battery_validation.md`。

## 0. 最新任务：八路红外灰度接入

- 513X 已独立提交为 `c08fa3d feat: add 513x motor control profile`，尚未 push。
- PID 网页任务暂停；相关未提交文件保持原样，不删除、不混入灰度任务。
- 灰度板接线：`OUT->PA26/ADC0_CH1`、`AD0->PA27`、`AD1->PA24`、`AD2->PA25`，EN/ERR 悬空；用户确认 OUT 不超过 3.3 V。
- 新增 `bsp_reflectance` 非阻塞扫描和一次掩码地址写入；最终每通道稳定 4 ms，完整八路 31.25 Hz，UART type 8 为 36 B payload 且每两轮发送一次。
- 串口静态环形缓冲由 1024 B 扩到 2048 B；实测峰值仍为 1020 B，保留约一半余量。
- 全量构建 0 Error / 0 Warning；最终 HEX SHA-256 `9EB459E3B1EE6FB0E52DF82936F4EE975178ECB4414ADCC9F676404B13814E16`，无线 DAP 500 kHz program/fast verify/reset 通过。
- 扫描频率测试覆盖 12.5、15.625、20.833、31.25、62.5 和 125 Hz。所有档位设备端 ADC timeout/incomplete/deadline/Health/drop 为零，电机输出为零。
- 无线 COM7 的 CRC/gap 会随高频地址切换总体增加；125 Hz 即使原始遥测仅 1 Hz，30 秒仍有 21 CRC/29 gap，因此不保留。
- 最终 31.25 Hz / 遥测 15.625 Hz：30 秒 3050 Control、476 Reflectance；Control 99.934 Hz、scan 31.25 Hz；2 CRC/3 gap。最新八路 `[267,1065,203,46,83,102,913,1029]`。
- 31.25 Hz 是当前无线链路下的性能折中，不是严格零误码上限；下一步需用有线 UART 或 AD0/1/2 串联阻尼后复测 62.5/125 Hz。
- 详细证据见 `docs/worklogs/2026-07-17_reflectance_bringup.md` 和 `tmp/reflectance-30s`。

## 1. 权威位置

```text
唯一正式工程：E:\ECHO
开发 worktree：C:\Users\Auror\ECHO-513a-work
当前 branch：refs/heads/codex/513a-motor-bringup
起始 HEAD：4b1a3dbef3c96b1b627c90d3c10566e3c6a0ec2f
当前 HEAD：351c43e
已验收基线：refs/tags/phase-1f-operability-diagnostics
origin/main：4b1a3dbef3c96b1b627c90d3c10566e3c6a0ec2f / Phase 1F，已 push
origin/phase-2a-at8236-chassis-encoder：本文件所在的 Phase 2A 阶段提交，已 push
```

## 2. 当前目标

- Phase 2A 架空直流电机与编码器阶段已完成：冻结 MG370 Profile v13、8–120 rpm 普通 PI、0–8 rpm 蠕行、机械死点恢复、负载释放快速退积分和右编码器迟到容错。
- 比赛用软件上限暂定 120 rpm、90% PWM；65 mm 轮径下约 0.408 m/s，并保留约 16% PWM 抗负载余量。
- 本阶段不再继续调直流电机。电流、温升、落地负载、实际补偿、直线同步和带载上限转入后续阶段；当前不混入 IMU、云台、视觉或组合动作。

## 3. 当前进度

### 已完成

- 2026-07-16 闭环调试已完成：位置式速度 PI 使用 `Kp=8`、`Ki=18`、`Kd=0`，积分限幅 160 permille，目标斜坡 350 rpm/s，输出封顶 900 permille。
- 启动采用双轮共同 50% / 最短 40 ms 破静摩擦；退出时直接交给每轮前馈，不把助推写入积分器。
- 运行中 speed frame type 5 可在 SystemTask 周期边界无停顿重定向目标；电气点动和跨模式请求仍保持 busy 门禁。
- 10->120 rpm 阶跃左右 `t90=280/280 ms`、同步差 0 ms、超调 `7.57%/8.61%`；120->10 rpm `t90=540/530 ms`、同步差 10 ms。
- 120 rpm / 30 秒连续运行通过：3000/3000 speed frames，左/右 `120.012/120.011 rpm`，CRC/gap/deadline/Health 全零。
- 5 rpm 蠕行通过：1000/1000 speed frames，起步差 10 ms；1 秒窗口左 `5.21–5.81 rpm`、右 `4.85–5.52 rpm`，不再因低速失速停机。
- 8 rpm 普通 PI 边界通过：左/右 `8.010/7.991 rpm`；60 rpm 与 100 rpm 分别达到约 0.204 m/s 与 0.340 m/s。
- 右编码器迟到 ISR 已改为沿用最近可靠方向；Health v2 增加累计迟到计数，持续密集迟到才降级。120 rpm / 30 秒累计 128 次但无 sustained issue。
- 板上当前固件为 MG370 Profile v8，HEX SHA-256 `BB4421DBC943CA4A1EDF7CE6B48448BB5106D8F342FCC1B0BC85465DE0B1E5A5`。
- 最终 FreeRTOS/App full rebuild 均 0 Error / 0 Warning；Code=64,768，ZI=16,980；重建 HEX 与板上哈希一致，无需重复烧录。

- Phase 1F 已在 `4b1a3db` 完成、验收、打 annotated tag 并合入正式 main。
- Phase 2A 分支/worktree 已从正式 Phase 1F 基线创建。
- 接手清理的代码和文档修改已形成，验证摘要见当前 worklog；尚未提交。
- 用户确认左轮使用 D153B Motor A/E1A/E1B，右轮使用 Motor B/E2A/E2B。
- 已审查 TI QEI、MSPM0G3507 I/O 和 D153B 原理图，形成 Phase 2A 设计文档。
- 左轮 TIMG8 QEI 已完成 SysConfig、0/0 构建、烧录回读校验、正反手转和 120 秒静止板测。
- 左轮向前为正，`encoder_count_sign=+1`；provisional 输出轴 CPR=68,028。
- 右轮 E2A->PB6 上升沿软件 x1、E2B->PB7 方向输入已完成 SysConfig 和源码实现。
- 右轮版本已完成 SysConfig、0/0 构建、烧录回读校验和正反手转板测。
- 右轮向前原始为负，`encoder_count_sign=-1`；provisional 输出轴 CPR=17,007。
- 左右编码器同时接线静止 10 秒为零；一圈绝对计数比 3.812:1，Health 全干净。
- TIMA0 CC0-CC3 已配置为 PB8/PB9/PB12/PB13、10 kHz；启动后 BSP 将四路硬件强制低。
- UART CommandService 已替代会重启目标的 SWD RAM 点动；ServiceTask 静态解析，SystemTask 唯一写输出。
- ChassisActuator 只接受 CRC、双 magic、单电机、不超过 10%/500 ms 的一次性点动请求。
- RTOS assert/malloc/stack/fatal 路径已接紧急双低钩子；安全 PWM 版本已 0/0 构建和烧录，未上 VM。
- 安全 PWM 版本已烧录并完成左右正负 5%/200 ms 无动力逻辑点动，四次均 20 active 帧后自动归零。
- 参数协议在命令路由重构后完成 `kp 1.0 -> 1.1 -> 1.0` 回归。
- 一套 `motor_profile` 已覆盖 MG370 与 513A 占位，未复制 BSP、未增加引脚、禁止运行时切换。
- MG370 v1 已填入确认数据；左轮 x4/+1/68,028，右轮 x1/-1/17,007，左右电机输出符号保持待定 0。
- 100 Hz 输出轴 RPM 诊断和 1 Hz Profile telemetry frame type 7 已接入。
- 513A 选择保留编译锁；旧 513X 负向测试只证明锁定机制，重命名后需重新验证锁定文案。

### 未开始

- 轮组落地后的电流、温升、实际补偿、直线同步、带载抗扰与速度上限；均明确 deferred 到后续阶段。
- 轮组落地后的负载、直线同步、实际轮距/里程、温升和电源电流验收。
- PB8/PB9/PB12/PB13 示波器物理波形复核；现有逻辑与编码器证据不能替代探头测量。
- 近零速短距离位置环；当前仅实现速度蠕行，不把 1–5 rpm 当作长期位置保持。

### 进行中

- 最新状态优先于本节后续保留的开环历史：双轮架空调试和最终静止收尾已完成，板上 Profile v13 输出已自动归零，本阶段结束。
- 当前串口为 COM4；未保留 OpenOCD、Keil 或串口采集后台会话。系统中另有用户/另一 Codex 的 PowerShell 进程，不得终止。
- 以下条目保留 2026-07-16 早期开环调试过程，仅作历史证据，不代表当前板上能力。

- 2026-07-16 用户在场进入直流电机调试：先低占空比开环确认 PWM/编码器链路，再进入 PI/PID 与自动整定。
- 当前 `COM4` 已识别为 DAPLink USB 串口，未发现 OpenOCD、Keil 或其他串口监视进程；尚未烧录、尚未产生本次运动输出。
- 烧录前 3 秒只读采集确认旧固件为 100 Hz Control / 1 Hz Health、CRC/gap/deadline/drop=0、左右编码器增量全 0、`ActuatorOutputPermitted=0`；旧固件无 Profile 帧。
- 同次采集发现 OLED offline、`I2cErrorCount=1693`，并存在 `ActiveIssueMask=0x00009800`；需在新固件复位后重新判定，若编码器 QEI fault 再现则禁止点动。
- MG370 Profile 固件已在 VM 断电条件下烧录；OpenOCD 快速 CRC 超时后，61,480 字节 Flash 读回 SHA-256 `D360C84932F44D8E3C11590AC2B9471AA5A2D25CE1E41B63A8886F705034B6E0` 一致并 `reset run`。
- 烧录后 5 秒静态采集通过：514 Control / 5 Health / 5 Profile，100/1/1 Hz，CRC/gap/deadline/drop/I2C/active/sticky=0，OLED online；左右编码器增量全 0，output permitted=0。
- 左轮 `+5%/200 ms` 与 `+7%/200 ms` 均被接受并准确保持 20 个控制周期后自动归零；Health/链路干净，但左编码器计数均为 0，未克服静摩擦。
- 左轮 `+10%/200 ms` 也未产生编码器计数；目标 Health 干净并已归零，但主机采集出现 1 个 CRC 错误和 1 帧缺口，该次证据判为 attention，不作为链路完全通过。
- 按用户现场指令，单次调试脉冲硬上限有界调整为 20%/1000 ms；单电机、双 magic、CRC、唯一 sequence、SystemTask 唯一写入者和自动归零保持不变，闭环仍锁定。
- 20%/1000 ms 版本完成 PowerShell AST 和 FreeRTOS/App full rebuild 0/0；Code=58,152，ZI=16,644，HEX SHA-256 `C3940A459151C975CAB52B2312FF4D6121A65960CEE48C10954AA4CAE9C6F903`。
- 该版本已在 VM 断电条件下烧录、验证和 reset run；随后 5 秒静态采集为 512 Control / 5 Health / 5 Profile，100/1/1 Hz，CRC/gap/deadline/I2C/active/sticky=0，左右编码器增量全 0，output permitted=0。
- 左轮 `+20%/1000 ms` 已执行：ACK 匹配、100 active、356 trailing safe，100/1 Hz，CRC/gap/deadline/Health 全干净并自动归零；左右编码器计数均为 0，未起转。停止继续提高占空比，等待现场电流/声音观察并检查电机端电压或限流折返。
- 用户随后明确要求改为双轮 `+50%/1000 ms`。调试协议现有界允许 1 或 2 个电机、最大 50%、最长 1 秒；主机脚本新增 `Both` 模式和独立 `ConfirmBothMotorsConnected` 门禁，原单轮模式仍要求另一电机断开。
- 双轮 50% 版本 AST 与安全负例通过，FreeRTOS/App full rebuild 0/0；Code=58,168，ZI=16,644，HEX SHA-256 `9025C4D027813A300809474B03C674F9FD1E361DAF4B6771B87BCAA51C3C80BF`。
- 用户确认 VM 断电后该版本已烧录、verify/reset；3 秒静态采集 309 Control / 4 Health / 4 Profile，100/1/1 Hz，CRC/gap/deadline/I2C/active/sticky=0，output permitted=0。
- 用户重新上电后双轮 `+50%/1000 ms` 执行通过：ACK 匹配、100 active、355 trailing safe，CRC/gap/deadline/Health=0；左原始净计数 `+34,776`、右原始净计数 `+9,608`，两路均确认起转并自动归零。
- active 窗内左/右原始计数为 `31,230 / 8,656`，按 provisional CPR 对应约 `27.5 / 30.5 rpm` 平均输出轴速度，峰值约 `38.1 / 40.2 rpm`。结合既有前进原始符号左正右负，冻结候选 `motor_output_sign` 为左 `+1`、右 `-1`。

## 4. Git 与文件所有权

本文件更新时间点，Phase 2A 工作树以实时 `git status` 为准；已观察到以下进行中修改：

```text
AGENTS.md
docs/ARCHITECTURE_BOUNDARIES.md
docs/CURRENT_WORKFLOW.md
docs/PROJECT_STATUS.md
tools/phase1f_field_check.ps1
docs/worklogs/2026-07-15_phase2a_takeover_cleanup.md（未跟踪）
docs/handoff/*（本交接机制）
docs/hardware/ECHO_WIRING_GUIDE.md（并行新增，已同步当前构建状态）
docs/phases/PHASE2A_AT8236_CHASSIS_ENCODER.md（未跟踪）
config/ECHO.syscfg
bsp/include/bsp_encoder.h（未跟踪）
bsp/source/bsp_encoder.c（未跟踪）
app/main.c
app/tasks/service_task.c
app/tasks/system_task.c
module/service/command_service.h（未跟踪）
module/service/command_service.c（未跟踪）
module/service/chassis_actuator.h（未跟踪）
module/service/chassis_actuator.c（未跟踪）
module/service/motor_profile_config.h（未跟踪）
module/service/motor_profile.h（未跟踪）
module/service/motor_profile.c（未跟踪）
module/service/parameter_service.h
module/service/parameter_service.c
module/service/telemetry.c
module/service/telemetry.h
module/service/system_health.h
module/service/system_health.c
keil/ECHO.uvprojx
platform/generated/ti_msp_dl_config.c
platform/generated/ti_msp_dl_config.h
tools/chassis_motor_pulse.ps1（未跟踪）
tools/telemetry_capture.ps1
docs/worklogs/2026-07-15_phase2a_left_encoder_bringup.md（未跟踪）
docs/worklogs/2026-07-15_phase2a_right_encoder_bringup.md（未跟踪）
docs/worklogs/2026-07-15_phase2a_at8236_logic_pwm.md（未跟踪）
docs/worklogs/2026-07-15_phase2a_motor_profiles.md（未跟踪）
```

- 暂存区在最近一次核对时为空。
- 上述接手清理文件属于当前任务，接手者必须先读 diff，不得还原或整文件覆盖。
- 正式工程原有用户文件、stash 和备份不在本任务范围内；正式工程保持只读。

## 5. 实现与决定

- 普通速度区定义为 `>=8 rpm`，采用每轮独立前馈 + 位置式 PI、测量低通、anti-windup、目标/输出斜坡和同方向扭矩约束。
- 蠕行区定义为 `<8 rpm`：双轮共同启动后，每轮使用约 35% 滞环驱动/滑行；近零时使用 60% 的 40 ms kick / 40 ms rest，不累计 PI 积分、不触发普通失速停机。
- MG370 Profile v8 前馈为左 `315 + 3.8*rpm`、右 `320 + 3.6*rpm`；软件速度上限 120 rpm、最大 PWM 900 permille。
- 目标斜坡期间冻结积分；同方向降速时控制输出最低钳位到 0，不允许为刹车反向驱动。
- 右轮 GPIO x1 ISR 若发现 A 相已回落，累计 late 但沿用最近可靠方向，避免读取已变化 B 相造成符号错误。
- Health schema 为 v2，Health frame 132 B；SerialTx 原子写上限已扩到 160 B，主机解析器兼容旧 112 B 和新 116 B payload。

- `CURRENT_HANDOFF.md` 记录基线之后的实时工作；`PROJECT_STATUS.md` 记录已确认阶段基线。
- 新对话固定先读实时交接，再用 Git、进程和硬件现场检查验证。
- Phase 1F 同名 branch/tag 使用完整 refs，不删除或重命名历史对象。
- Phase 1F field check 的累计 I2C 和 UART/OLED quiet 门禁正在接手清理中加固。
- 左轮硬件 QEI 使用 TIMG8：E1A->PA29/PHA，E1B->PA30/PHB；硬件按合法 AB 状态变化 x4 计数。
- 右轮 E2A->PB6 上升沿软件 x1、E2B->PB7 方向输入；x4 因满速约 340k ISR/s 被否决。
- 右轮向前原始为负，唯一方向配置值冻结为 `encoder_count_sign=-1`；诊断遥测继续保留原始计数。
- OpenOCD 实时连接写 RAM 会导致目标重启，禁止把 SWD RAM 写入作为电机点动入口；正式入口固定为 UART frame type 5/6。
- UART 执行器请求只 staging；SystemTask 周期边界应用，timing resync/fatal/完成/拒绝均清 pending 并四路低。
- `ECHO_MOTOR_PROFILE_SELECTION` 是唯一型号选择宏，默认 MG370；OLED/UART 不提供运行时切换。
- Profile 位于 service 层；BSP 继续只处理固定引脚和原始电气量。
- 未确认 Profile 字段为 0 且有效位清除；现有安全点动可用，闭环归一化输出保持锁定。
- 513A 缺少额定电压、堵转电流、减速比、编码器接口/电平/PPR，选择时必须编译失败。
- 未接的 QEI 输入会浮空；右轮单模块板测期间左轮未接导致 issue 16，不属于右轮 ISR late。
- PA29/PA30/PB6/PB7 不是 5 V tolerant；D153B 编码器接口没有电平转换，直连前必须测量并降压。
- 左右轮不是控制主从关系；左轮只作为第一只标定轮，整车前进时两轮统一为正。

## 6. 验证证据

| 层级 | 结果 | 说明 |
| --- | --- | --- |
| Phase 1F 固件终验 | passed | 证据见 Phase 文档和 worklog |
| 接手清理脚本 AST/fixture/负例 | passed | 见 `docs/worklogs/2026-07-15_phase2a_takeover_cleanup.md` |
| 本交接机制引用/结构检查 | passed | 引用路径存在，必填结构完整 |
| `git diff --check` | attention | SysConfig generated 2 行空白尾随；禁止手改 generated |
| Phase 2A SysConfig | passed | 0 error；1 条 STOP/STANDBY retention 提示 |
| Phase 2A FreeRTOS full rebuild | passed | 0 Error / 0 Warning |
| Phase 2A App full rebuild | passed | 0 Error / 0 Warning；Code=53992，ZI=16164 |
| Phase 2A 左编码器烧录 | passed | program；56,992 B 回读 SHA-256 一致；reset run |
| Phase 2A 左编码器方向 | passed | 前进约 +76.7k，后退约 -74.3k，sign=+1 |
| Phase 2A 左编码器静止 120 秒 | passed | 12,196 样本全部 delta=0，Health/链路全干净 |
| 右轮 SysConfig | passed | 0 error；PB6 GPIOB 上升沿 IRQ，PB7 GPIOB 输入 |
| 右轮 FreeRTOS full rebuild | passed | 0 Error / 0 Warning |
| 右轮 App full rebuild | passed | 0 Error / 0 Warning；Code=53640，ZI=16236 |
| 右轮固件烧录 | passed | program；56,664 B 回读 SHA-256 一致；reset run |
| 右轮静止 5 秒 | passed | 512 Control / 6 Health；右轮 delta 全 0；链路/Health 干净 |
| 右轮方向 | passed | 前进 `-18,632`，后退 `+21,691`，sign=-1 |
| 右轮连续手转负载 | passed | 10 ms 最大 230 count；SystemTask 最大 24 us；deadline=0 |
| 右轮 ISR late | passed | Health issue 17 未出现 |
| 左轮未接浮空 | attention | PA29/PA30 未接时机械扰动触发 issue 16；联合测试前重新接线 |
| 双编码器静止 10 秒 | passed | 左右 1019 个 delta 全 0；Health/链路干净 |
| 左 x4 / 右 x1 一圈比例 | passed | 76,573 / 20,072，绝对比 3.812:1；QEI/ISR late=0 |
| 双编码器连续静止 60 秒 | passed | 6099 个样本左右全 0；Health/链路干净 |
| AT8236 SysConfig | passed | TIMA0 CC0-CC3，PB8/PB9/PB12/PB13，4 MHz/400=10 kHz，初值 0%，默认 stop |
| AT8236 安全层构建 | passed | FreeRTOS/App 0/0；Code=56776，ZI=16572；HEX SHA-256 `251B7FFD474CA35F8B889A8D38146EE18C7C1E3685D080D3E43755D4067747EA` |
| AT8236 安全层烧录 | passed | program；59,920 B readback SHA-256 `9B28050074A22678F2316ECE4F1BECFEBDF84231F4DE9D1B731ACC273CF86D5E` |
| AT8236 四方向逻辑 PWM | passed | 左右正负 5%/200 ms 均 20 active 帧后归零；100/1 Hz；链路/Health 干净；编码器 0 |
| 参数协议回归 | passed | `kp 1.0 -> 1.1 -> 1.0`，首次 ACK applied，CRC/格式错误 0 |
| 最终静止收尾 | passed | 509 Control / 5 Health；100/1 Hz；CRC/gap/deadline/drop/I2C/active/sticky=0；output permitted=0 |
| MG370 Motor Profile 构建 | passed | FreeRTOS/App 0/0；Code=58152，ZI=16644；HEX SHA-256 `45D3035850AC9460232A75051FA1958F7F907187400E1C048274D1920F73CBC0` |
| 513A 选择门禁 | pending recheck | 旧 513X 锁定机制已通过；重命名后的错误文案待重新构建确认 |
| Profile telemetry fixture | passed | type 7 / 52 B；MG370 v1、68,028/17,007、+1/-1、x4/x1；CRC/unknown=0 |
| 2026-07-16 烧录前静态采集 | attention | 309 Control / 3 Health，100/1 Hz，左右编码器增量全 0，output permitted=0；旧固件 OLED offline、I2C error=1693、active/sticky=`0x00009800`，新固件复位后必须复核 |
| Motor Profile 烧录 | passed | VM 断电；61,480 B 读回 SHA-256 `D360C84932F44D8E3C11590AC2B9471AA5A2D25CE1E41B63A8886F705034B6E0` 一致；reset run |
| Motor Profile 静态板测 | passed | 514 Control / 5 Health / 5 Profile，100/1/1 Hz；MG370 v1；左右编码器增量全 0；Health 全干净；output permitted=0 |
| 左轮 +5%/200 ms | passed, no start | ACK/20 active/334 safe；Health 干净；左右编码器计数 0 |
| 左轮 +7%/200 ms | passed, no start | ACK/20 active/336 safe；Health 干净；左右编码器计数 0 |
| 左轮 +10%/200 ms | attention, no start | ACK 后自动归零、Health 干净、编码器计数 0；主机流 1 CRC error / 1 sequence gap，脚本按门禁失败 |
| 20%/1000 ms 调试上限构建 | passed | AST passed；FreeRTOS/App full rebuild 0/0；Code=58152，ZI=16644；HEX `C3940A...6F903` |
| 20%/1000 ms 固件烧录/静态板测 | passed | VM 断电烧录/verify/reset；512 Control / 5 Health / 5 Profile，100/1/1 Hz；左右计数 0；Health 全干净；output permitted=0 |
| 左轮 +20%/1000 ms | passed, no start | ACK/100 active/356 safe；CRC/gap/deadline/Health=0；左右编码器计数 0；已自动归零，不继续升占空比 |
| 双轮 +50%/1000 ms 构建 | passed | Both 安全门负例通过；FreeRTOS/App full rebuild 0/0；Code=58168，ZI=16644；HEX `9025C4D0...C80BF` |
| 双轮 50% 固件烧录/静态板测 | passed | VM 断电烧录/verify/reset；309 Control / 4 Health / 4 Profile，100/1/1 Hz；Health 全干净；output permitted=0 |
| 双轮 +50%/1000 ms | passed | ACK/100 active/355 safe；左/右净计数 +34,776/+9,608；CRC/gap/deadline/Health=0；自动归零；候选 motor sign 左 +1、右 -1 |
| 5 rpm 蠕行 10 s | passed | 1000/1000；起步差 10 ms；tail `5.303/5.521 rpm`；1 s 窗口左 `5.21–5.81`、右 `4.85–5.52 rpm`；Health clean |
| 8 rpm PI 10 s | passed | `8.010/7.991 rpm`；起步差 10 ms；CRC/gap/deadline/Health=0 |
| 60 rpm PI 10 s | passed | `59.959/60.025 rpm`；0.204 m/s；encoder late=0；Health clean |
| 100 rpm PI 10 s | passed | `100.019/100.018 rpm`；0.340 m/s；encoder late=9；Health clean |
| 120 rpm PI 30 s | passed | 3000/3000；`120.012/120.011 rpm`；0.408 m/s；encoder late=128；Health clean |
| 10->120 rpm step | passed | `t90=280/280 ms`；skew=0 ms；overshoot `7.57%/8.61%`；tail `120.020/120.016 rpm` |
| 120->10 rpm step | passed | `t90=540/530 ms`；skew=10 ms；无反向制动；tail `9.981/10.023 rpm` |
| 147 rpm 探索 | attention | 实际约 `160.6/157.7 rpm` 且 encoder late=217/sticky；已放弃极限目标并把比赛上限降到 120 rpm |
| 60 rpm 人工扰动 | not run | 用户确认未施加外力；该次主机流 1 CRC / 1 gap 不属于机械扰动证据 |
| PB8/PB9/PB12/PB13 物理电平/波形 | not run | 仍需万用表/示波器实测，逻辑遥测不能替代引脚测量 |
| 电机点动 | not run | 必须重新确认架空、限流和物理断电后执行 |

## 7. 硬件现场状态

- 最新状态：用户在场、轮组架空、左右电机与编码器均连接，编码器 3.3 V 且共地，VM 12 V 已上电，物理断电开关可用。
- 板上运行 Profile v8 / HEX `BB4421...E5A5`；最近 8 rpm 测试完成后自动归零，`ActuatorOutputPermitted=0`。
- 轮径已冻结为 65 mm；0.408 m/s 对应 120 rpm。轮距仍未冻结。
- 本节后续早期“VM 未上电/动力线断开”条目仅是历史条件，不代表当前现场状态。

- 左右编码器和 AT8236 安全 PWM 固件已烧录并完成无动力逻辑板测；电机、云台和 4S 未驱动。
- 用户确认物理映射：左轮 Motor A，+->AOUT1、-->AOUT2、A/B->E1A/E1B；右轮 Motor B，
  +->BOUT1、-->BOUT2、A/B->E2A/E2B。
- 当前左右编码器均已接线：E1A/E1B->PA29/PA30，E2A/E2B->PB6/PB7，3.3 V、共地。
- 最近一次确认的 D153B VM/4S 状态为未上电，AO1/AO2、BO1/BO2 断开并绝缘。
- 已烧录 PWM 安全层，但全部板测均在 VM 断电和动力线断开条件下完成，未驱动电机。
- 板上现运行双轮最高 50%/1000 ms 有界调试固件 HEX `9025C4D0...C80BF`；双轮 50% 脉冲已通过并自动归零。用户已被要求关闭 VM，实际状态等待确认。
- 2026-07-16 用户确认：人在现场、轮组架空、首次只接左电机、右电机动力线断开并绝缘、编码器 3.3 V 且可靠共地、12 V 电源限流约 0.5 A、物理断电可立即操作。
- 已要求烧录和静态检查期间保持 12 V VM 断电；实际接通 VM 前由用户再次执行现场动作。
- 异常发热的天猛星板继续禁用。

## 8. 后台进程与运行状态

- 2026-07-16 17:14 +08:00：所有 OpenOCD、Keil 和 COM4 测试会话均已退出；板上自动归零。系统仍有不属于本任务的长期 PowerShell 进程，未终止、未干预。
- 2026-07-16 17:22 +08:00：最终静止采集通过；511 Control / 5 Health / 5 Profile，100/1/1 Hz，左右编码器 delta 全 0，CRC/gap/deadline/active/sticky/I2C=0，`ActuatorOutputPermitted=0`。串口会话已退出。

- 2026-07-15 14:59 +08:00 核对：未发现 OpenOCD、GDB 或 pyOCD 进程。
- 2026-07-15 18:37 +08:00：烧录会话已退出；COM4 完成四次点动和最终静止采集；未发现 OpenOCD/UV4 进程。
- 2026-07-15 19:03 +08:00：Motor Profile 最终构建完成；未烧录，未发现 OpenOCD/UV4 进程。
- 2026-07-16 11:57 +08:00：`COM4` 为 VID 0D28/PID 0204 的 DAPLink USB 串口且状态正常；未发现 OpenOCD、pyOCD、J-Link、Keil 或串口监视进程。
- 2026-07-16 12:00 +08:00：完成不发送命令的烧录前静态采集；串口已关闭，无后台采集进程。
- 2026-07-16 12:02 +08:00：MG370 Profile 固件烧录、读回校验、reset run 和 5 秒静态采集完成；OpenOCD/串口会话均已退出。
- 2026-07-16 12:07 +08:00：完成左轮 5%/7%/10% 三档 200 ms 点动；均无编码器响应，10% 采集出现单次 CRC/gap；所有串口会话已退出。
- 2026-07-16 12:11 +08:00：用户确认 VM 已断电；20%/1000 ms 有界调试版本完成全量构建，未烧录，无后台构建/OpenOCD/串口进程。
- 2026-07-16 12:14 +08:00：20%/1000 ms 版本完成烧录、verify/reset 和 5 秒静态板测；OpenOCD/串口会话已退出，VM 保持断电。
- 2026-07-16 12:17 +08:00：左轮 +20%/1000 ms 点动完成、未起转、自动归零；串口会话已退出，无后台输出进程。
- 2026-07-16 12:21 +08:00：双轮 50%/1000 ms 版本完成 AST、安全负例和全量构建；未烧录，无 OpenOCD/串口会话。
- 2026-07-16 12:25 +08:00：双轮 50% 固件完成烧录、verify/reset 和静态板测；OpenOCD/串口会话已退出，VM 等待用户接通。
- 2026-07-16 12:29 +08:00：双轮 +50%/1000 ms 点动通过并自动归零；两编码器均响应，串口/Health 干净；串口会话已退出。

## 9. 问题、阻塞与风险

- 当前唯一现场待测项是人工阻力扰动；上一组用户未施加外力，需要按明确时机重新施加约 1 秒阻力。
- 右编码器线材当前不能更换；120 rpm 下 late 比例很低且方向回退有效，但轮组落地、强干扰或更差线束仍需复测。
- 当前 120 rpm 上限来自架空、12 V、约 0.5 A 限流条件，不等于落地带载最高速度。
- 5 rpm 是速度蠕行平均值，不保证瞬时速度恒定；接近零速的精确位移和停车仍应由后续位置环负责。

- GMR 编码器 500 PPR 位于电机轴还是输出轴、是否已含 x4 仍未确认。
- GMR E1A/E1B 的高电平和输出类型未确认，禁止直接连接非 5 V tolerant 的 PA29/PA30。
- 轮径、轮距、左右安装朝向和 513A 完整参数未冻结。
- MG370 左右 `motor_output_sign`、起转/最大 PWM、速度/加速度限制、堵转保护和 PID 未冻结；closed loop 锁定。
- 513A 关键电气与编码器参数未确认，当前只能保留占位并编译锁定；513B 是未来另一套硬件。
- 未接 PA29/PA30 会使左 QEI 输入浮空；联合测试必须重新接左编码器或设计明确的未接通道管理。
- CPU debug halt 期间不能依赖软件超时归零；首次带 VM 点动禁止断点/Watch，必须使用限流电源和物理断电。
- 实测 OpenOCD 无显式 halt 的 RAM 写入也会使本目标重启；电机命令只允许走 UART 安全帧。
- 物理 ADC 五键、Flash 持久化和硬件看门狗仍为 deferred。
- 涉及电机、轮组或 4S 前必须由用户在现场明确许可，架空轮组并准备物理断电。

## 10. 下一步精确动作

1. 用户回复“准备好”后，复位 MCU 并启动 60 rpm / 15 秒采集；用户在收到明确提示后轻压左轮约 1 秒并松开。
2. 计算速度最低点、输出增量、松手后 90% 恢复时间和左右扰动耦合；若无可测阻力则记录 deferred，不伪造通过。
3. 最终 full rebuild 已完成且哈希未变；只需在任务结束前做一次不发送命令的静止 Health/归零采集。
4. 人工扰动若本次无法配合，明确记录 deferred，不得把第一次无可辨识曲线写成通过。
5. 不自动提交、暂存或 push；正式 `E:\ECHO` 保持只读。

## 11. 禁止操作

- 不自动 push，不删除 stash、备份、branch、tag 或 worktree。
- 不使用破坏性 Git 命令，不覆盖未知 dirty 文件。
- 不手改 `platform/generated`。
- 未重新获得现场许可前，不连接电机动力线、VM/4S，不驱动轮组、云台或其他高功率输出。
- 不把 E1A/E1B 的 5 V 未确认信号直接接入 PA29/PA30。
- 不把 Phase 1F 完成描述为最终工程完成。

## 12. 相关资料

- `AGENTS.md`
- `docs/PROJECT_STATUS.md`
- `docs/CURRENT_WORKFLOW.md`
- `docs/ENGINEERING_RED_LINES.md`
- `docs/hardware/ECHO_WIRING_GUIDE.md`
- `docs/phases/PHASE1F_OPERABILITY_DIAGNOSTICS.md`
- `docs/phases/PHASE2A_AT8236_CHASSIS_ENCODER.md`
- `docs/worklogs/2026-07-15_phase1f_operability_diagnostics.md`
- `docs/worklogs/2026-07-15_phase2a_takeover_cleanup.md`
- `docs/worklogs/2026-07-15_phase2a_right_encoder_bringup.md`
- `docs/worklogs/2026-07-15_phase2a_motor_profiles.md`
- `docs/worklogs/2026-07-15_realtime_handoff_infrastructure.md`
- `docs/worklogs/2026-07-16_phase2a_speed_control_tuning.md`

## 13. 接手自检

- [x] 权威仓库、worktree、branch、HEAD 和基线已记录。
- [x] 已知 dirty 文件和所有权已记录。
- [x] 当前硬件与后台进程的不确定项未伪报。
- [x] 下一步和危险动作门槛已明确。
- [x] 新文件引用、必填结构和 `git diff --check` 已验证。

## 14. 当前功能接线指南

- 已新增长期入口 `docs/hardware/ECHO_WIRING_GUIDE.md`，当前版本 `v0.1`、状态
  `living_draft`；后续 Phase 接线继续更新同一文件。
- 指南已汇总已验收的 SWD、UART1、SSD1306 OLED 和 PB22 板载 LED，以及物理五键的
  deferred 状态。
- 已按用户确认记录：左轮 `AO1/AO2/E1A/E1B`，右轮 `BO1/BO2/E2A/E2B`；原文 `A02`
  统一规范为 `AO2`，接线仍须按模块丝印复核。
- 左右编码器接线和无动力板测状态已更新；电机 PWM 引脚已完成逻辑点动，但物理波形和带 VM 驱动仍未验收。
- 2026-07-16 已补充带 VM 双轮闭环证据：5 rpm 蠕行、8–120 rpm PI、大幅双向阶跃和 120 rpm / 30 秒连续运行均完成；人工阻力扰动待重测。
- 接线指南最初由并行文档任务新增；AT8236 安全固件此前已烧录并完成无动力板测。本次新增
  Motor Profile 仅构建，尚未烧录、上 VM 或驱动电机。

## 15. 2026-07-16 18:26 速度控制最新状态

本节优先于前文仍保留的 Profile v8 和“人工扰动 not run”历史描述。

- 板上当前为 MG370 Profile v13，HEX SHA-256
  `43F7F69B207BA39BBF5C847B935DD05E4EADD682780F1B982CF6C6A3291B5FAF`。
- v13 full rebuild：FreeRTOS/App 均 0 Error / 0 Warning，Code=65,200，ZI=17,020；DAPLink program/verify/reset passed。
- PI 仍为 `Kp=8`、`Ki=18`、`Kd=0`。普通启动为 50%；机械死点恢复独立为 60%、最长 80 ms。
- 负载释放资格要求目标稳定、曾建立同方向负载积分且随后超速至少 1.5 rpm；目标变化会清除资格。负载积分退回倍率为 10 倍，卸载专用 PWM 降沿上限为 6000 permille/s；普通降速仍使用 1500 permille/s。
- v13 `120->10 rpm`：`t90=550/540 ms`、同步差 10 ms、尾段 `9.956/9.979 rpm`，Health clean。
- v13 `10->120 rpm`：`t90=270/280 ms`、同步差 10 ms、超调 `7.97%/7.64%`、尾段 `120.026/120.041 rpm`，Health clean。
- v12 单左轮有效人工扰动：基线约 60 rpm，左轮最低 `44.276 rpm`，左 PWM 最高 `672 permille`，右轮保持约 60 rpm；松手峰值 `70.559 rpm`。10 ms 轨迹证明 3 倍退积分仍过慢，因此形成 v13。
- 另一组 v12 采集先压错右轮，随后又压左轮，不能作为单轮对照。
- v13 最终单左轮人工扰动通过：60 rpm 基线下左轮最低 `45.422 rpm`（下降 24.30%），PWM 由约 `507.9` 升至 `670 permille`；右轮均值 `59.999 rpm`。
- 松手后 100 ms 达峰值 `67.384 rpm`，超调 `12.31%`；820 ms 内进入并持续保持目标 ±3%，PWM 200 ms 内回到基线 ±5 permille。相比负载相近的 v12，超调由 `17.60%` 降至 `12.31%`。
- 最终静止收尾通过：512 Control / 5 Health / 5 Profile，100/1/1 Hz；CRC/gap/deadline/drop/I2C/active/sticky/encoder late 全零，`ActuatorOutputPermitted=0`。
- 精确下一步：轮组落地后复测电流、温升、直线同步、带载抗扰和 120 rpm 上限；架空 PI 调试不再继续为手压工况加大控制强度。

## 16. 2026-07-17 MG513X 513A 当前状态

- 用户指定正式调试 `513A`，产品为 MG513X、额定 12 V；图片资料给出额定电流 0.36 A、
  堵转电流 3.2 A、减速比 1:28、空载约 370 rpm、额定约 300 rpm。
- 用户纠正编码器为 GMR。当前 Profile 按 500 PPR、3.3 V、AB 相处理；理论输出轴计数暂定
  左 x4 `56000`、右 x1 `14000`，标记为 provisional，等待实测多圈校正。
- 513A 只开放最高 30%、最长 1000 ms 的有界电气点动；速度 PID、起转 PWM、速度/加速度和
  堵转阈值均未冻结，`closed_loop_ready=0`。
- 调试串口已从 UART1 PA8/PA9 迁移到无线从机所在的 UART0 PA10/PA11、230400、DMA_CH3；
  电机 PWM/编码器引脚保持与 MG370 完全相同。
- FreeRTOS full rebuild 与 UART1 App full rebuild均为 0 Error / 0 Warning；UART0 迁移后 App build
  仍为 0 Error / 0 Warning，Code=65200、RO=3352、RW=28、ZI=17020，HEX SHA-256
  `1C052546CA14B337AE353F41C701DCF35FDC67D1916EB476810CE84D5ABA1C7F`。
- `flash_echo.ps1` 已增加 `-AdapterSerial/-AdapterSpeedKhz`，双 DAP 时明确选择无线探针
  `2dc1718e`。
- 用户补接 nRESET 后，普通 SWD attach 恢复；Cortex-M0+、4 breakpoint、2 watchpoint 和 PC 读取通过。
- 513A 固件已通过无线 DAP 500 kHz 烧录。快速 CRC verify 失败后执行 68584 B 完整回读，SHA-256
  `B9DF4CB5B0467CC80B79B3240CA52820D9A38E29F7CEA582E79D2FF1173BFFEA6` 一致并 reset run。
- COM7 5 秒静态板测：512 Control、5 Health、5 Profile，100/1/1 Hz；Profile ID 2、GMR 500 PPR、
  CPR 56000/14000、ActuatorTestReady=1、OutputLocked=0；左右 512 个静止 delta 全 0，Health 全 0，
  OLED online，ActuatorOutputPermitted=0。
- 双轮前进极性依次执行 10%/200 ms、20%/500 ms、30%/1000 ms，随后执行反向 30%/1000 ms；
  四次均 ACK、active frame 数准确、自动归零，CRC/gap/deadline/I2C/active/sticky 全 0。
- 四次点动两路编码器绝对计数总和都为 0。最终 5 秒静止为 513 Control/5 Health/5 Profile，
  左右计数仍为 0、Health 全 0、ActuatorOutputPermitted=0。
- 当前阻塞是现场现象分类：需要用户确认电机是完全无动作、仅响/抖动，还是实际转动但编码器无计数。
  在此确认前不提高超过 30% 的占空比，也不解锁速度闭环。

## 17. 2026-07-17 MG513X 513A power bring-up superseding section 16

- User confirmed the motor moves from a direct 3.3 V supply. The no-motion result
  at 50% was therefore not a motor or gearbox failure.
- 513A profile is now version 3 with an electrical jog limit of 650 permille
  (65%); duration limit remains 1000 ms and closed-loop control remains locked.
- Full rebuild passed with 0 errors and 0 warnings. HEX SHA-256:
  `AB798F018142D05810C7FA2AB726C68A7F8B01F60099A204A4ACE3B4D1271853`.
- Wireless DAP programming at 500 kHz passed; COM7 static capture reports
  Profile v3, GMR 500 PPR, CPR 56000/14000, and clean Health/CRC/timing.
- Left +650 and -650 permille single-motor pulses both produced encoder response.
- Dual motor forward polarity (left +650, right -650) passed at 500 ms and
  1000 ms. The 1000 ms run produced 100 active frames, clean Health/CRC/gap/
  deadline telemetry, and automatic output zeroing. Active-window averages were
  approximately 65.3 rpm left and 65.7 rpm right; final 200 ms approximately
  72.3 rpm left and 71.0 rpm right.
- Evidence and detailed calculations are recorded in
  `docs/worklogs/2026-07-17_phase2a_513x_motor_bringup.md`.

## 18. 2026-07-17 model correction: current hardware is 513X

- The user corrected the hardware identity: every test previously labeled
  513A in sections 16-17 belongs to the MG513X / `513X` profile.
- Profile IDs are now MG370=1, 513X=2, 513A=3, and 513B=4. The active board is
  513X v4. 513A and 513B are independent compile-locked placeholders and must
  not inherit 513X electrical, encoder, startup, or PID values.
- The final 513X HEX SHA-256 is
  `EA016F7F9D6A9B093C978E37359AD7DED31EC9FCB172D4B4D9D7871BEADBA1F9`.
  Wireless DAP programming used 500 kHz; byte-for-byte readback passed after
  target CRC verification was unavailable.
- COM7 static telemetry reports ProfileId=2, Model=513X, ProfileVersion=4,
  GMR 500 PPR, provisional CPR 56000/14000, and clean Health.
- Forward electrical polarity is left positive and right negative. Encoder
  raw forward signs are left positive and right negative; profile normalization
  makes both forward speeds positive.
- Dual start sweep: 55% and 56% only twitched, 57% was asymmetric, 58% started
  with little margin, and 60% reliably started both motors. The profile uses
  600 permille startup and a 650 permille output ceiling.
- Closed-loop defaults are Kp=3, Ki=8, Kd=0 with a 70 rpm software limit.
  ParameterService now restores PID defaults from the active Motor Profile.
- Closed-loop validation passed at 5, 10, 30, +60, -60, and 70 rpm. The
  30->70 and 70->30 step tests passed. The final 70 rpm / 30 s run produced
  3000/3000 frames at 70.014/70.012 rpm with zero CRC, gaps, deadline misses,
  encoder-late events, active issues, or sticky issues, then auto-zeroed.
- Correct worklog:
  `docs/worklogs/2026-07-17_phase2a_513x_motor_bringup.md`.
- With wheels installed and suspended, 30->70 rpm passed at t90 280/250 ms
  with 2.59%/2.89% overshoot; 70->30 rpm passed at t90 710/520 ms and settled
  at 29.999/30.036 rpm.
- A valid 60 rpm left-wheel load disturbance reduced the left wheel to
  18.853 rpm and saturated output at 650 permille. The right wheel remained
  near 59.947 rpm. Release recovery was 230 ms to target +/-3%, 200 ms for PWM
  to baseline +/-5, with 5.01% overshoot and clean Health/transport timing.
- Early right-wheel disturbance captures lacked right-PWM telemetry and were
  not accepted. Control telemetry now appends `right_auxiliary` while retaining
  legacy 40-byte parser compatibility.
- Isolated right electrical drive (-650 permille, 500 ms) produced left=0 and
  right=8,869 absolute encoder counts, so channel mapping is correct. The user
  visibly observed right-wheel slowdown while the motor-side encoder remained
  near 60 rpm; inspect hub/coupler, tire/rim, output attachment, and gearbox
  slip before any straight-line claim.
- The repeated right-wheel disturbance with dual-PWM telemetry is valid and
  supersedes that preliminary mechanical-slip suspicion: baseline 59.996 rpm /
  631.6 permille, minimum 27.857 rpm, maximum 650 permille, 150 ms speed and
  PWM recovery, 5.71% overshoot. The untouched left wheel averaged 59.956 rpm.
- The user ended the 513X stage on 2026-07-17. Board output is zero. 513A and
  513B remain separate future stages with compile-locked profiles.

## 19. 2026-07-19 supply-voltage ADC validation

- The active board now contains the supply ADC build. App full rebuild passed
  with 0 errors and 0 warnings; HEX SHA-256 is
  `6BC7F10A6E4A4C1EC118E01DF2B7931DBE409B945A5F398E0238AAC5B5DB2E2C`.
- Supply ADC is `PB17 / ADC1_CH4`. The intended divider is 100 kohm high side,
  22 kohm low side, with 100 nF from PB17 to GND. A 16.8 V full 4S battery
  should place about 3.03 V on PB17. Direct battery-to-PB17 connection is
  prohibited.
- Firmware sampling is 100 Hz with a 1/8 IIR filter; UART frame type 9 is sent
  at 10 Hz. The 10-second COM9 capture received 102 voltage frames, reported
  100/10 Hz sample/telemetry rates, and had zero ADC conversion timeouts.
- The observed raw range was 657-803 and the latest filtered PB17 voltage was
  583 mV, yielding an invalid 3.235 V battery estimate. Treat the physical
  divider as not connected or incorrectly wired until proven with a meter.
- The user then connected a nominal 1.5 V cell directly to PB17 through the
  advised 10 kohm safety resistor. The valid retry was stable: 102 voltage
  frames, raw 1667-1687, average raw 1678.824, latest filtered ADC input 1351
  mV, and zero conversion timeouts. This confirms the ADC path. The reported
  7.49-7.53 V battery value is the expected 5.545x divider back-calculation and
  is not the cell voltage.
- Device-side Health was clean: no deadline miss, publish/transport/DMA drop,
  active issue, or sticky issue. `ActuatorOutputPermitted=0`; all sampled motor
  targets and outputs remained zero.
- COM9 still showed 29 host CRC/gap events in the 10-second mixed telemetry
  capture. This matches the existing UART signal-integrity limitation and does
  not indicate an ADC conversion failure.
- Next action: power down, fit/verify the 100k/22k/100nF divider and common
  ground, measure PB17 <= 3.3 V before reconnecting it to the MCU, then repeat
  the static capture together with a multimeter battery reading. Calibration
  factor is `multimeter_mv / adc_reported_mv`.
- No motor command was sent. No files were staged, committed, or pushed.
