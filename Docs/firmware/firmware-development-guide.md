# Gridopoly ESP32-S3 固件开发指南

更新日期：2026-09-21

本文给出单格模块固件的开发基线。GPIO 的最终定义来自
[ESP32-S3 引脚与接口定义](../hardware/esp32-s3-pin-map.md)，通信和物理顺序来自
[模块互连方案](../hardware/module-interconnect-5wire.md)。

## 1. 当前实际工程与构建

正式格子工程是 `Firmware/TileModule`，PlatformIO + Arduino，不是新建空白 ESP-IDF 工程。platformio.ini 固定 espressif32@7.0.1、C++17、16MB分区、qio_opi/OPI PSRAM与USB CDC；依赖版本按该文件，不凭开发机默认安装。

```powershell
cd Firmware/TileModule
pio run -e tile_esp32s3
pio test -e native
```

构建不等于烧录。串口身份、设备运行版本及本机忽略配置先核对，实际刷写流程见[工程README](../../Firmware/TileModule/README.md)。协议/模型主机测试不代替屏幕、RFID、ORDER电气或供电验证。

## 2. 目录与板型

板级定义位于 `include/board_config.h`，实现位于 `src/`，本机配置在 `config/`，禁止提交网络凭据。共享业务模型与协议位于 Firmware/libraries。

当前V0.32支持显示、LED、INA226、RFID、HTTP心跳和ORDER物理信标，未启用RS485。长边/角落显式板型和功耗管理尚待实现，见[直接24V板固件要求](module-power-firmware.md)。保留旧板构建，不复制整套业务工程；当前两板均不需要PD驱动，GPIO13为NC。

## 3. 板级 GPIO

以下宏是共同核心的说明示例；实际代码以include/board_config.h为准。当前两板均不得加入PD_RESET=13或STUSB4500=0x28启动依赖；GPIO13未连接，GPIO11/12保留INA226功能。

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
| GPIO13（两板NC） | 不初始化为PD控制 | 当前已取消PD_RESET功能 |
| `RS485_DIR` GPIO14 | 低 | DE=0、/RE=0，默认接收且不占用总线 |
| `ORDER_OUT` GPIO10 | 低 | Q1 截止，开漏输出释放 |
| `LCD_BL_PWM` GPIO16 | 低 | 上电先关闭背光，降低浪涌 |
| `LCD_CS` GPIO15 | 高 | 未初始化时不选中屏幕 |
| `LCD_SCLK/MOSI` | 低 | 减少无意义边沿 |
| `LED_DATA` GPIO21 | 低 | 防止 WS2812 随机锁存 |
| `RFID_SCLK/DIN` | 低 | HTRC110 初始化前保持稳定 |

ORDER 开漏线为低有效：GPIO10 输出高会打开 Q1，把下一块板的 `ORDER_IN`
拉低；输出低会释放 ORDER 链。上电、复位和异常退出时都必须释放。

## 5. 推荐启动顺序

1. 建立 USB 日志，输出固件版本、硬件版本、复位原因和 eFuse MAC。
2. 设置第 4 节的安全 GPIO 状态。
3. 初始化 NVS，读取运行地址、校准参数和故障计数。
4. 初始化共享I²C与INA226；3V3_SYS由外部仪表验证，INA226不直接测3.3V。
5. GPIO14 保持低/接收安全态；当前固件不启动 RS485 数据传输。
6. 初始化 ORDER V1 信标收发状态机，依协议启动；不能继续套用令牌枚举。
7. 初始化屏幕，保持低背光，完成后再渐亮。
8. 初始化 RMT 和 10 颗 WS2812，先发送全黑帧。
9. 初始化 HTRC110、4MHz 时钟接口和解码任务。
10. 执行自检并通过当前HTTP心跳报告状态；后续版本化扩展板型/电源诊断能力，不把历史BOOT_REPORT提案当现有wire。

任一外设失败不应让看门狗反复重启整板。记录故障并进入降级模式，例如屏幕
显示失败不应阻塞HTTP、ORDER和有效电量上报；RS485尚未启用。

## 6. 外设驱动约束

### 6.1 ST7789

- 240×320、只写 SPI，无 MISO。
- 当前默认8MHz，按已验证设备身份才使用本机40MHz配置；不能推广到全部板。
- 当前采用PSRAM合成/提交帧差分与内部RAM行发送；以实际驱动为准，不重复整页清空。
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

### 6.4 RS485（后续实现约束，当前未启用）

- 推荐使用 `UART_NUM_1`，TX=43、RX=44。
- 初始波特率 115200；多板稳定后使用 921600。
- 2Mbps、5Mbps 仅作为完成终端和波形验证后的可选档位。
- 发送前先置 GPIO14 高，等待收发器使能稳定，再写 UART。
- `uart_wait_tx_done()` 成功后再置 GPIO14 低，返回接收状态。
- 任意复位、异常和看门狗路径都应让 GPIO14 回到低。
- R15 默认不装。只有物理总线两端安装 120Ω终端。

