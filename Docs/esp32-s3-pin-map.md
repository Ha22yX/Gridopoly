# Gridopoly 单格模块引脚与接口定义

更新日期：2026-07-29

本文档记录当前嘉立创EDA原理图同步到 PCB 后的实际网络。引脚和连接器定义已
通过嘉立创EDA API读取 PCB 焊盘网络复核，不再沿用旧版 12V、3×3 触点或
“LED 尚未接入”的说明。

## 1. 当前硬件快照

- 主控：`ESP32-S3-WROOM-1-N16R8`，LCSC `C2913202`
- PCB 器件：144
- 加入 BOM 的器件：136
- 加入 BOM 但缺少 MPN/LCSC：0
- TP1～TP5：保留在 PCB，已取消加入 BOM
- 模块母线：`24V_BUS`
- 模块互连：5 个电气网络、每侧 15 个物理触点
- 屏幕接口：`J4`，2×4，公母连接后采用镜像针序
- LED：10 颗 `WS2812B-B-V6`，由 GPIO21 独立寻址
- 本地 24V 转 5V：`LMR16030PDDAR`

这份快照用于固件和接口开发，不等同于“PCB 已可直接投产”。投产前仍需完成
DRC、未布线检查、阻抗/载流复核、开关电源布局检查和实物 RFID 调谐。

## 2. 电源域

| 网络 | 含义 |
| --- | --- |
| `24V_BUS` | 模块间直通的 24V 母线 |
| `24V_BUCK_IN` | U24 自恢复保险丝后的本地降压输入 |
| `5V_FROM_24` | U23 降压后的原始 5V |
| `5V_FROM_24_FUSED` | U8 自恢复保险丝后的本地 5V 支路 |
| `USB_VBUS` | USB Type-C 输入的 5V |
| `USB_5V_FUSED` | U7 自恢复保险丝后的 USB 5V 支路 |
| `5V_SELECTED` | TPS2121 输出、R31 分流电阻之前 |
| `5V_IN` | R31 之后的本模块主 5V |
| `5V_RFID` | FB1 滤波后的 RFID 5V |
| `3V3_SYS` | AP63203 输出的系统 3.3V |
| `GND` | 全板公共地 |

完整供电路径：

```text
24V_BUS
   |
  U24 2920L150/60GR
   |
24V_BUCK_IN
   |
  U23 LMR16030PDDAR
   |
  L2 8.2uH
   |
5V_FROM_24
   |
  U8 2016L300/16MR
   |
5V_FROM_24_FUSED -----> U9 TPS2121 IN1

USB_VBUS
   |
  U7 2016L300/16MR
   |
USB_5V_FUSED ---------> U9 TPS2121 IN2

U9 OUT -> 5V_SELECTED -> R31 10mΩ -> 5V_IN
                                            |
                                            +-> U2 AP63203 -> 3V3_SYS
                                            +-> FB1 -> 5V_RFID
                                            +-> 屏幕与 WS2812
```

`24V_BUS`在输入、输出连接器之间直通。U24只保护本模块的降压支路，不会切断
下游模块的母线。

## 3. ESP32-S3 物理引脚

N16R8 使用 Octal PSRAM，GPIO35、GPIO36、GPIO37 不可作为普通外设引脚。
ESP32-S3 的 GPIO 均不耐受 5V。

