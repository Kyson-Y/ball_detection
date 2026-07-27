# 2026-07-20 右编码器引脚迁移

性质：配置变更，静态检查和全量构建通过，未烧录

## 目标

- 将右编码器从 PB6/PB7 迁移到 U6 排针的 PA25/PA24。
- 保留左编码器硬件 QEI、右编码器软件 x1 解码和现有电机控制参数。

## 引脚

| 信号 | 原引脚 | 新引脚 | 用途 |
| --- | --- | --- | --- |
| E1A | PA29 | PA29 / TIMG8_C0 | 左编码器硬件 QEI PHA |
| E1B | PA30 | PA30 / TIMG8_C1 | 左编码器硬件 QEI PHB |
| E2A | PB6 | PA25 / U6-5 | GPIOA 上升沿中断，软件 x1 |
| E2B | PB7 | PA24 / U6-6 | GPIOA 方向输入 |

红外地址线已经迁移为 PB0/PB1/PB11，所以 PA24/PA25 已释放。SysConfig 全量物理引脚扫描
未发现重复分配；UART、I2C、按键、供电 ADC、蜂鸣器和 LED 也不占用 PA24/PA25。

## 实现

- 只在 `config/ECHO.syscfg` 中修改右编码器的两个物理引脚。
- 由 SysConfig 重新生成 `platform/generated/ti_msp_dl_config.c/.h`，未手改生成文件。
- 生成宏为 `GPIO_RIGHT_ENCODER_PORT=GPIOA`、E2A=`DL_GPIO_PIN_25`、
  E2B=`DL_GPIO_PIN_24`，E2A IRQ 为 `GPIOA_INT_IRQn` / `DL_INTERRUPT_GROUP1_IIDX_GPIOA`。
- `keil/startup_mspm0g350x_uvision.s` 将 Group1 向量指向 `GROUP1_IRQHandler`，
  `bsp_encoder.c` 已实现该处理函数，因此不需要改动解码代码。

## 验证

- SysConfig：0 error；仅保留既有 ProjectConfig warning。
- FreeRTOS 全量构建：0 Error / 0 Warning。
- App 全量构建：0 Error / 0 Warning。
- Code=77,420，RO-data=3,428，RW-data=188，ZI-data=19,044。
- HEX SHA-256：
  `E90D613C8BE05F2E90DA3EA55EE81649FA94713333D767BE2A20F3E45C445CB8`。

## 未完成

- 按用户要求未烧录，未访问 MCU。
- 未在 PA25/PA24 新接线上执行正反手转、方向符号、计数连续性和静止零漂移板测。
- 板测前必须断开电机动力和 4S；编码器信号高电平不得超过 3.3 V，主控必须共地。
