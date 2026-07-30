# UART1 MaixCAM 视觉链路恢复

## 目标

恢复 MaixCAM 到正式 UART1 `PA9/RX, 115200 8N1`，同时保留 UART2 的 ESP32 通信、
UART0 调试遥测、UART3 张大头和现有 H 任务。

## 根因

正式配置仍为 `ECHO_BALL_VISION_USE_UART2=1`。在该配置下，`main.c` 按设计跳过
`BSP_TfminiUart_Init()`，ServiceTask 也只消费 UART2，因此 UART1 的 NVIC、接收缓冲和
视觉数据路径都未启用。修复前 COM7 向 PA9 发送 880 B，`g_bsp_tfmini_uart_diag` 全为 0。
IRQ 向量、PA9 复用和 UART1 接收实现本身没有故障。

## 修改

- 将视觉输入恢复为 UART1；UART2 自动恢复 ESP link 服务。
- 未修改 UART0、UART1、UART2、UART3 的 BSP 源码，未修改控制或任务状态机。

## 验证

- App：0 error / 0 warning。
- 40 个固定序号帧：UART1 收到 880 B，overflow 0；1 帧有效，39 帧按预期记为 duplicate。
- 100 个递增序号帧：UART1 收到 2200 B，有效帧 100；CRC、格式、重复、丢帧、乱序、
  overflow 和 unexpected IRQ 全为 0，high-water 12 B。
- 同期 UART2 ESP TX DMA 正常完成发送；SystemTask deadline 0。
- 固件已烧录；未执行底盘或张大头运动。
