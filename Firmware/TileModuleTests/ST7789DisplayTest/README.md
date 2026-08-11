# ST7789 屏幕接线测试固件

这是格子模块的独立硬件测试工程，用于验证 2.0 英寸 ST7789 240×320
屏幕的八线连接、方向、颜色、显示边界、SPI 稳定性和背光控制。

该工程位于 `Firmware/TileModuleTests/`，不会被
`Firmware/TileModule/` 下的正式固件引用。

## 硬件和依赖

- MCU：ESP32-S3-WROOM-1-N16R8
- 框架：PlatformIO + Arduino
- 屏幕：ST7789，240×320，只写 SPI，无 MISO
- SPI：Mode 0，20 MHz
- 图形库：Adafruit GFX 1.12.6
- 屏幕库：Adafruit ST7735 and ST7789 Library 1.11.0
- USB 日志：115200 baud

PlatformIO 环境固定为 16 MB Flash、8 MB OPI PSRAM 和
`default_16MB.csv` 分区。此测试本身不依赖 PSRAM，但配置保持与真实模组一致。

## J4 八线接线

| J4 | 屏幕标记 | PCB 网络 | ESP32-S3 |
| ---: | --- | --- | ---: |
| 1 | GND | `GND` | - |
| 2 | 3V3 | `3V3_SYS` | - |
| 3 | BL/PWR | `LCD_BL_PWM` | GPIO16 |
| 4 | CS | `LCD_CS` | GPIO15 |
| 5 | SCL/SCK | `LCD_SCLK` | GPIO7 |
| 6 | SDA/MOSI | `LCD_MOSI` | GPIO6 |
| 7 | DC/WR | `LCD_DC` | GPIO5 |
| 8 | RST | `LCD_RST` | GPIO4 |

屏幕上的 `SCL` 和 `SDA` 是 SPI 的 SCLK 和 MOSI，不是 I²C。

> J4 是 2×4 接口，公母连接器面对面插接后会形成镜像。接线前必须同时核对
> PCB Pin 1 标记、屏幕 Pin 1 标记和观察方向，不能只看左右顺序。

只能给屏幕接 3.3 V。断电完成接线和连续性检查后再上电，不要带电插拔屏幕。

## 构建和测试

在仓库根目录运行：

```powershell
pio test -d Firmware/TileModuleTests/ST7789DisplayTest -e native
pio run -d Firmware/TileModuleTests/ST7789DisplayTest -e tile_esp32s3
```

原生测试验证：

- J4 六根 MCU 信号对应正确 GPIO；
- 分辨率、SPI 模式和 20 MHz 频率；
- 上电时先关闭背光、释放 CS，再初始化 SPI 和屏幕；
- 初始化完成、清黑屏后才接管 PWM 并渐亮；
- 十个测试画面的顺序、持续时间和循环；
- `millis()` 回绕时阶段计时仍正确。

构建产物位于：

```text
Firmware/TileModuleTests/ST7789DisplayTest/.pio/build/tile_esp32s3/firmware.bin
```

## 烧录和串口

连接格子模块的原生 USB 接口后运行：

```powershell
pio run -d Firmware/TileModuleTests/ST7789DisplayTest -e tile_esp32s3 -t upload
pio device monitor -d Firmware/TileModuleTests/ST7789DisplayTest -e tile_esp32s3 -b 115200
```

工程不固定 COM 口。如果自动选择错误，可以在命令末尾临时添加
`--upload-port COMx`，或给串口监视器添加 `--port COMx`；不要把本机端口提交到
`platformio.ini`。

上电后串口会输出固件名称、SPI 参数、GPIO 映射、当前阶段和该阶段应观察到的
结果。程序不会等待串口连接，因此不打开监视器也会正常显示。

## 自动显示顺序

1. 红色纯色：整屏均匀红色。
2. 绿色纯色：整屏均匀绿色。
3. 蓝色纯色：整屏均匀蓝色。
4. 白色纯色：检查闪烁、暗区和明显坏点。
5. 黑色纯色：背光保持开启，像素内容为黑色。
6. RGB/CMY 色条：从左到右显示 `R G B C M Y`，用于识别红蓝互换。
7. 边界和四角：显示完整外框、中心十字以及 `TL/TR/BL/BR` 标记。
8. 网格和灰阶：检查丢行、抖动、传输错误和灰阶变化。
9. 诊断页：显示分辨率、SPI、GPIO 和持续增加的帧计数。
10. 背光渐变：亮度平滑升降，然后回到正常测试亮度并重新循环。

屏幕接口没有 MISO，ESP32 无法读取控制器 ID、寄存器或显示像素。因此测试程序
只能证明命令已发送，最终通过条件必须由肉眼观察和必要的示波器/逻辑分析仪测量
共同确认。

## 故障排查

### 屏幕和背光都不亮

1. 断电确认 J4 Pin 1、镜像方向、GND 和 3.3 V。
2. 上电测量 J4 Pin 2 是否为稳定的 3.3 V。
3. 测量 GPIO16/J4 Pin 3 是否出现约 5 kHz PWM。
4. 查看 USB 串口是否持续输出阶段日志；有日志说明 MCU 程序仍在运行。

### 背光亮但一直白屏

优先检查 CS、DC、RST、SCLK 和 MOSI。用逻辑分析仪应看到：CS 在传输期间拉低，
SCLK 为 Mode 0，MOSI 有数据，复位脚在初始化时产生有效复位过程。

### 红色和蓝色互换

先用 `R G B C M Y` 色条确认交换规律并记录屏幕批次。不要凭单一图片判断。
如果确定面板需要 BGR 顺序，再把颜色顺序作为明确的板级配置修改。

### 图像镜像、旋转或四角位置错误

确认 J4 没有接反。如果只有方向不适合机械安装，修改
`include/board_pins.h` 中的 `kDisplayRotation`，有效值为 0～3，然后重新构建。

### 边框被裁切或整体偏移

确认屏幕实际可视区确实为 240×320，而不是使用 ST7789 的 240×240、240×280
或其他带 RAM 偏移的模组。不要用旋转掩盖面板尺寸或偏移错误。

### 网格抖动、随机花屏或间歇性错误

检查排线长度、接地连续性、接插件压接和 3.3 V 纹波。可暂时把
`kSpiFrequencyHz` 从 20 MHz 降到 10 MHz 进行对比；如果降频后稳定，继续检查
信号完整性，而不是直接把降频当作接线已合格。

## 验收记录

实物验收时至少记录：

- PCB 版本和屏幕采购批次；
- 固件 Git 提交；
- 3.3 V 电压和屏幕工作电流；
- 红、绿、蓝、白、黑是否正确；
- 四角方向和完整外框是否正确；
- 20 MHz 下网格是否持续稳定；
- 背光渐变是否平滑且没有引起复位。