| U3 Pin | 模块引脚 | 当前网络 | 功能 | 状态 |
| ---: | --- | --- | --- | --- |
| 1 | GND | `GND` | 地 | 已连接 |
| 2 | 3V3 | `3V3_SYS` | 主控供电 | 已连接 |
| 3 | EN | `MCU_EN` | 10kΩ上拉、1µF对地、RESET | 已连接 |
| 4 | GPIO4 | `LCD_RST` | 屏幕复位 | 已连接 |
| 5 | GPIO5 | `LCD_DC` | 屏幕数据/命令 | 已连接 |
| 6 | GPIO6 | `LCD_MOSI` | 屏幕 SPI 数据 | 已连接 |
| 7 | GPIO7 | `LCD_SCLK` | 屏幕 SPI 时钟 | 已连接 |
| 8 | GPIO15 | `LCD_CS` | 屏幕片选 | 已连接 |
| 9 | GPIO16 | `LCD_BL_PWM` | 屏幕背光 PWM | 已连接 |
| 10 | GPIO17 | `ESP_RFID_SCLK` | HTRC110 串行时钟 | 已连接 |
| 11 | GPIO18 | `ESP_RFID_DIN` | 发往 HTRC110 的数据 | 已连接 |
| 12 | GPIO8 | `ESP_RFID_DOUT` | 来自 HTRC110 的数据 | 已连接 |
| 13 | GPIO19 | `USB_DM_MCU` | USB D- | 已连接 |
| 14 | GPIO20 | `USB_DP_MCU` | USB D+ | 已连接 |
| 15 | GPIO3 | - | 启动配置相关 | 未连接，谨慎使用 |
| 16 | GPIO46 | - | 启动配置、仅输入 | 未连接，谨慎使用 |
| 17 | GPIO9 | `ESP_ORDER_IN` | 顺序检测输入 | 已连接 |
| 18 | GPIO10 | `ESP_ORDER_OUT` | 顺序开漏输出控制 | 已连接 |
| 19 | GPIO11 | `CURRENT_SCL` | INA226 I2C SCL | 已连接 |
| 20 | GPIO12 | `CURRENT_SDA` | INA226 I2C SDA | 已连接 |
| 21 | GPIO13 | - | 扩展 | 未连接 |
| 22 | GPIO14 | `RS485_DIR` | RS485 DE 与 /RE | 已连接 |
| 23 | GPIO21 | `ESP_LED_DATA_3V3` | WS2812 数据 | 已连接 |
| 24 | GPIO47 | - | 扩展 | 未连接 |
| 25 | GPIO48 | - | 扩展 | 未连接 |
| 26 | GPIO45 | - | 启动配置相关 | 未连接，谨慎使用 |
| 27 | GPIO0 | `MCU_BOOT` | BOOT 按键/下载模式 | 已连接 |
| 28 | GPIO35 | 内部 Octal PSRAM | 不可外用 | 禁止使用 |
| 29 | GPIO36 | 内部 Octal PSRAM | 不可外用 | 禁止使用 |
| 30 | GPIO37 | 内部 Octal PSRAM | 不可外用 | 禁止使用 |
| 31 | GPIO38 | - | 扩展 | 未连接 |
| 32 | GPIO39 | - | 扩展/JTAG | 未连接 |
| 33 | GPIO40 | - | 扩展/JTAG | 未连接 |
| 34 | GPIO41 | - | 扩展/JTAG | 未连接 |
| 35 | GPIO42 | - | 扩展/JTAG | 未连接 |
| 36 | GPIO44/RXD0 | `U3_RXD0` | RS485 UART 接收 | 已连接 |
| 37 | GPIO43/TXD0 | `U3_TXD0` | RS485 UART 发送 | 已连接 |
| 38 | GPIO2 | - | 扩展 | 未连接 |
| 39 | GPIO1 | - | 扩展 | 未连接 |
| 40 | GND | `GND` | 地 | 已连接 |
| 41 | EPAD/GND | `GND` | 模组地/散热焊盘 | 已连接 |

旧文档曾将 INA226 写成 GPIO12/13；当前正确值为 GPIO11=SCL、
GPIO12=SDA。GPIO13 当前未连接。

## 4. 固件板级定义

```cpp
#pragma once

#define PIN_LCD_RST          4
#define PIN_LCD_DC           5
#define PIN_LCD_MOSI         6
#define PIN_LCD_SCLK         7
#define PIN_LCD_CS          15
#define PIN_LCD_BL_PWM      16

#define PIN_RFID_SCLK       17
#define PIN_RFID_DIN        18
#define PIN_RFID_DOUT        8

#define PIN_ORDER_IN         9
#define PIN_ORDER_OUT       10

#define PIN_CURRENT_SCL     11
#define PIN_CURRENT_SDA     12
#define INA226_I2C_ADDR   0x40

#define PIN_RS485_DIR       14
#define PIN_RS485_TX        43
#define PIN_RS485_RX        44

#define PIN_LED_DATA        21

#define PIN_USB_DM          19
#define PIN_USB_DP          20
#define PIN_BOOT             0
```

