# ECHO 当前状态

最后核对日期：2026-07-19（Asia/Shanghai）

## 0. 八路红外灰度接入

- 513X 已提交为 `c08fa3d`，尚未 push；PID 网页任务暂停且未删除。
- 灰度板固定接线为 `OUT->PA26/ADC0_CH1`、`AD0->PA27`、`AD1->PA24`、`AD2->PA25`；用户确认 OUT 不超过 3.3 V。
- 新增非阻塞八路原始值扫描和 UART type 8 telemetry；最终完整扫描 31.25 Hz、原始遥测 15.625 Hz，未加入黑白标定或循迹控制。
- 全量构建 0 Error / 0 Warning；最终 HEX `9EB459E3B1EE6FB0E52DF82936F4EE975178ECB4414ADCC9F676404B13814E16` 已烧录并快速校验通过。
- 频率探索覆盖 12.5、15.625、20.833、31.25、62.5 和 125 Hz。设备端 ADC timeout/incomplete/deadline/Health 均为零；无线 COM7 错误率随高频地址切换总体上升。
- 最终 31.25 Hz 的 30 秒测试：Control 99.934 Hz、Reflectance telemetry 15.592 Hz、2 CRC/3 gap；电机输出保持零。下一步用有线 UART 或地址线阻尼复核更高频率。
- 详细证据见 `docs/worklogs/2026-07-17_reflectance_bringup.md`。

## 0.1 513X PID 遥测网页更新

- 开发 worktree `C:\Users\Auror\ECHO-513a-work` 已完成 PID 网页与 96 B Control telemetry 扩展，尚未合入 `E:\ECHO`。
- 网页支持左右目标/实际轮速、左右总输出、左右 P/I/D/前馈曲线选择，20 s 同步窗口和暂停显示。
- 网页支持共享 Kp/Ki/Kd 一次应用并逐项显示 ACK/应用序号，参数只写 RAM。
- 网页支持左右独立 `-100..100 rpm` 持续速度；关闭网页不自动停机，只有 `0/0 rpm`、故障或复位停止。
- 513X Profile v5 最终固件已烧录并静态板测通过；非零持续运动验证 deferred。
- 验证证据见 `docs/worklogs/2026-07-17_pid_telemetry_web_console.md`。

## 0.2 513X-4S 15.9 V PID 网页复调

- 当前板上为 Profile ID 5 / `513X-4S v11`；默认 PID 已持久化为
  `Kp=6`、`Ki=8`、`Kd=0`，15.9 V 前馈为左 `388.5 + 1.93*rpm`、
  右 `375 + 2.70*rpm`。升速目标斜坡为 150 rpm/s，降速独立为 90 rpm/s。
- 用户 CSV 中两次 `10 rpm` 起步峰值为 20.04/21.86 rpm，右轮运行中
  `0 -> 60 rpm` 峰值为 69 rpm。独立启动恢复和测量起点跟踪修复后，v9 完整复测的
  `10 rpm` 峰值均为 12.43 rpm，右轮 `0 -> 60` 峰值 61.71 rpm、t90 260 ms。
- 最终 `Kp=6` A/B：`30 -> 60` t90 320/190 ms、峰值 61.82/62.16 rpm；
  `60 -> 30` t90 550/450 ms、最低 27.10/27.00 rpm；`30 -> 70` t90 300/250 ms、
  峰值 71.46/72.00 rpm，尾段 70.04/70.10 rpm。
- 前馈重拟合后 70 rpm 稳态积分由旧 CSV 约 `+38/+47 permille` 降到
  `+1.1/-4.1 permille`。`Kd=0.1` 未改善右轮降速下冲且拖慢左轮升速，已拒绝。
- 修复速度模式停机后 Control telemetry 字段退回原始编码器计数、网页仍按 rpm/PWM 绘制的
  语义错误；最终停机曲线无虚假尖峰，最后 20 帧目标、速度、输出全零。
