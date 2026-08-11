# Gridopoly ESP32-S3 固件开发指南

更新日期：2026-07-31

本文给出单格模块固件的开发基线。GPIO 的最终定义来自
[ESP32-S3 引脚与接口定义](../hardware/esp32-s3-pin-map.md)，通信和物理顺序来自
[模块互连方案](../hardware/module-interconnect-5wire.md)。

## 1. 推荐工作流

- 框架：ESP-IDF。
- 芯片目标：`esp32s3`。
- 构建：CMake + `idf.py`。
- 调试/日志：USB Serial/JTAG，GPIO19=D-、GPIO20=D+。
- 单元测试：ESP-IDF Unity；协议编码、CRC、RFID 解码在主机侧增加纯 C/C++
  测试。
- 版本管理：固定可复现的 ESP-IDF 版本和 `sdkconfig.defaults`，不要让开发机各自
  使用未记录的 IDF 版本。

初始化工程：

```powershell
idf.py create-project gridopoly-tile
cd gridopoly-tile
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py flash monitor
```

建议启用 USB Serial/JTAG 控制台。GPIO43/44 虽然网络名带 `TXD0/RXD0`，
固件可通过 GPIO Matrix 把它们分配给 `UART_NUM_1`，避免与控制台职责混淆。

N16R8 表示 16MB Flash 和 8MB Octal PSRAM。`sdkconfig.defaults` 应固定
16MB Flash 和 Octal PSRAM 配置；大块屏幕缓冲优先放 PSRAM。分区表需为 OTA
和资源缓存预留空间，不要按常见 4MB 模组默认值构建。

## 2. 建议目录

```text
firmware/
  CMakeLists.txt
  sdkconfig.defaults
  main/
    app_main.c
    board_pins.h
    board_config.h
  components/
    tile_protocol/
    tile_rs485/
    tile_order/
    tile_display/
    tile_led/
    tile_rfid/
    tile_power_monitor/
    tile_self_test/
  test/
```

业务代码不得直接出现裸 GPIO 数字；所有硬件定义放入 `board_pins.h`。

## 3. 板级 GPIO

```c
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

#define TILE_LED_COUNT      10
#define LCD_WIDTH          240
#define LCD_HEIGHT         320
```

GPIO35、36、37 被 N16R8 的 Octal PSRAM占用，禁止分配。GPIO3、45、46
涉及启动配置，除非重新完成启动模式审查，否则不要使用。

## 4. 上电安全状态

在启动外设任务前先设置以下状态：

| 功能 | 启动状态 | 原因 |
| --- | --- | --- |
| `RS485_DIR` GPIO14 | 低 | DE=0、/RE=0，默认接收且不占用总线 |
| `ORDER_OUT` GPIO10 | 低 | Q1 截止，开漏输出释放 |
| `LCD_BL_PWM` GPIO16 | 低 | 上电先关闭背光，降低浪涌 |
| `LCD_CS` GPIO15 | 高 | 未初始化时不选中屏幕 |
| `LCD_SCLK/MOSI` | 低 | 减少无意义边沿 |
| `LED_DATA` GPIO21 | 低 | 防止 WS2812 随机锁存 |
| `RFID_SCLK/DIN` | 低 | HTRC110 初始化前保持稳定 |

ORDER 为低有效令牌：GPIO10 输出高会打开 Q1，把下一块板的 `ORDER_IN`
拉低；输出低会释放 ORDER 链。上电、复位和异常退出时都必须释放。

## 5. 推荐启动顺序

1. 建立 USB 日志，输出固件版本、硬件版本、复位原因和 eFuse MAC。
2. 设置第 4 节的安全 GPIO 状态。
3. 初始化 NVS，读取运行地址、校准参数和故障计数。
4. 初始化 INA226，确认 `3V3_SYS` 和 I2C 正常。
5. 初始化 RS485 UART，保持接收状态。
6. 初始化 ORDER 输入滤波和状态机，但不立即向下游传令牌。
7. 初始化屏幕，保持低背光，完成后再渐亮。
8. 初始化 RMT 和 10 颗 WS2812，先发送全黑帧。
9. 初始化 HTRC110、4MHz 时钟接口和解码任务。
10. 执行自检并上报 `BOOT_REPORT`；只有自检通过才接受高功耗命令。

任一外设失败不应让看门狗反复重启整板。记录故障并进入降级模式，例如屏幕
失败时仍保留 RS485、ORDER 和电流上报。

