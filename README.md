# MaixCAM Pro 钢球位置检测与控制输出

本工程使用 MaixCAM Pro 从固定俯视相机中检测半圆水管内钢球的一维位置，并通过 UART 向 MSPM0G3507（天猛星）发送位置、速度和状态。当前请求模式为 `640x480 @ 60 FPS`；网页显示检测、偏移和控制频率，并按需提供低帧率 JPEG 预览。

## 给队友和 AI 的快速入口

按以下顺序读取即可理解工程：

1. `docs/CODEX_HANDOFF.md`：AI 必须遵守的板端生命周期、部署、恢复和标定红线。
2. `config/ball_detection.json`：相机、ROI、阈值、标定、UART 和温度参数。
3. `app/maixcam_ball_control.py`：正式实时主循环。
4. `app/ball_detector.py`：空管差分、一维投影、峰值和质心。
5. `app/ball_uart_protocol.py`：22 字节协议、CRC16、alpha-beta 速度估计。
6. `app/status_server.py`：状态 JSON、网页和单张 JPEG 接口。
7. `app/preview_encoder.py`：后台按需编码并丢弃过期预览帧。
8. `app/reference_calibration.py`：多帧平均、原子替换、标定历史和旧参考备份。
9. `app/capture_empty_reference.py`：停止正式程序后单独采集空管基准的备用入口。

不要在没有新标定和测试证据时同时修改 ROI、曝光、阈值和毫米换算。`FLAG_DETECTED=0` 或 `FLAG_REFERENCE_MISMATCH=1` 时，下位机不得使用位置闭环。

## 识别流程

```text
640x480 灰度图
-> 固定长条 ROI（当前 0,190,640,100）
-> 与多帧平均空管基准作差
-> 中值补偿整体亮度变化
-> 差值阈值 18 得到变化像素
-> 沿水管宽度方向压成一维响应
-> 28 px 窗口卷积并搜索局部峰值
-> 峰值附近加权质心得到钢球 x
-> 以 ROI 长边中心 ROI_X + ROI_WIDTH / 2 为 O 点
-> 输出 offset_px，并按临时标定换算 offset_mm
-> alpha-beta 估计速度
-> UART 每个新相机帧发送一包
```

钢球不需要是规则二值圆；算法检测的是它相对空管基准造成的局部变化。全局变化比例超过 `0.45` 时判定参考失配并清除有效输出。

## 轻量误识别过滤

正式程序仍然是原来的 `app/maixcam_ball_control.py`，没有新增第二套识别入口，也没有改变标定文件和 UART 协议。每帧处理顺序补充为：

```text
启动时校正一次水管上下位置
-> 全局亮度补偿
-> 空管差分和二值化
-> 一维投影并保留最多 5 个候选峰
-> 面积、宽度和高度初筛
-> 首次出现连续 3 帧确认
-> 已锁定时按预测位置、最大速度和跳变门限选择候选
-> 不可能跳变不更新滤波器，清除 DETECTED
-> 合法测量进入 alpha-beta 位置与速度估计
-> UART 发送
```

默认最大钢球速度为 `800 mm/s`，瞬时跳变余量为 `20 px`，预测窗口为 `70 px`。因此远处反光即使响应更强，只要原轨迹附近仍有合法候选，就优先保留原目标；如果没有合法候选，本帧显示 `REJECTED`，而不是输出远处坐标。参数集中在 `alignment`、`detector` 和 `measurement_filter` 三个配置段。

状态接口增加 `raw_detected`、`measurement_rejected`、`rejection_reason`、`candidate_count`、`raw_center_x`、`expected_center_x`、`filter_locked`、`rejected_jumps`、`vertical_shift` 和 `alignment_update_ms`，用于区分“没有差分目标”和“发现目标但因不符合物理运动被拒绝”。

板端实测动态上下对齐单次约 `192 ms`，每帧局部亮度补偿额外约 `17 ms`，因此正式配置把 `alignment.update_every_frames` 设为 `0`、把 `detector.local_brightness_enabled` 设为 `false`。两段可选实现仍保留用于后续优化，但当前不能在要求高帧率时打开。

## UART

- MaixCAM Pro：`UART0`，`A16=TX`，`A17=RX`，`/dev/ttyS0`。
- 波特率：`115200`，8N1；22 字节在 60 Hz 仅需 `13.2 kbit/s`。
- 电平：仅允许 `3.3 V TTL`，两板可靠共地，TX/RX 交叉。
- 多字节字段：小端序。
- CRC：CRC16-CCITT-FALSE，`poly=0x1021`，`init=0xFFFF`，覆盖字节 `2..19`。

MaixPy 默认使用 UART0 运行 Maix Comm Protocol。正式程序在打开控制串口前调用 `maix.comm.rm_default_comm_listener()` 释放该端口，然后映射 A16/A17。应用运行期间 UART0 专用于下位机控制，MaixVision 的 UART 通道不可同时使用；网页和 SSH 不受影响。