- Keil App 0 Error / 0 Warning；最终 HEX
  `E9E9EE2756116538987D83AABDC3DDAB306ECE5596BA39A0C73B5E98E622F839` 已通过
  CMSIS-DAP `2b5d6f2a`、500 kHz 烧录、快速校验和复位。
- 冷启动 apply sequence 0 即报告 `6/8/0`；Health active/sticky/deadline 为零，
  `ActuatorOutputPermitted=0`。落地直线、带载、电流和温升仍未验收。
- 主机 UART 仍受红外地址切换电气耦合影响；最终 Kp A/B 采集有 38 个主机 CRC 错误，
  设备端 active/sticky/deadline 和输出收尾均正常。
- 详细证据见 `docs/worklogs/2026-07-19_513x_4s_pid_web_retune_15v9.md`。

## 1. 权威仓库

- 唯一正式工程：`E:\ECHO`
- 正式分支：`main`
- 最近已验收阶段：Phase 1F
- 最近已验收固件：本文件所在的 Phase 1F 提交
- 阶段标签：`phase-1f-operability-diagnostics`
- 基线父提交：`cb7c4c32783cf7eeeabbbdec4a193aee99077159` / `phase-1e-oled-ui`
- 远端 `origin/main`：`4b1a3db` / Phase 1F，已 push

Phase 1E 与 Phase 1F 已合入正式 main 并推送至 GitHub。Phase 2A 已从正式 Phase 1F 基线
创建独立分支/worktree，完成成果已推送至 `origin/phase-2a-at8236-chassis-encoder`：

```text
branch:   phase-2a-at8236-chassis-encoder
worktree: C:\Users\Auror\ECHO-phase2a-work
start:    4b1a3db / refs/tags/phase-1f-operability-diagnostics
```

Phase 2A 已开始。左右编码器无动力板测已通过；AT8236 默认零输出和 UART 一次性点动安全层
已完成 FreeRTOS/App 0/0 构建、烧录回读校验及四方向 5%/200 ms 无动力逻辑 PWM 板测。
共享底层的 MG370/513A 编译时 Motor Profile 已实现并完成 MG370 0/0 构建，但新 Profile 固件
尚未烧录。物理 PWM 波形、带 VM 单轮点动、方向冻结、故障测试和连续运行尚未完成。

## 2. 必须保留的用户状态

正式工作树原有 4 个用户文件，阶段合入时必须保持其语义，不得自动暂存、覆盖或还原：

```text
ECHO.uvmpw
freertos/keil/freertos_ECHO.uvprojx
keil/ECHO.uvprojx
tools/telemetry-web/README.md
```

Phase 1F 需要在 `keil/ECHO.uvprojx` 中登记 `system_health.c` 和 `bsp_reset.c`。正式合入必须
只增加这两个 `<File>` 节点，同时保留用户已有路径/配置变化，禁止整文件覆盖。

安全恢复点仍保留：

- `stash@{0}: pre-phase1e-user-protected-changes`
- `C:\tmp\ECHO-phase1e-merge-backup-20260715`

不得自动 pop/drop stash，不得删除备份。

## 3. 已完成阶段

| 阶段 | 提交 | 标签 | 结论 |
| --- | --- | --- | --- |
| 1A | `f3a4552` | `phase-1a-baseline` | 可移动工程、Keil/VSCode/DAPLink 链通过 |
| 1B | `212513a` | `phase-1b-rtos-skeleton` | 静态 RTOS 骨架、hook、栈/堆/故障诊断通过 |
| 1C | `cc4b52f` | `phase-1c-clock-timebase` | 80 MHz、1 MHz 时基和 100 Hz 时序诊断通过 |
| 1D | `e7a1ac7` | `phase-1d-telemetry-tuning` | UART DMA 遥测、网页曲线和 RAM 参数协议通过 |
| 1E | `cb7c4c3` | `phase-1e-oled-ui` | SSD1306 UI、I2C 超时恢复和 UART quiet window 通过 |
| 1F | 本文件所在提交 | `phase-1f-operability-diagnostics` | 统一健康、五页 UI、参数元数据、诊断工具与低功率终验通过 |