## 6. 外设驱动约束

### 6.1 ST7789

- 240×320、只写 SPI，无 MISO。
- 从 SPI mode 0、20MHz 开始验证；稳定后可提高到 40MHz，再根据屏幕模块
  和实际波形决定是否继续提高。
- 使用 DMA 分块发送，避免一次申请完整帧缓冲。
- 原始 RGB565 全屏为 `240 × 320 × 2 = 153600` 字节。
- 背光必须使用 PWM 和功耗策略控制，不要把 GPIO16 当作普通常高电源。

### 6.2 WS2812

- 使用 RMT 外设生成时序，不要用普通任务延时 bit-bang。
- 物理顺序为 U12、U13、U14、U15、U16、U17、U18、U19、U20、U21。
- 常见颜色字节顺序为 GRB，首板必须用红、绿、蓝三帧确认当前器件实际顺序。
- 10 颗 LED 全白按每颗最坏约 60mA估算，阵列可能接近 600mA。
- 默认全局亮度建议不高于 25%，收到电源能力和温升验证结果后再放宽。
- 复位或通信超时进入全黑或低亮故障色，不能保持未知高功耗状态。

### 6.3 INA226

当前地址为 `0x40`，分流电阻为 10mΩ，正电流方向：

```text
5V_SELECTED -> R31 -> 5V_IN
```

推荐首版标定：

```text
Current_LSB = 100uA/bit
Calibration = 0.00512 / (0.0001 × 0.01) = 5120 = 0x1400
Power_LSB = 25 × Current_LSB = 2.5mW/bit
```

该设置的满量程约为 3.2767A。未写 Calibration 寄存器时，INA226 的 Current
和 Power 寄存器会保持为零，不能把零误判为模块没有耗电。

启动后至少读取：

- 制造商 ID 和芯片 ID，确认总线对象确实是 INA226。
- Bus Voltage、Shunt Voltage、Current、Power。
- 连续平均值和峰值；不要只上传单次读数。

### 6.4 RS485

- 推荐使用 `UART_NUM_1`，TX=43、RX=44。
- 初始波特率 115200；多板稳定后使用 921600。
- 2Mbps、5Mbps 仅作为完成终端和波形验证后的可选档位。
- 发送前先置 GPIO14 高，等待收发器使能稳定，再写 UART。
- `uart_wait_tx_done()` 成功后再置 GPIO14 低，返回接收状态。
- 任意复位、异常和看门狗路径都应让 GPIO14 回到低。
- R15 默认不装。只有物理总线两端安装 120Ω终端。

### 6.5 ORDER

输入 GPIO9 带 10kΩ上拉和 100nF 滤波，低电平表示收到上游令牌。建议：

- 连续读取为低至少 2～5ms 后确认令牌。
- 一个模块只在“未分配且持有令牌”时响应 `ENUM_NEXT`。
- 地址写入 NVS 前先完成主控确认，避免掉电留下半枚举状态。
- 传递令牌时 GPIO10 置高；收到下游确认或超时后置低释放。
- 枚举超时必须可恢复，不能永久把 ORDER_OUT 拉低。

### 6.6 HTRC110 与 125kHz 标签

- HTRC110 是三线串行控制接口，不应直接套用标准 SPI 驱动。
- 命令、采样和时序按 HTRC110 数据手册实现。
- HTRC110 输出的是原始解调数据；固件还要实现 EM4100/TK4100
  Manchester 解码、帧头、行/列奇偶校验和固定 ID 提取。
- 先以“单标签进入线圈”为设计前提。EM4100/TK4100 没有防冲突协议，
  多个棋子同时放在同一格时通常会碰撞、无有效帧或读数不稳定。
- 连续收到至少 2～3 个相同且校验正确的帧后再上报 `TAG_PRESENT`。
- 标签离开应使用超时去抖，避免边缘位置反复出现/消失。
- 谐振电容和天线电感属于硬件标定项，固件不能修复严重失谐。

## 7. 功耗模式

定义至少三档：

| 模式 | LED | 背光 | RFID | 用途 |
| --- | --- | --- | --- | --- |
| `SAFE_USB` | 全黑或极低亮 | 低 | 间歇 | 普通电脑 USB 调试 |
| `NORMAL_24V` | 受全局亮度限制 | 正常 | 连续 | 24V 母线运行 |
| `FAULT` | 全黑/故障色 | 低 | 关闭 | 过流、欠压、过温或通信失联 |