INA226 初始化：

```cpp
Wire.begin(PIN_CURRENT_SDA, PIN_CURRENT_SCL); // SDA=12, SCL=11
```

GPIO 数字只应在一个板级头文件中维护，业务代码不要散落裸数字。

## 5. 屏幕接口 J4

J4 为 `DS1023-2*4SF11`，LCSC `C92271`。PCB 当前焊盘网络如下：

| J4 Pin | 网络 | ESP32-S3 | 屏幕信号 |
| ---: | --- | --- | --- |
| 1 | `GND` | - | GND |
| 2 | `3V3_SYS` | - | 3V3 |
| 3 | `LCD_BL_PWM` | GPIO16 | PWR/BL |
| 4 | `LCD_CS` | GPIO15 | CS |
| 5 | `LCD_SCLK` | GPIO7 | SCL |
| 6 | `LCD_MOSI` | GPIO6 | SDA |
| 7 | `LCD_DC` | GPIO5 | WR/DC |
| 8 | `LCD_RST` | GPIO4 | RST |

该顺序与旧文档中的 J2 顺序相反，是公母连接器面对面插合后的镜像结果。制作
线束、封装或转接板时必须同时核对 Pin 1 标记和观察方向，不能只按视觉左右顺序。

接口没有 MISO，ST7789 当前为只写 SPI，ESP32 不能从屏幕读回像素或寄存器。

## 6. USB Type-C 接口 J1

| J1 引脚组 | 网络 | 连接 |
| --- | --- | --- |
| A4/B9、B4/A9 | `USB_VBUS` | 经 U7 到 TPS2121 IN2 |
| A5 | `USB_CC1` | 5.1kΩ 到 GND |
| B5 | `USB_CC2` | 5.1kΩ 到 GND |
| A6/B6 | `USB_DP` | D1 ESD、22Ω、`USB_DP_MCU`、GPIO20 |
| A7/B7 | `USB_DM` | D1 ESD、22Ω、`USB_DM_MCU`、GPIO19 |
| A8/B8 | - | NC |
| A1/B12、B1/A12 | `GND` | 公共地 |
| SHELL 1～4 | `GND` | 当前直接接公共地 |

D1 为 `TPD2EUSB30DRTR`。USB D+/D- 应按 90Ω 差分阻抗布线，D1 靠近
Type-C，22Ω串联电阻靠近 ESP32。

CC1/CC2 只声明本板为受电设备。当前没有读取上游电源宣告能力，不能默认任意
电脑 USB 口都能持续提供 3A。

## 7. 3×5 磁吸触点

每侧使用 5 个 3Pin 连接器，共 15 个物理触点：

- 6 个并联 `24V_BUS`
- 6 个并联 `GND`
- 1 个 `BUS_A`
- 1 个 `BUS_B`
- 1 个 `ORDER_IN` 或 `ORDER_OUT`

输入侧器件：`YZ10515055F-03025-01`，LCSC `C54799715`。

输出侧器件：`YZ126715035T-03025-01`，LCSC `C54799683`。

### 7.1 输入侧

| 连接器 | Pin 1 | Pin 2 | Pin 3 |
| --- | --- | --- | --- |
| JIN1 | `ORDER_IN` | `BUS_A` | `BUS_B` |
| JIN2 | `24V_BUS` | `24V_BUS` | `24V_BUS` |
| JIN3 | `GND` | `GND` | `GND` |
| JIN4 | `24V_BUS` | `24V_BUS` | `24V_BUS` |
| JIN5 | `GND` | `GND` | `GND` |

### 7.2 输出侧

| 连接器 | Pin 1 | Pin 2 | Pin 3 |
| --- | --- | --- | --- |
| JOUT1 | `BUS_B` | `BUS_A` | `ORDER_OUT` |
| JOUT2 | `24V_BUS` | `24V_BUS` | `24V_BUS` |
| JOUT3 | `GND` | `GND` | `GND` |
| JOUT4 | `24V_BUS` | `24V_BUS` | `24V_BUS` |
| JOUT5 | `GND` | `GND` | `GND` |