Phase 1D 仓库没有保存原始串口日志或具体验收数字，后续仍不得补写。Phase 1F 的数字来自
本次保存的 ignored JSON/CSV 和 RAM 快照，摘要见 Phase 文档与 worklog。

## 4. 当前固件行为

- Phase 2A worktree 的当前固件已覆盖下列 Phase 1F/早期 Phase 2A 描述：SystemTask 100 Hz 已运行真实双轮速度控制，不再发布纯测试信号。
- MG370 Profile v8：左/右方向 `+1/-1`，CPR `68028/17007`，速度上限 120 rpm，PWM 上限 900 permille。
- `>=8 rpm` 使用位置式 PI（`Kp=8`、`Ki=18`、`Kd=0`）与每轮前馈；`<8 rpm` 使用短启动脉冲、滞环驱动/滑行和近零 kick/rest 蠕行。
- UART speed 命令支持运行中在 SystemTask 周期边界重定向；命令结束、调度异常和 RTOS fatal 仍强制双路归零。
- Health schema v2 公开右编码器 ISR late 总数；late 边沿沿用最近可靠方向，持续密集迟到才产生 degraded issue。
- MCLK 80 MHz，FreeRTOS Tick 1 kHz，TIMG12 标称 1 MHz、32 位向上计数。
- SystemTask 100 Hz；当前仍发布测试信号，不是真实电机 PID。
- ServiceTask 2 ms 服务 UART/参数，并每 100 ms 成为健康快照的唯一写入者。
- TelemetryTask 非阻塞发送 100 Hz Control、1 Hz Health 和参数 ACK；UART1 PA8/PA9、230400 8N1。
- Health 快照包含 schema、身份、uptime/reset reason、overall/active/sticky/first fault、任务栈、
  deadline、UART/Telemetry、参数、OLED/I2C、future sensor/actuator 安全占位。
- OLED 为 Overview、RTOS、COMM、DEVICE、PARAM 五页；虚拟输入支持 press/long/repeat/timeout。
- `kp/ki/kd/target` 共用一个 metadata 表；UART/OLED 只 staging，SystemTask 周期边界应用。
- 启动最早期通过 BSP 只读捕获 reset cause；当前执行器 armed/output permitted 始终为 0。
- 当前已有左右编码器诊断采样和受限 UART 单次 PWM 命令；默认输出为零，尚无带动力运动证据。
- 执行器命令固定为 CRC、双 magic、单电机、最高 10%、最长 500 ms；SystemTask 唯一写输出。
- 电机型号由 `ECHO_MOTOR_PROFILE_SELECTION` 编译时选择；默认 MG370 v1，禁止 OLED 运行时切换。
- MG370 每轮 Profile 固定左 x4/+1/68,028、右 x1/-1/17,007；电机输出符号和闭环限制仍待实测。
- 513A 关键参数未确认，选择它会产生明确编译错误；不会生成可驱动固件。另一套 513 电机
  保留名称 `513B`，当前不共享或预填 513A 参数。

## 5. Phase 1F 最终构建与实测

