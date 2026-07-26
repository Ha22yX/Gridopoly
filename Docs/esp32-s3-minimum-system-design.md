# Gridopoly ESP32-S3 最小系统原理图设计说明

版本：V1.0

日期：2026-07-27

范围：仅设计 ESP32-S3 最小系统原理图，不设计 PCB，不连接显示屏、RFID、LED、RS485 或 GPIO 扩展排针。

## 1. 设计目标

本原理图用于建立一个可独立启动、可通过 USB-C 下载固件并读取串口日志的高配置 ESP32-S3 核心电路，作为 Gridopoly 后续各功能模块的基础。

具体目标如下：

1. 使用高配置 ESP32-S3 模组。
2. 使用外部统一的 3.3V 电源，不在本板上设置稳压或升降压电路。
3. 增加较大的储能电容，缓解 Wi-Fi 发射、电源线较长或其他模块切换时产生的短时电压跌落。
4. USB-C 同时承担固件下载、USB Serial/JTAG 和串口日志功能。
5. 保留 RESET 和 BOOT 按键，确保在 USB 自动下载异常时仍可手动进入下载模式。
6. 当前不设置 GPIO 排针；所有未使用 GPIO 暂时保持未连接，为后续显示屏、RFID、LED、通信和顺序检测电路预留。
7. 原理图必须通过网络级检查，不能仅凭视觉判断导线是否连接。

## 2. 主控型号

主控选择：

`ESP32-S3-WROOM-2-N32R16V`

嘉立创器件信息：

- 嘉立创器件编号：`C42417293`
- 嘉立创系统库器件 UUID：`4ad078707dc648a9b80e3e3ab72372e9`
- 符号 UUID：`adf37244bf304353afa1c8542056cfc1`
- 封装 UUID：`9dcf44b405774f37803e1e3f15c4f02f`

主要配置：

- ESP32-S3 双核 Xtensa LX7，最高 240MHz。
- 32MB Octal SPI Flash。
- 16MB Octal SPI PSRAM。
- 2.4GHz Wi-Fi 和 Bluetooth 5 LE。
- 原生 USB 2.0 Full-Speed、USB Serial/JTAG。
- 模组内置 40MHz 晶振、射频匹配、PCB 天线、Flash 和 PSRAM。
- 工作电压范围为 3.0V 至 3.6V。
- 模组提供 33 个 GPIO；本型号的内部 Flash/PSRAM 已占用部分芯片管脚，这些内部管脚不能作为外部 GPIO 使用。

官方资料：

