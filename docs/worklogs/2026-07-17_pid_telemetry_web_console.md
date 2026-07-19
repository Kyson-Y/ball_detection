# 2026-07-17 PID 遥测网页与在线控制

## 目标

- 同步显示左右目标速度与实际轮速。
- 显示左右轮总 PWM、P、I、D 和前馈分量，并允许逐条选择曲线。
- 网页一次下发共享 `Kp/Ki/Kd`，逐项校验参数 ACK 和应用序号。
- 网页分别下发左右轮 `-100..100 rpm` 持续目标，`0/0 rpm` 显式停机。
- 保持 40 B、44 B 历史 Control payload 的主机解析兼容性。

## 实现

- Control payload 追加到 96 B，新增右目标、左右 P/I/D/前馈、当前 Kp/Ki/Kd 和参数应用序号。
- `chassis_actuator_diagnostics_t` 发布左右控制器分量，不改变控制器所有权。
- 速度命令 `duration_ms=0` 定义为持续运行；持续目标只因 `0/0 rpm`、故障或复位停止。
- 513X Profile 升级到 v5，软件速度上限由 70 rpm 调整到 100 rpm。
- 网页改为两个同步 20 s 图表，14 条曲线可选，暂停仅冻结显示，采集继续。
- 在线 PID 复用 frame type 2/3，速度复用 frame type 5/6，没有新增串口协议族。

## 验证

- PowerShell AST：通过。
- 混合 40/44/96 B fixture：202 有效帧、200 Control、100 Hz、CRC/gap 全零。
- Keil App：`0 Error / 0 Warning`，Code 66,608 B，RO 3,168 B，ZI 17,052 B。
- 最终 HEX SHA-256：`B2287E3E460F7955D24B4E6BA04C3DF6BDFD31DDAED89D8A461E9B8C9E783CE8`。
- DAPLink 烧录：program、verify、reset 通过。
- Web smoke：Edge headless 桌面 1440x1000 与手机 390x844 均通过，无横向溢出，Canvas 非空，暂停交互通过。
- COM7 PID ACK：3/8/0 均一次成功，apply sequence 1/2/3。
- COM7 零速持续命令：`0/0 rpm`、duration 0、mode 1 返回 actuator ACK status 0。
- COM7 最终静态采集：209 Control、100 Hz、CRC/gap/out-of-order/deadline 全零，Health clean，输出禁止，参数应用序号 3。

## 当前状态与风险

- 板上最终保持 `0/0 rpm`，`ActuatorOutputPermitted=0`。
- 在线 PID 只更新 RAM；关闭网页不会恢复默认值，复位/断电恢复 Profile 默认值。
- 关闭网页不会自动发送停机；持续速度运行时必须重新连接发送 `0/0 rpm`、触发故障停机或物理断电。
- 本次未自动执行非零持续转动；该项需要用户在场、轮组安全和明确运动授权后验证。

## 2026-07-19 固定启动与复测

- 新增根目录 `open_pid_console.cmd` 和
  `tools/open_pid_console.ps1`。重复启动会复用已有服务，并在 Edge/Chrome
  打开固定地址 `http://127.0.0.1:8765/`。
- 本机 Python 较旧，不支持 `http.server --directory`；launcher 已改为使用
  `Start-Process -WorkingDirectory`，服务健康检查返回 HTTP 200，且只保留一个
  Python 服务进程。
- Web smoke 新增串口 fixture，实际点击连接、一次应用 Kp/Ki/Kd 和立即停机，
  验证三帧 parameter set、三帧 parameter ACK、一帧 `0/0 rpm` speed command
  与一帧 actuator ACK。桌面 1440x1000 和手机 390x844 均通过。
- 串口写队列现在可从单次 write rejection 恢复；关闭页面前若最后一次受理目标
  非零，浏览器会触发离开提醒，但仍遵守“不自动停机”的用户要求。
- 页面在串口已打开但无遥测时提示检查 `PA10(TX) -> adapter RXD`；ACK 超时
  时提示检查 `adapter TXD -> PA11(RX)`、共地和 230400 波特率。