| 项目 | 结果 |
| --- | ---: |
| FreeRTOS full rebuild | 0 Error / 0 Warning |
| App full rebuild | 0 Error / 0 Warning |
| 程序尺寸 | Code=53048, RO=2864, RW=28, ZI=15956 |
| HEX SHA-256 | `1A205780BF54C948915A7D29E1DC6C240912C4A4FE4A95499D4B093FF25D3157` |
| DAPLink/OpenOCD | program/verify/reset passed |
| 120 秒 Control / Health | 12194 / 121，100 Hz / 1 Hz |
| 120 秒 CRC / gap / deadline / drop | 0 / 0 / 0 / 0 |
| 120 秒周期 / max execution / jitter | 9998-10002 / 23 / 2 us |
| 120 秒 I2C / OLED / quiet | 43378 success、0 error、271 refresh、271/271 |
| 10 分钟 Control / Health / total | 60953 / 610 / 61563 |
| 10 分钟 CRC / gap / deadline / drop | 0 / 0 / 0 / 0 |
| 10 分钟 I2C / OLED / quiet | 234578 success、0 error、1466 refresh、1466/1466 |
| quiet max / active | 38529 us / 0 |
| TX ring high-water | 280 B |
| 六任务最小剩余栈 | 180 / 128 / 151 / 102 / 104 / 104 words |
| heap minimum | 3064 B |
| Health / active / sticky / actuator permitted | OK / 0 / 0 / 0 |

最终已 `reset run`，3 秒干净状态复核为 parameter sequence=0、parameter errors=0、Health OK、
active/sticky=0、OLED online、输出门锁定。

## 6. Phase 1F 硬件门禁

| 门禁 | 状态 | 说明 |
| --- | --- | --- |
| 物理 ADC 五键 | deferred | 无电阻梯形、引脚冻结和 ADC 实测分布；虚拟输入契约已通过 |
| Flash 掉电保存 | deferred | scatter 未保留参数槽；未执行 Flash 擦写或损坏注入 |
| 硬件看门狗 | deferred | 未完成 owner/window/debug halt/reset cause/执行器安全板测，默认未启用 |

这些 deferred 不允许被描述为通过。它们不阻塞 Phase 2A 的独立台架实现，但真实电机输出前
必须重新审查安全影响；物理菜单、赛场掉电保存和看门狗分别在对应硬件准备完成后闭环。

## 7. 已知风险

- OLED 仍可能是临时杜邦线；正式整车需短线、可靠共地、核实上拉和去耦。
- 两块天猛星中异常发热的板继续禁用。
- OpenOCD 目标端 CRC 算法偶尔无法 halt；脚本的逐字节 Flash 回读 SHA-256 回退已通过。
- COM 号会变化；工具已要求显式 `-Port`，不能把本次 COM4 当永久事实。
- Health frame 已扩展为 132 B，SerialTx 原子写上限同步提高到 160 B；主机解析器兼容旧 112 B 和新 116 B Health payload。
- AT8236 逻辑安全层已建立唯一写入者、默认零输出和限时单次命令，但物理波形与带载失效状态未验收。
- MG370 的起转/最大 PWM、速度/加速度限制、堵转保护阈值和 PID 尚未冻结，闭环输出接口保持锁定。

## 8. 下一步：Phase 2A

Phase 2A 范围仅限 AT8236、底盘电机和 GMR 编码器。开始前：

1. 已确认正式 main 安全合入 Phase 1F，并从该标签创建独立分支/worktree。
2. 记录电机额定电压/电流、减速比、轮径、轮距和编码器计数定义。
3. 用户再次确认现场安全后烧录 MG370 Profile 固件，VM 断电复核 1 Hz Profile 遥测和四路静止低。
4. 用户在场、单轮架空、另一电机断开、限流和物理断电确认后，先做左轮 5%/200 ms 点动。
5. 冻结左右电机方向与运行限制后完成故障、连续运行、最大编码器速率、电流和温升验收。

接手清理已更新工作流程和架构状态，并加固 Phase 1F field check 的累计 I2C 与 UART/OLED
quiet-window 门禁。这些维护不代表 Phase 2A 功能已经实现。

当前左轮编码器配置为 D153B E1A/E1B -> PA29/PA30 TIMG8 硬件 QEI。用户确认实际信号为
3.3 V；向前原始计数为正，provisional 输出轴 CPR 为 68,028，闭环前仍需多圈精确标定。

不得提前混入底盘 IMU、云台或树莓派功能。长期阶段顺序见 `docs/CURRENT_WORKFLOW.md`。

