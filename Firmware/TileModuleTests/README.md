# 格子模块硬件测试固件

此目录收纳可独立构建、烧录的格子模块硬件验证程序。每项测试使用自己的
PlatformIO 工程，不与 `Firmware/TileModule/` 下的正式固件混合。

当前测试：

- `ST7789DisplayTest/`：验证 J4 八线屏幕连接、显示方向、颜色、边界、SPI
  稳定性和背光 PWM。