- 2026-07-19 当前 COM9 实物复测未通过：Kp=3 连续三次请求均无 ACK，随后
  普通 4 秒采集为 0 字节，置 DTR/RTS 后 3 秒也只有 132 字节。此结果表明当前
  板子供电或 COM9 物理链路状态不满足验收，不能推翻此前 COM7 双向 ACK 证据，
  也不能声称今天的 COM9 下发成功。
- 当前没有发送任何非零速度命令。

## 2026-07-19 模拟调参

- 网页新增与真实串口互斥的 `启动模拟` 模式；模拟模式不打开 COM 口，也不访问
  电机输出。
- 模型复用 513X-4S 左右前馈 `409+1.09*rpm`、`401+1.62*rpm`、650 permille
  输出限制、150 rpm/s 目标斜坡、100 Hz 周期和 P/I/D 分量语义；电机对象为带
  左右差异和轻微测量波动的一阶示意模型。
- 模拟器走与真实设备相同的网页 parameter/actuator ACK 和 96 B Control 解码路径，
  因此能验证按钮、重试、曲线、实时数值和 CSV 数据流，但不能替代实机辨识。
- 70 rpm 对比：`Kp=0.5/Ki=0/Kd=0` 在 3 秒内只达到约
  `61.9/61.5 rpm`，左右均未达到 90% 目标；`Kp=3/Ki=8/Kd=0`
  的 t90 约为 `554/734 ms`，尾段约 `69.994/70.000 rpm`，模型中无明显超调。
- `tests/web/pid_console_simulation_test.cjs` 自动执行上述对比，保存 JSON 和整页
  截图；与桌面/手机 Web smoke 均通过。
- 这些数字只说明网页调参流程和控制器基本作用正确，不作为 513X 实机 PID 验收值。

## 2026-07-19 COM9 真实 Chrome 与本机桥接验收

前文“当前 COM9 实物复测未通过”只描述当时的供电/串口状态，已被本节的有线
COM9 实测结论取代。

- 使用 Google Chrome 实体 Web Serial、真实 `COM9 / 230400` 连续复现：浏览器能
  接收控制帧，但固定 DTR/RTS 组合可能只能收不能发；不同关闭状态下可双向工作的
  组合在 `DTR 1 / RTS 0` 与 `DTR 0 / RTS 0` 之间变化。
- 网页因此改为四种 DTR/RTS 组合逐项执行真实 `0/0 rpm` ACK 自检，只有双向通过
  才解锁速度和 PID 按钮。第三次直接 Web Serial 验收得到 685 Control、1 Actuator
  ACK、3 Parameter ACK、17 CRC/17 gap、0 unexpected ACK。
- 直接 Web Serial 关闭 DAPLink 后触发 MCU 复位；静态收尾看到 uptime 从约 4 秒
  重新开始，RAM apply sequence 回到 0。这违反“关闭网页不停止/不复位”的要求，
  因此直接 Web Serial 只保留为 `?transport=webserial` 诊断模式。
- 新增 `tools/telemetry_bridge.py`，只使用 Python 3.6 标准库和 Windows `CreateFileW /
  ReadFile / WriteFile` 串口 API。桥进程持续独占 COM9，网页通过本机 HTTP 长轮询和
  二进制 POST 通信；刷新、关闭或断开网页不会关闭串口或切换 DTR。
- 正式 `http://127.0.0.1:8765/` 桥接版在真实 Google Chrome 首次连接通过：80 个
  Control 帧、1 个用户 `0/0 rpm` Actuator ACK、3 个 `4/10/0` Parameter ACK，
  CRC/gap/unexpected ACK 全零，参数应用序号为 4/5/6。
- Chrome 关闭后桥仍报告 `COM9 connected`。随后板端 uptime 连续到 985000 ticks，
  PID 保持 `4/10/0`、apply sequence 6，左右目标/PWM 为 0，
  `ActuatorOutputPermitted=0`、active issue 为 0，证明页面关闭未复位 MCU。
- `open_pid_console.cmd` 现在启动/复用本机桥并优先打开 Google Chrome。命令行串口
  工具运行前必须先停止桥，因为 Windows COM9 是独占资源。