## 9. Phase 2A 当前确认基线（2026-07-16）

| 项目 | 结果 |
| --- | --- |
| 当前板上 HEX | `BB4421DBC943CA4A1EDF7CE6B48448BB5106D8F342FCC1B0BC85465DE0B1E5A5` |
| 构建 | FreeRTOS/App full rebuild 0 Error / 0 Warning；Code=64,768，ZI=16,980 |
| 5 rpm 蠕行 | 10 秒完整；1 秒窗口左 `5.21–5.81`、右 `4.85–5.52 rpm`；Health clean |
| 8 rpm PI 边界 | `8.010/7.991 rpm`；Health clean |
| 60 / 100 rpm | `59.959/60.025`、`100.019/100.018 rpm`；Health clean |
| 120 rpm 连续 | 30 秒、3000/3000；`120.012/120.011 rpm`；约 0.408 m/s；Health clean |
| 10 -> 120 rpm | `t90=280/280 ms`，skew 0 ms，overshoot `7.57%/8.61%` |
| 120 -> 10 rpm | `t90=540/530 ms`，skew 10 ms，同方向降速禁止反向驱动 |
| 147 rpm 探索 | 不采用；编码器 late=217 且闭环模型超出线性区，比赛上限降为 120 rpm |
| 人工阻力扰动 | not run；用户确认上一组没有施加外力，1 CRC / 1 gap 仅为主机串口偶发 |
| 最终静止收尾 | 511 Control / 5 Health / 5 Profile；左右 delta=0；CRC/gap/deadline/active/sticky/I2C=0；output permitted=0 |

当前 120 rpm 上限仅代表轮组架空、12 V、约 0.5 A 限流和现有线材条件。轮组落地后的电流、温升、
直线同步和抗扰仍需独立验收。正式工程 `E:\ECHO` 未修改，当前 worktree 未自动提交、暂存或 push。

## 10. Phase 2A 速度控制更新（2026-07-16 18:26）

- 当前板上固件：MG370 Profile v13，HEX
  `43F7F69B207BA39BBF5C847B935DD05E4EADD682780F1B982CF6C6A3291B5FAF`。
- full rebuild：FreeRTOS/App 0 Error / 0 Warning，Code=65,200，ZI=17,020；DAPLink program/verify/reset passed。
- 普通区 PI 仍为 `Kp=8/Ki=18/Kd=0`；新增有资格门控的负载释放快速退积分，目标变化不会触发该路径。
- 正常启动 50%，机械死点恢复 60% / 最长 80 ms；软件速度/PWM 上限仍为 120 rpm / 900 permille。
- v13 `120->10 rpm`：`t90=550/540 ms`，尾段 `9.956/9.979 rpm`；v13 `10->120 rpm`：`t90=270/280 ms`，超调 `7.97%/7.64%`。
- v13 最终单左轮人工扰动通过：左轮最低 `45.422 rpm`，PWM `507.9->670 permille`，松手峰值 `67.384 rpm`（`12.31%`），820 ms 内进入并持续保持目标 ±3%；右轮均值 `59.999 rpm`。
- 最终静止收尾为 512 Control / 5 Health / 5 Profile，CRC/gap/deadline/drop/I2C/active/sticky/encoder late 全零，输出门禁为 0。
- 当前测试命令已结束并自动归零。正式 `E:\ECHO` 仍未修改；未自动暂存、提交或 push。
- 阶段状态：架空直流电机与编码器调试完成。电流、温升、落地负载、实际补偿、直线同步和带载速度上限 deferred 到后续阶段。

## 11. 513X profile correction and validation (2026-07-17)

- The newly tested motor was corrected from the temporary name 513A to 513X.
- Active profile: ID 2, Model 513X, version 4. IDs 3 and 4 are reserved for
  independent 513A and 513B profiles and remain compile-locked.