JOUT1 是镜像针序，不应改成与 JIN1 相同的数字顺序。实际插合后：

```text
JIN1.1 ORDER_IN  <-> 上一模块 JOUT1.3 ORDER_OUT
JIN1.2 BUS_A     <-> 上一模块 JOUT1.2 BUS_A
JIN1.3 BUS_B     <-> 上一模块 JOUT1.1 BUS_B
```

`24V_BUS`、`GND`、`BUS_A`、`BUS_B`输入输出直通；ORDER 由 ESP32 逐级
接收和传递。

## 8. ORDER 顺序检测

```text
JIN1.1 ORDER_IN -> R16 1kΩ -> ESP_ORDER_IN / GPIO9
                                      |
                                      +-> R17 10kΩ -> 3V3_SYS
                                      +-> C24 100nF -> GND

GPIO10 / ESP_ORDER_OUT -> R18 1kΩ -> Q1 Gate
                                      |
                                      +-> R19 100kΩ -> GND

Q1 Source -> GND
Q1 Drain  -> ORDER_OUT -> JOUT1.3
```

Q1 为 2N7002 开漏输出。下一级通过 R17 上拉，避免相邻模块的 3.3V 电源
被直接并联。

## 9. RS485

U6 为 `SN65HVD75DR`，3.3V 半双工收发器。

| U6 Pin | 名称 | 当前网络 |
| ---: | --- | --- |
| 1 | R | `U3_RXD0` -> GPIO44 |
| 2 | /RE | `RS485_DIR` |
| 3 | DE | `RS485_DIR` |
| 4 | D | `U3_TXD0` <- GPIO43 |
| 5 | GND | `GND` |
| 6 | A | `BUS_A` |
| 7 | B | `BUS_B` |
| 8 | VCC | `3V3_SYS` |

配套电路：

- C23：100nF，VCC 到 GND。
- R14：10kΩ，将 `RS485_DIR` 下拉，上电默认接收。
- R15：120Ω，跨 `BUS_A/B`，DNP，仅物理总线两端装配。
- D4：SM712，保护 `BUS_A/B`，应靠近信号触点。

## 10. 125kHz RFID

### 10.1 数字接口

| 方向 | ESP32 | 电平转换 | HTRC110 |
| --- | --- | --- | --- |
| ESP32 -> HTRC110 | GPIO17 `ESP_RFID_SCLK` | U5 通道1，3.3V转5V | `HTRC_SCLK` |
| ESP32 -> HTRC110 | GPIO18 `ESP_RFID_DIN` | U5 通道2，3.3V转5V | `HTRC_DIN` |
| HTRC110 -> ESP32 | GPIO8 `ESP_RFID_DOUT` | U4，5V转3.3V | `HTRC_DOUT_5V` |

### 10.2 HTRC110 U1

| U1 Pin | 名称 | 当前网络/连接 |
| ---: | --- | --- |
| 1 | VSS | `GND` |
| 2 | TX2 | `U1_TX2`，调谐电容组 |
| 3 | VDD | `5V_RFID` |
| 4 | TX1 | `U1_TX1` -> R9 27Ω/1W -> C13 100nF -> 天线 |
| 5 | MODE | `GND` |
| 6 | XTAL1 | `RFID_CLK_4M` |
| 7 | XTAL2 | NC |
| 8 | SCLK | `HTRC_SCLK` |
| 9 | DIN | `HTRC_DIN` |
| 10 | DOUT | `HTRC_DOUT_5V` |
| 11 | NC | NC |
| 12 | CEXT | `RFID_CEXT`，C7 100nF 到 GND |
| 13 | QGND | `RFID_QGND_FILT`，C8 100nF 到 GND |
| 14 | RX | `U1_RX` -> R8 270kΩ -> `ANT_TAP_HV` |

X1 为 4MHz、5V 有源振荡器，供电接 `5V_RFID`，输出接 `RFID_CLK_4M`。

