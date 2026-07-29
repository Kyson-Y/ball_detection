# OLED 与按键显示响应排障

## 现象

- MAIN 页 IMU/航向显示不更新，按键看起来没有反应。
- UART 遥测确认 IMU 在线且持续更新，控制、IMU 和姿态帧均正常。

## 根因

H3 新增 25 Hz 滚球遥测后，UART0 DMA 更接近持续忙状态。DisplayTask 使用
`SerialTx_TryBeginPriorityQuietWindow()` 直接碰撞当前 DMA，没有先阻止下一块 DMA 启动。
修复前 20 秒内 OLED quiet window 被延后 1062 次，只完成 37 次刷新。按键扫描任务仍在
运行，但页面反馈太慢，因此表现为按键无反应。

## 修改

DisplayTask 每次刷新前先登记 priority quiet request，再领取 requested quiet window。
没有修改控制频率、控制参数、IMU 采样、页面数据源或按键映射。

全量重建同时发现本机 uVision 工程会话曾删掉三个已提交的 H3 源文件条目；已将
`ball_vision.c`、`ball_balance_service.c` 和 `ball_position_controller.c` 恢复到工程，
未改动源码语义。

## 验证

- App 全量重建：0 error / 0 warning。
- 修复后 10 秒：OLED 完整刷新 122 次，I2C 错误 0，deadline 0。
- DisplayTask 最低栈余量：274 words。
- IMU 与姿态数据持续更新；UI 软件注入可从 MAIN 切换到 TEST。
- 固件已通过 DAPLink 烧录并运行；未执行电机或张大头动作。