- 513X uses GMR 500 PPR, provisional CPR 56000/14000, left/right forward motor
  signs +1/-1, 600 permille startup, 650 permille maximum output, and a 70 rpm
  software limit.
- Closed-loop defaults are Kp=3, Ki=8, Kd=0 and are restored from the selected
  profile after reset.
- Final 70 rpm / 30 s validation passed at 70.014/70.012 rpm with 3000/3000
  frames and clean CRC/gap/deadline/encoder-late/Health diagnostics.
- Final HEX SHA-256:
  `EA016F7F9D6A9B093C978E37359AD7DED31EC9FCB172D4B4D9D7871BEADBA1F9`.
- Control telemetry now includes both left and right normalized PWM with legacy
  40-byte control-frame parser compatibility.
- Valid 60 rpm load tests passed on both wheels. Left load: minimum 18.853 rpm,
  230 ms recovery, 5.01% overshoot. Right load: minimum 27.857 rpm, 150 ms
  recovery, 5.71% overshoot. The untouched wheel stayed near 60 rpm in each run.

## 12. 513X 4S battery profile (2026-07-18)

- The original `MG513X v5` 12 V profile is preserved as profile ID 2. The active build now uses independent `513X-4S v3`, profile ID 5.
- The old profile failed on 4S at a 30 rpm command: approximately 37.1/35.1 rpm with both integrators saturated at -90 permille.
- The final 4S profile retains 600 permille startup and 650 permille maximum output, while steady feedforward is left `409 + 1.09*rpm` and right `401 + 1.62*rpm`.
- Final 30 rpm tail mean is 30.865/30.111 rpm. Final 30 to 70 rpm step has t90 670/600 ms, peak 73.286/71.143 rpm, and 70 rpm tail mean 70.129/70.059 rpm.
- All final device-side Health, deadline, DMA stall, encoder-late, and output-residue checks passed. The final HEX SHA-256 is `D110D1B5DAC13BEDFFC3744CF93412CA949CEF1FC9267713D58061968F7BF0F2`.
- Host UART is not production-clean during combined motor and 125 Hz reflectance operation: 40 CRC/gap errors in the final step and 63 in the final 30 rpm run. Hardware damping and wiring correction remain required.
- Details: `docs/worklogs/2026-07-18_513x_4s_battery_validation.md`.

## 13. 4S supply-voltage ADC bring-up (2026-07-19)

- Supply sensing is implemented on `PB17 / ADC1_CH4`, independently of the
  reflectance input on `PA26 / ADC0_CH1`.
- The required external network is battery positive -> `100 kohm` -> PB17,
  PB17 -> `22 kohm` -> GND, and PB17 -> `100 nF` -> GND. Battery ground and
  controller ground must be common. Never connect the 4S positive terminal
  directly to PB17.
- Firmware samples at 100 Hz, applies a 1/8 IIR filter, and publishes UART type
  9 voltage telemetry at 10 Hz. The conversion formula is
  `raw / 4095 * 3.3 * 122 / 22`.
- Full App rebuild passed with 0 errors and 0 warnings. HEX SHA-256:
  `6BC7F10A6E4A4C1EC118E01DF2B7931DBE409B945A5F398E0238AAC5B5DB2E2C`.
  DAP programming at 500 kHz passed byte-for-byte Flash readback verification.
- The first 10-second static capture received 102 voltage frames at 10 Hz and
  confirmed a 100 Hz ADC sample rate with zero conversion timeouts, zero
  deadlines, zero device-side drops, and actuator output disabled.
- The measured PB17 input was only about 0.58 V, producing an invalid 3.17-3.39
  V battery estimate. This is not a valid 4S reading and indicates that the
  divider is absent, incorrectly wired, or not connected to the battery.
- A subsequent direct low-voltage input test passed: a nominal 1.5 V cell
  applied to PB17 through a 10 kohm safety resistor produced raw 1667-1687,
  average raw 1678.824, and approximately 1.353 V at the ADC input. The 10-second
  peak-to-peak input variation was approximately 16 mV, with zero conversion
  timeouts. The displayed 7.50 V battery value is expected because firmware is
  already configured for the future 100k/22k divider.