| 字节 | 字段 | 类型/单位 |
| --- | --- | --- |
| 0..1 | 帧头 | `AA 55` |
| 2 | version | `01` |
| 3 | type | `01` 状态包 |
| 4 | length | `16`，即 22 字节 |
| 5 | flags | 状态位 |
| 6..7 | seq | `uint16` |
| 8..11 | capture_time_ms | `uint32` |
| 12..13 | center_offset | `int16`，0.1 mm |
| 14..15 | velocity | `int16`，mm/s |
| 16 | confidence | `uint8`，0..255 |
| 17 | reserved | 0 |
| 18..19 | age_ms | `uint16` |
| 20..21 | crc16 | `uint16` |

`flags`：bit0检测到、bit1速度有效、bit2预测值、bit3低置信度、bit4参考失配、bit5标定有效、bit6温度警告。

固定测试包：

```text
AA 55 01 01 16 23 E8 03 40 E2 01 00 EA 00 82 FF E6 00 03 00 21 9A
```

## 标定

最终固定相机、水管和照明后，先移走钢球，再运行 `app/capture_empty_reference.py`。它采集 32 帧平均值并写入：

```text
/root/ball_detection/runtime/empty_pipe_reference.npy
/root/ball_detection/runtime/empty_pipe_reference.json
/root/ball_detection/runtime/empty_pipe_reference.pgm
/root/ball_detection/runtime/empty_pipe_reference_raw.jpg
```

随后把钢球放在多个已知毫米位置，更新配置中的 `mm_per_pixel` 和 `position_sign`。当前 O 点固定由 ROI 中心计算，`250/640 mm/px` 只是“25 cm 水管占满 640 px”的临时线性标定。

正式程序还提供隐藏接口 `POST /calibrate/empty`，网页上没有标定按钮。启动程序、重启板子、打开网页或读取状态都不会触发标定；必须在确认相机、水管、照明固定且钢球已移走后显式发送：

```sh
curl -X POST http://10.5.66.1:8080/calibrate/empty
```

程序复用当前摄像头采集 32 帧，期间 UART 输出保持无效。通过 `/status.json` 查看 `calibration_state`、`calibration_captured_frames`、`calibration_target_frames`、`calibration_id` 和 `calibration_error`；状态变为 `succeeded` 后，新参考立即生效，无需重启。进行中的重复请求返回 HTTP `409`，历次参考保存在 `/root/ball_detection/runtime/calibrations/`。

## 板端运行

正式应用必须由 `/maixapp/apps/launcher/launcher_daemon` 启动，应用 ID 和入口分别为：

```text
ball_detection_control
/maixapp/apps/ball_detection_control/main.py
```

禁止在 SSH 中直接执行 `python3 app/maixcam_ball_control.py`、`nohup` 或 `scripts/maixcam_ball_control_launcher.sh`。这会绕过 Maix 应用生命周期，使 launcher UI 与检测进程同时占用媒体资源，实测会把约 `40–46 Hz` 降到约 `13 Hz`。安全启动和恢复步骤见 `docs/CODEX_HANDOFF.md`。

开机应用日志：

```text
/maixapp/tmp/last_run.log
```

`scripts/maixcam_ball_control_launcher.sh` 只保留为无 `launcher_daemon` 环境下的恢复入口；正常 MaixCAM 系统会拒绝直接运行它。

状态网页为 `http://10.5.66.1:8080/`，JSON 接口为 `http://10.5.66.1:8080/status.json`，单张预览为 `http://10.5.66.1:8080/frame.jpg`。预览默认 `480x360 @ 2 FPS`、JPEG 质量 55，仅在网页请求时由后台线程编码；队列只保留最新帧。

开机启动应用 ID 为 `ball_detection_control`，入口是 `/maixapp/apps/ball_detection_control/main.py`。`scripts/enable_autostart.sh` 把该 ID 写入 `/maixapp/auto_start.txt`；`disable_autostart.sh` 恢复原文件。任何时候只能有一个进程占用相机，停止程序只能对已核验 PID 使用 `SIGTERM`。

## 测试与当前证据

电脑端：

```sh
python -m unittest discover -s tests -v
```

- 电脑端 24 项检测、候选选择、跳变拒绝、协议、CRC、滤波、配置、标定归档、状态页和 JPEG 接口测试通过；本次轻量误检过滤仍需部署到 MaixCAM 后复测实际帧率和实物效果。
- V3 实测无预览约 `41.93 Hz`，打开 2 FPS 预览约 `40.81 Hz`，下降约 2.7%；单张约 16.5 KB、编码约 30 ms，`preview_errors=0`、`uart_errors=0`。
- 最终毫米精度必须在正式机械安装、重新采集空管基准并放置真实钢球后验收；合成测试不能替代实物精度测试。