- [ESP32-S3-WROOM-2 技术规格书](https://documentation.espressif.com/esp32-s3-wroom-2_datasheet_cn.html)
- [ESP32-S3 硬件设计指南](https://docs.espressif.com/projects/esp-hardware-design-guidelines/zh_CN/latest/esp32s3/schematic-checklist.html)

## 3. 总体电路结构

原理图划分为五个功能区：

1. 3.3V 输入与滤波。
2. ESP32-S3-WROOM-2-N32R16V 主控模组。
3. EN 上电延时与手动复位。
4. GPIO0 启动配置与手动下载。
5. USB-C、USB 数据保护及 USB Serial/JTAG。

原理图使用以下主要网络名称：

| 网络名称 | 用途 |
| --- | --- |
| `+3V3_SYS` | 外部统一 3.3V 电源 |
| `GND` | 公共地 |
| `ESP_EN` | ESP32-S3 使能与复位 |
| `BOOT_GPIO0` | GPIO0 启动配置 |
| `USB_DN_CONN` | Type-C 接口侧 USB D- |
| `USB_DP_CONN` | Type-C 接口侧 USB D+ |
| `USB_DN_MCU` | ESP32-S3 侧 USB D- |
| `USB_DP_MCU` | ESP32-S3 侧 USB D+ |
| `USB_CC1` | Type-C CC1 |
| `USB_CC2` | Type-C CC2 |
| `USB_VBUS_5V` | Type-C 提供的 5V，仅隔离保留，不接入 3.3V |
| `USB_SHIELD` | Type-C 金属外壳 |

## 4. 3.3V 输入与滤波

### 4.1 供电要求

本原理图不包含 LDO、DCDC 或其他电源转换电路。外部系统必须直接提供稳定的 3.3V。

外部 3.3V 电源要求：

- 正常工作电压：3.3V。
- 允许范围：不得超出模组规定的 3.0V 至 3.6V。
- 单个 ESP32-S3 模组可用电流不得低于 500mA。
- 考虑 Wi-Fi 峰值、线路压降和后续外设，原型测试电源建议为每个模组预留至少 800mA，系统总电源按模块数量重新计算。
- 3.3V 和 GND 必须使用低阻抗连接，不能使用过细或过长的杜邦线承担高峰值电流。

### 4.2 电容配置

`+3V3_SYS` 与 `GND` 之间并联以下电容：

| 位号 | 推荐值 | 类型与耐压 | 作用 |
| --- | --- | --- | --- |
| C1 | 470uF | 低 ESR 电解或固态电容，6.3V 或更高 | 大容量储能，缓解短时供电跌落 |
| C2 | 10uF | X5R/X7R 陶瓷，6.3V 或更高 | 中低频去耦 |
| C3 | 1uF | X7R 陶瓷，10V，0603/0805 | 中高频去耦 |
| C4 | 100nF | X7R 陶瓷，10V，0603 | 高频去耦，靠近模组 3V3 引脚 |

C1 和 C2 放在外部 3.3V 进入本模块的位置。C3 和 C4 放在 ESP32-S3 模组 3V3 引脚附近。

470uF 只能缓解毫秒级瞬态压降，不能修复长期低压、供电限流或过细导线造成的持续电压下降。未来大量模块并联时，还必须评估总电容产生的上电浪涌电流；必要时将单模块 C1 调整为 100uF 至 220uF，或增加分区上电和限流措施。

### 4.3 Type-C VBUS 隔离

Type-C 的 VBUS 是 5V，严禁直接连接 `+3V3_SYS`。

本版本规定：

- Type-C 仅用于 USB 数据。
- ESP32-S3 必须由外部 `+3V3_SYS` 供电后，USB 才能工作。
- Type-C 所有 VBUS 引脚连接到独立网络 `USB_VBUS_5V`。
- `USB_VBUS_5V` 不连接到 ESP32-S3 的 3V3 引脚。
- 原理图在 `USB_VBUS_5V` 网络旁放置醒目的“禁止连接 3.3V”说明。

## 5. EN 上电与复位电路

ESP32-S3 的 EN 引脚为高电平时运行，拉低时复位。

连接方式：

| 位号 | 数值/型号 | 连接 |
| --- | --- | --- |
| R1 | 10kΩ | `+3V3_SYS` 到 `ESP_EN` |
| C5 | 1uF，X7R | `ESP_EN` 到 `GND` |
| SW1 | 常开轻触按键 | `ESP_EN` 到 `GND` |

`ESP_EN` 连接 U1 的 EN 引脚。

设计目的：

- R1 为 EN 提供稳定上拉。
- C5 与 R1 形成上电延时，给 3.3V 电源留出稳定时间。
- SW1 按下时将 EN 拉低，实现手动复位。

EN 网络应保持短且远离高速数据线。不能在 EN 上使用过大的电容，否则复位释放时间过长，可能影响下载和启动。

## 6. BOOT 下载模式电路

GPIO0 是 ESP32-S3 的启动配置管脚。正常启动时保持高电平；复位期间将 GPIO0 拉低可进入下载模式。

连接方式：

| 位号 | 数值/型号 | 连接 |
| --- | --- | --- |
| R2 | 10kΩ | `+3V3_SYS` 到 `BOOT_GPIO0` |
| SW2 | 常开轻触按键 | `BOOT_GPIO0` 到 `GND` |

`BOOT_GPIO0` 连接 U1 的 GPIO0。

手动进入下载模式的操作顺序：

1. 按住 BOOT。
2. 按下并松开 RESET。
3. 松开 BOOT。
4. 电脑通过 USB Serial/JTAG 下载固件。

GPIO0 不配置大电容，避免上电时被错误保持为低电平。

## 7. USB-C 与原生 USB Serial/JTAG

### 7.1 方案选择

本版本使用 ESP32-S3 内置 USB Serial/JTAG，不增加 CH340、CP2102 或其他 USB-UART 芯片。

该方案可实现：

- 通过 USB-C 下载 ESP-IDF 或 Arduino 固件。
- 读取 USB 串口日志。
- 使用内置 USB JTAG 调试。
- 减少芯片数量、成本、占板面积和额外供电负载。

### 7.2 Type-C 接口

J1 使用 USB Type-C USB 2.0 Device 接口。

连接要求：

| Type-C 信号 | 连接 |
| --- | --- |
| A6、B6 | 合并为 `USB_DP_CONN` |
| A7、B7 | 合并为 `USB_DN_CONN` |
| A5 CC1 | 通过 R5 连接 GND |
| B5 CC2 | 通过 R6 连接 GND |
| VBUS | 合并为 `USB_VBUS_5V`，不接 3.3V |
| GND | 连接 `GND` |
| SBU1、SBU2 | 添加 No Connect 标记 |
| Shield | 连接 `USB_SHIELD` |

R5、R6 均为 5.1kΩ，表示本设备是 USB Type-C 受电设备端。即使本版本不使用 VBUS 给系统供电，也必须保留 CC 下拉电阻，确保主机正确识别接口方向和设备连接。

### 7.3 USB 数据线

ESP32-S3 USB 管脚：

- GPIO19：USB D-。
- GPIO20：USB D+。

连接方式：

| 位号 | 数值 | 连接 |
| --- | --- | --- |
| R3 | 22Ω | `USB_DN_CONN` 到 `USB_DN_MCU` |
| R4 | 22Ω | `USB_DP_CONN` 到 `USB_DP_MCU` |
| D1 | USBLC6-2SC6，嘉立创 `C7519` | 保护接口侧 D+、D- |
| C6 | DNP，预留 5pF 至 10pF | `USB_DN_MCU` 到 GND |
| C7 | DNP，预留 5pF 至 10pF | `USB_DP_MCU` 到 GND |

`USB_DN_MCU` 连接 U1 的 GPIO19。

`USB_DP_MCU` 连接 U1 的 GPIO20。

R3、R4 在 PCB 阶段应靠近 ESP32-S3 模组。D1 应靠近 Type-C 接口。D1 的 VBUS 钳位脚连接隔离网络 `USB_VBUS_5V`，不能连接 `+3V3_SYS`。C6、C7 默认不装，仅在信号完整性或 EMC 测试需要时使用。

### 7.4 Type-C 外壳

Type-C 金属外壳使用独立网络 `USB_SHIELD`，并预留以下连接：

| 位号 | 数值 | 连接 |
| --- | --- | --- |
| R7 | 1MΩ | `USB_SHIELD` 到 `GND` |
| C8 | 4.7nF | `USB_SHIELD` 到 `GND` |
| R8 | 0Ω，DNP | `USB_SHIELD` 到 `GND`，供调试选择 |

默认装配 R7、C8，不装 R8。后续根据 EMC、机壳和接地测试决定是否改为直接接地。

## 8. GPIO 当前处理方式

本版本不设置 GPIO 排针，也不把 GPIO 接到其他功能模块。

处理原则：

1. GPIO19 和 GPIO20 分别连接 USB D- 和 USB D+。
2. GPIO0 连接 BOOT 电路，正常启动后仍可由软件作为 GPIO 使用，但外部电路必须避免影响启动电平。
3. 其他由模组符号提供且未使用的 GPIO 暂时保持未连接。
4. 在 EasyEDA 中对暂未使用的 GPIO 添加 No Connect 标记，表示这是经过确认的暂时未使用状态，而不是漏画。
5. GPIO3、GPIO45、GPIO46 等启动配置相关管脚旁增加注释，后续扩展时必须重新检查其上电电平。
6. GPIO43、GPIO44 是默认 UART0 TX/RX，本版本不连接 USB-UART 芯片，暂时保留未使用。
7. 后续增加显示屏、RFID、LED、RS485 和 TOKEN 电路时，再移除相应 GPIO 的 No Connect 标记并分配网络。

## 9. 主要器件清单

| 位号 | 器件 | 推荐参数 |
| --- | --- | --- |
| U1 | ESP32-S3-WROOM-2-N32R16V | C42417293 |
| D1 | USBLC6-2SC6 | ST，嘉立创 `C7519`，SOT-23-6 |
| J1 | TYPE-C-31-M-12 | 韩国韩荣，16Pin，嘉立创 `C165948` |
| SW1 | RESET 按键 | 常开轻触按键 |
| SW2 | BOOT 按键 | 常开轻触按键 |
| R1 | EN 上拉 | 10kΩ |
| R2 | GPIO0 上拉 | 10kΩ |
| R3、R4 | USB 串联电阻 | 22Ω |
| R5、R6 | Type-C CC 下拉 | 5.1kΩ，1% |
| R7 | Shield 泄放电阻 | 1MΩ |
| R8 | Shield 直连选择 | 0Ω，DNP |
| C1 | 输入储能 | 470uF，低 ESR，≥6.3V |
| C2 | 电源去耦 | 10uF，X5R/X7R |
| C3 | 电源去耦 | 1uF，X7R |
| C4 | 电源去耦 | 100nF，X7R |
| C5 | EN 延时 | 1uF，X7R |
| C6、C7 | USB 调试电容 | 5pF 至 10pF，DNP |
| C8 | Shield 高频连接 | 4.7nF |

嘉立创库定位信息：

| 器件 | 器件 UUID | 符号 UUID | 封装 UUID |
| --- | --- | --- | --- |
| U1 ESP32-S3-WROOM-2-N32R16V | `4ad078707dc648a9b80e3e3ab72372e9` | `adf37244bf304353afa1c8542056cfc1` | `9dcf44b405774f37803e1e3f15c4f02f` |
| J1 TYPE-C-31-M-12 | `74d31c19993b4b9581f3175a7da4b280` | `8de51be61d8845edaaae6c1109a76c83` | `6be549a13f5d4c07a7d9efa614d32ed9` |
| D1 USBLC6-2SC6 | `41567551402d47e3a0236ab3d895b322` | `0f94f580b82e472f955382ed98bcde1a` | `3c60dc918974420eb4105dc403eb98d3` |

## 10. 精确网络连接表

| 网络 | 必须连接的端点 |
| --- | --- |
| `+3V3_SYS` | U1 3V3、R1.1、R2.1、C1.1、C2.1、C3.1、C4.1 |
| `GND` | U1 所有 GND/EPAD、C1.2、C2.2、C3.2、C4.2、C5.2、SW1.2、SW2.2、R5.2、R6.2、D1 GND、J1 GND |
| `ESP_EN` | U1 EN、R1.2、C5.1、SW1.1 |
| `BOOT_GPIO0` | U1 GPIO0、R2.2、SW2.1 |
| `USB_DN_CONN` | J1 A7、J1 B7、D1 D- 通道、R3.1 |
| `USB_DN_MCU` | R3.2、U1 GPIO19、C6.1 |
| `USB_DP_CONN` | J1 A6、J1 B6、D1 D+ 通道、R4.1 |
| `USB_DP_MCU` | R4.2、U1 GPIO20、C7.1 |
| `USB_CC1` | J1 CC1、R5.1 |
| `USB_CC2` | J1 CC2、R6.1 |
| `USB_VBUS_5V` | J1 所有 VBUS 引脚、D1 VBUS 钳位脚，仅形成隔离网络 |
| `USB_SHIELD` | J1 所有 Shield、R7.1、C8.1、R8.1 |

R7.2、C8.2、R8.2 连接 `GND`。C6.2、C7.2 连接 `GND`，但 C6、C7 默认不装。

## 11. EasyEDA 绘图规则

为了避免导线假连接、网络误合并和交叉短路，绘图必须遵守以下规则：

1. 原理图按功能块从左到右排列：电源、ESP32-S3、复位/启动、USB-C。
2. 电源和地优先使用明确的电源端口与网络标签，避免跨页面长导线。
3. USB D+、D- 使用短而平行的局部导线，不与其他网络交叉。
4. 不允许不同网络的导线共用同一坐标点。
5. 每次只创建一个网络；创建后立即读取该导线的网络名称和坐标。
6. 每个端点必须与器件引脚的实际连接坐标完全一致。
7. 完成一个网络后，检查该网络是否包含全部预期端点，且没有多余端点。
8. 完成全部网络后再保存原理图。
9. 不能通过文字标签靠近导线的方式假装连接；网络标签必须真正落在导线上。
10. 未使用 GPIO 必须添加 No Connect 标记，不能用悬空短线代替。

## 12. 验证标准

原理图完成后必须执行以下检查：

1. 当前活动文档确认为原理图页。
2. U1 型号确认为 `ESP32-S3-WROOM-2-N32R16V`。
3. U1、J1、D1、SW1、SW2 和所有阻容器件位号唯一。
4. `+3V3_SYS` 与 `USB_VBUS_5V` 是两个独立网络，绝不能合并。
5. GPIO19 仅连接 `USB_DN_MCU`。
6. GPIO20 仅连接 `USB_DP_MCU`。
7. EN 网络包含 U1 EN、10kΩ 上拉、1uF 电容和 RESET 按键。
8. GPIO0 网络包含 U1 GPIO0、10kΩ 上拉和 BOOT 按键。
9. CC1、CC2 分别通过独立的 5.1kΩ 电阻接地。
10. 所有 GND 和模组 EPAD 接地。
11. 未使用 GPIO 具有 No Connect 标记。
12. J1 的 SBU1、SBU2 具有 No Connect 标记。
13. 读取 EasyEDA 中全部导线，确认不存在把多个不同网络合并为一条导线的情况。
14. 放大检查所有引脚端点，确认导线端点与引脚连接点重合。
15. 如 EasyEDA 提供 ERC，运行 ERC 并逐条处理错误；不接受未解释的短路或未连接电源错误。

## 13. 本版本不包含的内容

以下内容不在当前原理图范围内：

- 3.3V 稳压、DCDC 或 LDO。
- USB VBUS 给系统供电。
- USB-UART 桥接芯片。
- GPIO 排针或扩展连接器。
- TFT 显示屏。
- RFID 读卡器及天线。
- 可编程 LED。
- RS485 收发器。
- TOKEN 顺序检测。
- SD 卡、音频、摄像头或其他 ESP32-S3 外设。
- PCB 布局、布线和 Gerber。

## 14. 后续扩展原则

后续增加其他模块时，以本原理图为主控基础页：

1. 移除被选中 GPIO 的 No Connect 标记。
2. 建立独立功能页或清晰的功能区。
3. 为每个新模块记录供电、电平、接口速度和占用 GPIO。
4. 重新检查 GPIO0、GPIO3、GPIO45、GPIO46 等启动配置管脚。
5. GPIO19、GPIO20 在使用 USB 时不分配给其他外设。
6. 每增加一个功能块就执行一次网络连通检查，避免最后统一排查。