### 10.3 天线 J3

| J3 Pin | 网络 | 用途 |
| ---: | --- | --- |
| 1 | `ANT_COIL_A` | 外部线圈第一端 |
| 2 | `ANT_TAP_HV` | 外部线圈第二端和 RX 采样点 |

调谐电容接在 `ANT_TAP_HV` 与 `U1_TX2` 之间：

- C14～C17：4 × 1nF
- C18：220pF
- C19、C20：DNP 调试焊盘
- 标称总值：约 4.22nF

4.22nF 在 125kHz 理想对应约 384µH。最终必须使用实际线圈，在屏幕和结构件
安装完成后测量并调谐。固件仍需完成 EM4100/TK4100 Manchester 解码、
奇偶校验和 ID 提取。

## 11. WS2812 LED 阵列

GPIO21 已实际连接，不再是规划项。

```text
GPIO21 / ESP_LED_DATA_3V3
   -> U5 Pin 9 (3A)
   -> U5 Pin 8 (3Y) / LED_DATA_5V_RAW
   -> R38 330Ω
   -> LED_DATA_01
   -> U12 -> U13 -> U14 -> U15 -> U16
   -> U17 -> U18 -> U19 -> U20 -> U21
```

| LED | DIN | DOUT |
| --- | --- | --- |
| U12 | `LED_DATA_01` | `LED_DATA_02` |
| U13 | `LED_DATA_02` | `LED_DATA_03` |
| U14 | `LED_DATA_03` | `LED_DATA_04` |
| U15 | `LED_DATA_04` | `LED_DATA_05` |
| U16 | `LED_DATA_05` | `LED_DATA_06` |
| U17 | `LED_DATA_06` | `LED_DATA_07` |
| U18 | `LED_DATA_07` | `LED_DATA_08` |
| U19 | `LED_DATA_08` | `LED_DATA_09` |
| U20 | `LED_DATA_09` | `LED_DATA_10` |
| U21 | `LED_DATA_10` | NC |

U12～U21 均由 `5V_IN` 供电、接 `GND`，每颗旁边配置一颗 100nF
去耦电容 C42～C51。U22 为 `5V_IN` 上的 470µF/10V 阵列储能电容。

WS2812 按串行级联工作，ESP32 使用一根数据线即可为每颗 LED 设置不同颜色。

## 12. 24V 转本地 5V

U23 为 `LMR16030PDDAR`，LCSC `C90665`。

| U23 Pin | 名称 | 当前网络/连接 |
| ---: | --- | --- |
| 1 | BOOT | `BUCK24_BOOT`，C56 100nF 到 SW |
| 2 | VIN | `24V_BUCK_IN` |
| 3 | EN | `24V_BUCK_IN` |
| 4 | RT/SYNC | `BUCK24_RT`，R39 49.9kΩ 到 GND |
| 5 | FB | `BUCK24_FB` |
| 6 | SS/PGOOD | NC |
| 7 | GND | `GND` |
| 8 | SW | `BUCK24_SW` |
| 9 | EP | `GND` |

关键外围：

| 器件 | 参数 | 作用 |
| --- | --- | --- |
| U24 | 2920L150/60GR，1.5A hold/3A trip | 本模块 24V 支路保护 |
| D6 | SMBJ30A | 24V TVS，Pin1接24V、Pin2接GND |
| C52/C55 | 2 × 2.2µF/100V | 输入陶瓷电容 |
| C53 | 100nF/100V | 输入高频去耦 |
| C54 | 47µF/63V | 输入储能 |
| C56 | 100nF | BOOT-SW |
| D3 | SS510 | 续流二极管，Pin1接SW、Pin2接GND |
| L2 | 8.2µH | 输出电感 |
| C35～C38 | 4 × 22µF/16V | 输出滤波 |
| R34/R35 | 100kΩ/17.8kΩ | 5V 反馈分压 |

## 13. 双路 5V 与电流检测

U9 为 `TPS2121RUXR`：

- IN1：`5V_FROM_24_FUSED`
- IN2：`USB_5V_FUSED`
- OUT：`5V_SELECTED`
- ST：`PWRMUX_STATUS`
- ILM：`PWRMUX_ILIM`
- SS：`PWRMUX_SS`