硬件没有检测 Type-C 电流宣告，不能仅凭“USB 已连接”判断可用 3A。USB 供电
默认进入 `SAFE_USB`，除非测试人员明确配置为合格的 5V/3A适配器。
`PWRMUX_STATUS` 也没有接到 ESP32，因此模式不能依赖 TPS2121 状态脚自动切换；
首版使用 NVS 配置、主控命令或明确的测试模式选择功耗档位。

## 8. 模块身份与配置

- 永久唯一 ID：ESP32-S3 eFuse 默认 MAC。
- 运行地址：每次 ORDER 枚举由树莓派分配，建议 16 位。
- `order_index`：物理顺序号，从 0 或 1 开始必须在协议中固定。
- NVS 保存：硬件版本、亮度上限、INA226 校准、RFID 参数和最后故障。
- 不要把上一次运行地址当成当前物理顺序；重新拼接棋盘后必须重新枚举。

## 9. RS485 协议 V0

当前建议冻结为下列二进制帧，所有多字节整数使用小端：

```text
SOF0 SOF1 VERSION FLAGS DST SRC SEQ CMD LENGTH PAYLOAD CRC16
 1    1      1      1    2   2   2   1     2     N      2
```

| 字段 | 值/含义 |
| --- | --- |
| SOF | `0x47 0x50`，ASCII `GP` |
| VERSION | `0x00` |
| FLAGS | ACK 请求、ACK、错误、广播 |
| DST/SRC | 16 位运行地址，`0xFFFF` 为广播 |
| SEQ | 请求/响应关联和去重 |
| CMD | 命令码 |
| LENGTH | Payload 字节数，V0 建议不超过 1024 |
| CRC16 | CRC-16/CCITT-FALSE，覆盖 VERSION 到 PAYLOAD |

最低命令集：

| 命令 | 方向 | 作用 |
| --- | --- | --- |
| `PING` | 双向 | 在线与延迟检测 |
| `ENUM_NEXT` | Pi -> 广播 | 请求当前令牌模块认领 |
| `CLAIM` | 模块 -> Pi | 上报 MAC、能力和硬件版本 |
| `ASSIGN_ADDR` | Pi -> 模块 | 分配地址和 `order_index` |
| `PASS_TOKEN` | Pi -> 模块 | 允许向下一级传令牌 |
| `BOOT_REPORT` | 模块 -> Pi | 启动、自检和电源状态 |
| `SET_LED` | Pi -> 模块 | 10 颗 LED 颜色/亮度 |
| `LCD_BEGIN/DATA/END` | Pi -> 模块 | 分块发送屏幕资源 |
| `RFID_EVENT` | 模块 -> Pi | 标签出现、离开或碰撞/无效帧 |
| `CURRENT_REPORT` | 模块 -> Pi | 电压、电流、功率和峰值 |

所有改变持久状态或分块传输的命令都要 ACK、超时、重试和重复包去重。

## 10. 图片传输预算

240×320 RGB565 原始图片为 153600 字节。忽略协议开销时：

| 总线速率 | 单模块理论最短时间 | 40 模块理论最短时间 |
| ---: | ---: | ---: |
| 921600bps | 1.33s | 53.3s |
| 2Mbps | 0.61s | 24.6s |
| 5Mbps | 0.25s | 9.83s |

实际时间还包括 ACK、帧间隔、重试和总线转向。每局只传一次图片时，可以在
开局阶段顺序加载；优先考虑图片索引、压缩或预置资源，不能把 5Mbps 的理论值
当作已验证性能。

## 11. 自检与日志

`BOOT_REPORT` 至少包含：

- 固件版本、硬件版本、eFuse MAC、复位原因。
- 5V 母线、电流和 INA226 在线状态。
- 屏幕初始化、LED 初始化、RFID 初始化结果。
- RS485 波特率、运行地址、`order_index`。
- 当前功耗模式和故障位。

日志禁止持续打印 RFID 原始边沿或每个 LED 帧；高频数据使用可开关调试级别，
避免日志本身破坏实时性。

## 12. 官方资料

- ESP32-S3 USB Serial/JTAG：
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/usb-serial-jtag-console.html
- INA226：
  https://www.ti.com/lit/ds/symlink/ina226.pdf
- SN65HVD75：
  https://www.ti.com/lit/ds/symlink/sn65hvd75.pdf
- HTRC110：
  https://www.nxp.com/docs/en/data-sheet/037031.pdf
