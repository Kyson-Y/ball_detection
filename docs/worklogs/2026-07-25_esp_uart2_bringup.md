# 2026-07-25 UART2 与 ESP32-S3 双向 DMA 链路

> 本文前半记录最初单套板、921600 与临时回显夹具的阶段性结果。最终已降至 230400，
> 并完成双套天猛星 + ESP32-S3 的 ESP-NOW 端到端测试；当前结论见
> `docs/worklogs/2026-07-25_espnow_dual_board_link.md`。

## 目标与边界

- 为后续双车 ESP-NOW 通信建立一组天猛星到 ESP32-S3 的独立有线 UART。
- U0 继续负责 DAPLink/电脑遥测，UART2 不复用现有 `SerialTx`。
- 本次只做低功耗通信，不发送电机命令，不验证 ESP-NOW 端到端。

## 接线

```text
DAPLink UART <-> 天猛星 U0，电脑端 COM4，230400 8N1
天猛星 PB15/UART2_TX -> ESP32-S3 GPIO18/UART1_RX
天猛星 PB16/UART2_RX <- ESP32-S3 GPIO17/UART1_TX
天猛星 GND           --- ESP32-S3 GND
ESP32-S3 USB          -> 电脑 COM17
```

ESP32 由自身 USB 供电，没有把两块板的 3.3 V 电源轨并联。电脑上连接的 MaxiCam 未访问。

## 实现

- SysConfig 中 `ESP_LINK_UART/UART2` 目标 921600 baud，PB15 TX、PB16 RX。RX 使用
  `DMA_CH1` Full Channel repeat-single 连续写 512 B 缓冲，TX 使用 `DMA_CH2`。
- `bsp_esp_uart` 保持 `Init/TryRead/TryWrite/ServiceTx` 接口；正常路径由 DMA 搬运，
  `ServiceTx` 只保留兼容入口。任务轮询 DMA 剩余计数读取短帧，无需等待整块填满。
- 初始 TX DMA 使用 FIFO `ONE_ENTRY` 阈值时，ESP 坏帧固定少 FIFO 边界字节；改为
  `FIFO EMPTY` 后该模式消失。RX 初始软件重装块存在空窗，改为硬件 repeat；随后又修复
  repeat 回卷与 ISR 完成计数竞态，最终生产位置只由任务侧剩余计数维护。
- `esp_uart_link_test` 每 20 ms 发 16 B PING，使用 CRC16-CCITT、序号和时间戳；等待 ESP32
  改为 ACK 后回传，100 ms 超时。
- UART0 telemetry type 12 升级为 schema 2、96 B payload，增加 RX/TX DMA done、RX reload、
  EOT、IRQ、high-water 和 DMA active；PowerShell 抓取工具已支持解码。
- ESP32 使用 MicroPython 临时回显夹具 `tools/esp32/uart_echo_test.py`。该夹具只验证串口，
  断电后不会自动运行，不能当作最终 ESP-NOW 固件。

## 构建与烧录

- SysConfig：0 error / 1 个既有 ProjectConfig warning。
- FreeRTOS/App 全量构建：0 Error / 0 Warning。
- Program size：Code=80,620，RO-data=3,492，RW-data=188，ZI-data=20,012。
- HEX SHA-256：`7EAC01309C923D2BAA554AA8B155E2B092CA3F350246A2E75739E2AD21B7B3AA`。
- DAPLink：CMSIS-DAP `4CDD7B98801CB180A5B29C09725A99B3`，SWD 500 kHz。
- OpenOCD 目标 CRC 算法仍可能超时；脚本按既有设计回退到 84,304 B Flash 逐字节回读。
- Flash binary/readback SHA-256：
  `4CA80A808D0DCF61BC1B7AB5B92A20153569C7214BB26D0E8206C861E47EEE01`，一致后 reset run。

## 板上结果

最终 120 秒无中途调试的天猛星 RAM 快照：

```text
tx_frames=6039
ack_frames=6025
timeouts=14
crc_errors=0
format_errors=0
unexpected_sequence=0
rx_bytes=96400
tx_bytes=96624
rx_overflow=0
rx_dma_done=188
rx_dma_reload=188
tx_dma_done=6039
tx_eot=6039
unexpected_irq=0
rx_high_water=16
rx_dma_active=1
rtt_min_us=1926
rtt_avg_us=2744
rtt_max_us=4142
rtt_last_us=3000
```

RX 字节数严格等于 6025 个完整 ACK 乘 16，TX 字节数严格等于 6039 个 PING 乘 16；
DMA 完成计数、EOT 和回卷计数相互一致，证明数据正常路径确实由 DMA 完成。RTT 相比旧的
CPU FIFO 版本明显降低。

但 TX/ACK 相差 14，约为 0.23%。ESP32 独立计数也观察到少量 MCU->ESP CRC 坏帧；
16x MSPM0 实际约 919,540 baud，ESP32 默认档实际约 922,190 baud。尝试 ESP 919,540、
920,863 以及 MSPM0 8x/922,190 的 A/B 均未稳定得到零误码，最终保留接收容差更高的
MSPM0 16x 和 ESP 默认 921600。结论是 DMA 驱动已通过，当前跳线物理层尚非生产零误码。

随后一个 type 12 短窗口记录为 1,202 TX、1,178 ACK、24 timeout（约 2.00%），高于上述
120 秒长测。该差异说明当前跳线下的误码率会随采样窗口和电气条件明显波动，0.23% 不能
当作固定上限，也不能据此宣称链路达到生产零误码。

U0 仍会出现此前已知的灰度地址切换电气串扰，第一次 PowerShell CRC 解析因此丢弃了大量
主机帧；直接按 type 12 固定头提取和 ESP32/设备端独立计数一致。该 U0 问题不影响本次
UART2 双向结论，但仍需在正式整车上处理线束/串联阻尼并复测。

## 下一步

1. 缩短 UART2 线束、TX 与 GND 成对布线，并在两端 TX 源端 A/B 22--47 ohm 串联阻尼。
2. 若 921600 仍非零误码，改为 460800 并重做至少 120 秒测试；正式协议必须保留重试。
3. 用 ESP-IDF 实现 UART1<->ESP-NOW 双向桥，保留 CRC、序号、ACK、重传、去重和心跳。
4. 第二组天猛星与 ESP32 使用相同接线，完成双车端到端测试；无线异常不得触发非零电机输出。