### 6.5 ORDER

当前为每板独立物理信标，不是ENUM_NEXT/PASS_TOKEN。GPIO10低释放、高拉低下游；1ms任务按6秒周期发送bootId/seq/CRC，通过HTTP报告接收到的实际上游。

准确脉宽、序号、TTL、卡低和调度异常规则以[ORDER V1](tile-order-protocol.md)为准；服务器当前拒绝闭环，完整环形产品需要先定义断点或新增协议。

### 6.6 HTRC110 与 HITAG S256

- HTRC110 是三线串行控制接口，不应直接套用标准 SPI 驱动。
- 命令、采样和时序按 HTRC110 数据手册实现。
- 当前标签为 HITAG S256；固件发送 Advanced UID REQUEST `11001`，解码
  `SOF 111 + UID0..UID3` 的 AC2K 2kbit/s 响应，并按 `UID3..UID0` 显示 8 位十六进制 UID。
- 发生碰撞时，V0.26 在场保持开启的同一 Init 会话内按碰撞位展开 AC SEQUENCE；
  每条前缀命令由 `k[5] + UID prefix[k] + CRC8` 组成，逐个恢复剩余 `32-k` 位 UID。
- 解码必须选择时间上最早的完整 SOF，不能在后续 AC 数据内部搜索“更深”的伪碰撞；
  每个采样相位连续确认两次，最多三轮枚举取 UID 并集。
- 单格集合最多保留 6 个去重 UID，500ms 复检一次；超过上限显示 `6+`。每个 UID 使用独立饱和缺失证据：漏读加 2、有效读取减 1、阈值 3，兼顾单次抗抖和约 0.5～1 秒的真实离场响应；零星边缘响应不得把离场确认整体清零。
  协议状态机和 CRC 已通过原生测试，但多实体标签仍必须完成线圈中心、边缘和叠放验收。
- 初次单标签仍需连续收到 3 个相同 UID 后再发布。
- UID 扫描只发送识别请求，不得发送标签存储器写命令。
- 标签离开应使用超时去抖，避免边缘位置反复出现/消失。
- 谐振电容和天线电感属于硬件标定项，固件不能修复严重失谐。

### 6.7 Tag 联机与移动提示

V0.27 的 HTTP 心跳请求可带完整 Tag JSON：`tagReaderState` 只能为 `scanning`、`stable` 或
`fault`，`tagRevision` 是模块本地变更游标，`tags` 为最多 6 个大写 8 位十六进制 UID，
`overflow` 表示枚举结果被截断。稳定集合、reader 状态或 overflow 变化时立即发心跳；否则
维持 2 秒续租。只有 `stable && !overflow` 的完整集合能够触发游戏逻辑，扫描中、故障和溢出
状态只用于诊断。模块重启后 revision 可从头开始，服务器必须以每次完整 body 为准。

心跳响应的 `movementCue` 固定包含 `mode`、`playerId` 和 `revision`。`departure` 表示当前格
是本次移动起点，整圈 LED 橙色呼吸；`destination` 表示目标格，整圈 LED 绿色双闪；其他格
使用 `none`。目标提示优先于出发提示，移动提示优先于常态的地区色、所有者色和占用呼吸。
固件仅在 revision 或 cue 内容变化时重置动画，重复心跳不得使动画不断从第一帧开始。

Tag 与玩家的一对一绑定、跨模块全局汇总和自动到达均属于服务器权威状态。目标格稳定读到
当前移动玩家的绑定 UID 后，服务器复用现有 `ConfirmPosition` 事务；格子不得自行推进回合，
玩家控制屏的手动确认入口必须保留。相邻格同时识别的强度仲裁仍是后续能力。


## 7. 功耗模式

当前每板约1W是设计预算，不是现有亮度常量已经证明的最坏功耗。须实现并实测启动、维护、正常、降载策略，见[直接24V板功耗固件规格](module-power-firmware.md)。INA226仅测本地5V侧，不能代表整盘24V总功率。硬件限流/软启动自行工作，不等待服务器计数。

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
- 当前按设备身份、HTTP租约及ORDER锚点派生assignment；16位RS485运行地址仍是历史提案。
- `order_index`：物理顺序号，从 0 或 1 开始必须在协议中固定。
- NVS 保存：硬件版本、亮度上限、INA226 校准、RFID 参数和最后故障。
- 不要把上一次运行地址当成当前物理顺序；重新拼接棋盘后必须重新枚举。

## 9. 历史提案：RS485 协议 V0（未部署）

以下帧格式仅保留设计背景，不是当前Tile V0.32的通信契约。当前使用HTTP与ORDER V1；将来启用RS485须重新审查并版本化。

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
