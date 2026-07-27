# 2026-07-20 红外复用地址线迁移

性质：配置变更，已构建，待板测

## 目标

- 将八路红外灰度板的三根 74HC4051 地址线移到 U6 下方排针。
- 保留现有 ADC 采样、扫描协议和电机安全状态。

## 引脚

| 信号 | 原引脚 | 新引脚 |
| --- | --- | --- |
| OUT | PA26 / ADC0_CH1 | PA26 / ADC0_CH1 |
| AD0 | PA27 | PB0 / U6-29 |
| AD1 | PA24 | PB1 / U6-30 |
| AD2 | PA25 | PB11 / U6-33 |

PB0、PB1、PB11 均为当前分配中的空闲 GPIOB。PB21 保留给 ICM42688 INT1，
PA30 保留给左编码器 E1B，PB22 为板载 LED，PB10 为无源蜂鸣器 PWM。

## 实现

- 更新 `config/ECHO.syscfg` 的 `GPIO_REFLECTANCE_MUX` 三根输出。
- SysConfig 生成 `GPIOB`、`DL_GPIO_PIN_0`、`DL_GPIO_PIN_1` 和
  `DL_GPIO_PIN_11`。
- `bsp_reflectance` 原有一次掩码地址写入继续适用，无需修改扫描逻辑。

## 验证

- SysConfig：0 error；仅保留既有 ProjectConfig warning。
- FreeRTOS 全量构建：0 Error / 0 Warning。
- App 全量构建：0 Error / 0 Warning。
- Code=77,420，RO-data=3,428，RW-data=188，ZI-data=19,044。
- HEX SHA-256：
  `61F45C67FE54B046A767785E6619B3E8D09D572A8AF839605001810A3CBF675B`。

## 未完成

- 未烧录。
- 未按新线序执行八路扫描、通道顺序、ADC timeout 和串口 CRC/gap 板测。
- 换线前必须断电；`OUT` 仍只能接 PA26，不能接普通 GPIO。
