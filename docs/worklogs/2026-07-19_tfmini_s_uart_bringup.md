# 2026-07-19 TFmini-S UART 接入

性质：normal

## 目标与范围

- 识别接在 UART1 的北醒激光测距模块，完成非阻塞驱动、诊断和上位机遥测。
- 确认未来 I2C 能力，但本次不永久切换接口，不改动电机控制参数，不产生运动输出。

## 开始状态

- worktree：`C:\Users\Auror\ECHO-513a-work`
- 分支：`refs/heads/codex/513a-motor-bringup`
- 起始 HEAD：`e99c464`
- 用户已有 dirty 文件：`docs/hardware/ECHO_WIRING_GUIDE.md`；全程未覆盖、还原或暂存。
- 板上起始固件：`MG513X-4S v11`；UART0 为 COM9 / 230400，当前被浏览器串口占用。

## 修改

- SysConfig 新增 UART1：PA8 TX、PA9 RX、115200 8N1、RX FIFO 中断。
- 新增 `bsp_tfmini_uart`：ISR 只搬运到 128 B 静态环形缓冲，单次最多 32 B，命令 TX
  使用 16 B 静态非阻塞缓冲。
- 新增 `tfmini_s`：解析 `59 59` 九字节帧和 `5A LEN` 命令回包，记录距离、强度、温度、
  帧率、无效值、校验错误、超时和固件版本。
- ServiceTask 继续作为设备服务所有者；启动后只读查询固件版本，不新增任务。
- UART0 新增 telemetry type 10 / 64 B payload / 20 Hz；主机采集脚本同步解析。
- 新增协议 C 夹具和 type 10 PowerShell 夹具。

## 硬件状态

- 模块照片板内丝印：`TFmini V1.8.1`；实机协议和版本回包确认按 TFmini-S 驱动。
- 红线 5 V、黑线 GND、黄线 RXD、绿线 TXD；通信为 3.3 V LVTTL。
- 电机仍连接，但命令目标为零；全部测试中左右 PWM 和执行器输出均为零。

## 验证

- TFmini-S 用户手册的接线、UART、I2C、命令页已通过 Poppler 渲染并目视核对。
- SysConfig：0 error，1 个既有 ProjectConfig warning。
- FreeRTOS + App full rebuild：0 Error / 0 Warning；Code=72,656，RO=3,208，RW=188，
  ZI=18,508；HEX SHA-256
  `9FDEE670AA28F049945DE41AE510654FC9CF010BF8A656B792947F25AA463DFF`。
- C 协议夹具：ArmClang `-Wall -Wextra -Werror` 编译通过；本机没有可执行该目标的原生
  C 运行环境，因此合成向量未运行，不能把它记为运行通过。
- 旧 telemetry fixture 和新增 TFmini type 10 fixture 均运行通过。
- Flash 76,056 B 逐字节回读 SHA-256 与构建二进制一致：
  `7B5FE3ABA8FD12EE37E82819C0FD85A7A8E3F16E61EBE3F93EFA64F64E2A29D9`。
- 20 秒同一 OpenOCD 会话内无断点运行后一次 RAM 快照：2007 帧、2007 有效、
  0 无效、0 校验、0 超时、0 UART 溢出；帧率约 97.895 Hz；版本 `2.3.7`。
- 同次 RTOS：SystemTask 2041、ServiceTask 20415、deadline 0、fault 0；左右 PWM、
  applied/normalized output、armed/output permitted 全为 0。
- 10 个一秒间隔静态样本：距离 103–104 cm，均值 103.9 cm；强度 2328–2337；
  温度 56.00 C。10 次主动 halt 后累计 1 个校验错误，只作为调试干扰记录。

## 问题与判断

- 最初采用“先运行、再新建 OpenOCD 会话读取 RAM”的方法时只看到 4–5 ms 计数。
  A/B 证明是 MSPM0 在 OpenOCD examine 时重新启动目标，不是 MCU 在 4 ms 卡死。
- 改为同一 OpenOCD 会话内 reset/run/sleep/halt/dump 后得到完整 20 秒计数。
- 当前 COM9 被浏览器占用，未完成真实 UART0 type 10 串口采集；主机编码/解码契约由构建、
  type 10 fixture 和设备 RAM 诊断覆盖，但串口端到端仍应在释放 COM9 后补一次。

## 风险与下一步

- PA8/PA9 没有硬件 I2C 复用；未来应把 TFmini-S 迁移到 PA0/PA1 的 I2C0，与 OLED
  以地址 0x10/0x3C 共总线。
- 未完成 I2C 接线、上拉和驱动前禁止发送永久接口切换/保存命令。
- 真实车载环境仍需验证阳光、黑色目标、斜面、玻璃、振动和多传感器串扰；当前只证明
  室内静态目标和通信链路。

## 结束状态

- 最终 commit：本文件所在提交。
- push：未执行。
- 用户 dirty 接线文档保持原样；`tmp/` 保存未跟踪的 PDF 提取和原始 RAM 快照。