- Final voltage accuracy remains uncalibrated until the divider is physically
  verified and a simultaneous multimeter battery reading is supplied.
- Detailed evidence: `docs/worklogs/2026-07-19_supply_voltage_adc_validation.md`.

## 14. 513X-4S PID tuning (2026-07-19)

- `513X-4S` Profile 已从 v3 更新为 v4，冷启动默认 PID 为 `Kp=4`、`Ki=10`、`Kd=0`；12 V `MG513X v5` 未修改。
- 本次 Profile、断言和调参记录已提交为 `294893d`，尚未 push。
- 双轮 `30->70 rpm` 的 t90 为 570/580 ms，峰值 71.679/71.150 rpm，尾段 70.004/70.059 rpm。
- 双轮 `70->30 rpm` 的 t90 为 620/620 ms，下冲 8.84%/4.29%，尾段 30.09/30.32 rpm。
- `Ki=12/14` 与 `Kd=0.1` 均经实机 A/B 后拒绝；继续增大积分会放大降速下冲。
- Keil App 构建 0 Error / 0 Warning；HEX SHA-256 `6642AE8A449EDFA9C6FA09C44501B7B458E3814838A417242089BD2E4715ADF8` 已通过 CMSIS-DAP 烧录、校验和复位。
- 冷启动遥测确认 Profile ID 5 / v4、默认 `4/10/0`、左右输出全零、Health active/sticky/deadline 全零。
- 主机端仍有红外切换耦合造成的 CRC/gap；落地负载、直线同步、电流和温升 deferred。
- 详细证据：`docs/worklogs/2026-07-19_513x_4s_pid_tuning.md`。

## 15. TFmini-S 激光测距接入（2026-07-19）

- 照片板内丝印为 `TFmini V1.8.1`；外形、线序、9 字节测距帧和实机 `0x5A` 回包确认
  按 TFmini-S 驱动。固件版本原始 `07 03 02`，显示为 `2.3.7`。
- 当前 UART1 为 PA8 TX、PA9 RX、115200 8N1；红线必须使用 5.0 V +/-0.1 V，黑线共地，
  黄线 RXD、绿线 TXD，逻辑电平 3.3 V LVTTL。
- 新驱动使用中断环形缓冲和 ServiceTask 流式解析，不新增任务；支持距离、强度、芯片温度、
  帧率、无效状态、校验错误、超时和固件版本诊断。UART0 type 10 以 20 Hz 对外发布。
- FreeRTOS/App full rebuild 0 Error / 0 Warning；76,056 B Flash 逐字节回读 SHA-256
  `7B5FE3ABA8FD12EE37E82819C0FD85A7A8E3F16E61EBE3F93EFA64F64E2A29D9` 一致；
  HEX SHA-256 为 `9FDEE670AA28F049945DE41AE510654FC9CF010BF8A656B792947F25AA463DFF`。
- 20 秒无断点实测为 2007/2007 有效帧、0 无效、0 校验、0 超时、0 UART 溢出，设备帧率
  约 97.9 Hz；SystemTask/ServiceTask 为 2041/20415，deadline/fault 为 0，电机输出全零。
- 10 个一秒静态样本为 103–104 cm，强度 2328–2337，芯片温度 56.00 C。
- TFmini-S 支持 I2C 0x10 / 400 kbit/s，但 PA8/PA9 没有硬件 I2C 复用；未来推荐迁移到
  PA0/PA1 的 I2C0 与 OLED 0x3C 共总线。当前未切换或保存 I2C 设置。
- 详细接线和协议：`docs/hardware/TFMINI_S_LIDAR.md`；过程记录：
  `docs/worklogs/2026-07-19_tfmini_s_uart_bringup.md`。
