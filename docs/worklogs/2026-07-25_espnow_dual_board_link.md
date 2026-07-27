# 2026-07-25 双套天猛星与 ESP32-S3 ESP-NOW 链路

## 目标与硬件

两套天猛星通过 UART2 连接各自 ESP32-S3，由 ESP-NOW 建立双向链路，为后续双车协同或
基站与小车通信做底座。本次只发送 PING/ACK 测试帧，不发送电机命令，执行器始终为 0。

```text
主机：天猛星 U0 COM4，ESP32 USB COM17，ESP MAC AC:A7:04:1D:B6:7C
从机：天猛星 U0 COM7，ESP32 USB COM15，ESP MAC 14:C1:9F:2E:B2:40
每组：PB15/UART2_TX -> GPIO18/UART1_RX
      PB16/UART2_RX <- GPIO17/UART1_TX
      GND            --- GND
```

两块板各自 USB 供电，不连接 3.3 V 电源轨。电脑上的 MaxiCam 未访问。

## 实现

- 天猛星 UART2 最终采用 230400 8N1；RX 为 `DMA_CH1` 512 B repeat-single 环形缓冲，
  TX 为 `DMA_CH2` 且由 FIFO empty 触发。
- 16 B 测试帧包含同步字、版本、类型、序号、时间戳、固定模式与 CRC16-CCITT。每 20 ms
  发送一帧；未收到 ACK 时在 30/60/90/120 ms 重发同一帧，160 ms 后才记逻辑 timeout。
- ESP32 固件为 `tools/esp32/espnow_uart_link.py`，SHA-256
  `C9D769D1E37F9AEEBE2E7B4082BE318B4F30EF7EC974EFE6E3687B5294F80DD6`。两端均持久化为
  `main.py`，按自身 MAC 自动选择角色，ESP-NOW 固定信道 6。
- 无线层包含应用 CRC、序号、ACK、15 ms 重试、重复包处理、MAC 白名单、8192 B 接收缓冲、
  缓冲异常自动重建和 8 秒硬件 WDT。故障注入确认主循环中断后约 8 秒复位并自动恢复。

## 波特率选择

实际 A/B 覆盖 921600、460800、230400 和 115200。921600 误码波动明显；460800 每数千帧
仍有少量 UART CRC；115200 在本次接线下反而更差。230400 的物理误码最少，因此作为当前
最终配置。正式整车仍建议缩短线束、TX 与 GND 成对布线，并预留 22--47 ohm 源端串联阻尼。

## 验收结果

三次 UART 重发版本的 180 秒测试中，主机新增 1 次逻辑 timeout、从机 0 次；两端 ESP-NOW
无线 timeout、invalid、peer error、TX fail、RX drop、buffer error 和 recover 均为 0，ESP 脚本
没有 traceback 或复位。两端 UART2 RX overflow 与 unexpected IRQ 均为 0，DMA 保持 active。

按用户要求不继续追求长测绝对零错误。最终改为四次重发并同步烧录两块天猛星，30 秒短测为：

| 端点 | TX | ACK | 逻辑 timeout | UART CRC/格式/异常序号 | 物理重发 | RX overflow/异常 IRQ |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 主机 | 1,607 | 1,607 | 0 | 0/0/0 | 28 | 0/0 |
| 从机 | 1,639 | 1,639 | 0 | 0/0/0 | 37 | 0/0 |

主机 RTT 最小/平均/最大为 14.949/17.478/80.949 ms；从机为
14.906/17.892/140.001 ms。两端 TX DMA done 与 EOT 计数一致。U0 遥测采集仍受既有灰度
地址切换电气串扰影响并可能返回 CRC 门禁码 2，这与 UART2/ESP-NOW 专用计数分开判断。

## 最终镜像与后续边界

- FreeRTOS/App 全量构建：0 Error / 0 Warning。
- Program size：Code=80,748，RO-data=3,492，RW-data=188，ZI-data=20,028。
- HEX SHA-256：`B524E493244F2068F17D38026BDAE7C621F79F33CDC92FE300A2DE5A9EBB6459`。
- 两块天猛星均完成 500 kHz program/verify/reset。主机调试器序列号为
  `4CDD7B98801CB180A5B29C09725A99B3`；从机调试器为 `FAED:4873/2dc1718e`。

当前链路适合继续开发双车/基站消息协议，但 PING 测试帧本身不是正式业务协议。下一阶段需要定义
节点状态、目标编号、坐标/里程、任务命令、心跳和急停字段，并规定失联时执行器归零；不能让无线
异常直接触发非零电机输出。