U10 为 `INA226AIDGSR`，地址 `0x40`：

```text
5V_SELECTED -> R31 10mΩ -> 5V_IN -> 本模块全部负载
```

| U10 Pin | 名称 | 当前网络 |
| ---: | --- | --- |
| 1 | A1 | GND |
| 2 | A0 | GND |
| 3 | ALERT | NC |
| 4 | SDA | `CURRENT_SDA` -> GPIO12 |
| 5 | SCL | `CURRENT_SCL` -> GPIO11 |
| 6 | VS+ | `3V3_SYS` |
| 7 | GND | GND |
| 8 | VBUS | `5V_IN` |
| 9 | VIN- | `5V_IN` |
| 10 | VIN+ | `5V_SELECTED` |

R31 为 10mΩ、2W、1%，不是 10MΩ。PCB 上必须从 R31 两端分别引 Kelvin
采样线到 VIN+ 和 VIN-。

## 14. 5V 转 3.3V

U2 为固定 3.3V 输出 `AP63203WU-7`：

| U2 Pin | 当前连接 |
| ---: | --- |
| 1 FB | `3V3_SYS` |
| 2 EN | `5V_IN` |
| 3 VIN | `5V_IN` |
| 4 GND | `GND` |
| 5 SW | `BUCK_SW` -> L1 4.7µH |
| 6 BST | `BUCK_BST`，C2 100nF 到 SW |

输入为 C1 10µF 和 C33 100nF，输出为 C3/C4 各 22µF。

## 15. 测试点与指示灯

| 位号 | 网络 |
| --- | --- |
| TP1 | `5V_IN` |
| TP2 | `3V3_SYS` |
| TP3 | `GND` |
| TP4 | `5V_RFID` |
| TP5 | `24V_BUCK_IN` |

TP1～TP5 加入 PCB，但不加入 BOM。

D2 与 R7 组成 3.3V 电源指示灯：

```text
3V3_SYS -> D2 -> R7 2.2kΩ -> GND
```

它是电源灯，不是固件运行状态灯。ESP32 软件复位时不会熄灭，只有 3.3V
掉电时才会熄灭。

## 16. 特殊布线网络

| 网络 | 要求 |
| --- | --- |
| `USB_DP`/`USB_DM` | 90Ω差分阻抗，同层、同参考面、少过孔、组内等长 |
| `BUS_A`/`BUS_B` | 差分成对、连续参考地、避免长支线 |
| `BUCK24_SW` | 最小铜区和最短回路，远离 RFID、USB、天线 |
| `BUCK_SW` | 最小铜区和最短回路，远离敏感信号 |
| `ANT_COIL_A`/`ANT_TAP_HV` | 短、远离数字边沿和开关节点，按高交流电压留间距 |
| `U1_TX1`/`U1_TX2`/`U1_RX` | RFID 模拟网络，按天线区集中布局 |
| `5V_SELECTED`/`5V_IN` | 主电流网络，R31使用宽铜与Kelvin采样 |
| `24V_BUS` | 6触点汇流，使用宽铜皮而非普通信号线 |
| `LED_DATA_01`～`LED_DATA_10` | 按物理 LED 顺序短接，避免靠近 RFID RX |

只有 USB D+/D- 需要严格长度匹配。RS485 A/B 应保持相近长度，但不需要 USB
级别的严格等长。SPI 和 WS2812 在本板距离内以短、连续参考地优先。

## 17. 维护规则

1. 原理图变更后，用嘉立创EDA API读取 PCB 焊盘网络再更新本文档。
2. 固件 GPIO 只从本文档和统一板级头文件派生。
3. 新选贴片器件优先要求嘉立创贴装库存不少于 1000。
4. 位号、网络、Footprint、MPN、LCSC 在原理图、PCB、BOM 和文档中保持一致。
5. 屏幕和 JOUT1 必须保留镜像针序说明。
6. DRC 为零不代表电路安全；电源方向、极性、载流、阻抗和实物装配仍需人工复核。
