# Gridopoly 项目文档中心

更新日期：2026-08-06

本目录是 Gridopoly 产品、硬件、固件、游戏规则和玩家终端开发的统一入口。
日常开发不应依赖打开原理图查针脚；只有修改硬件连接、封装或 BOM 时才需要
回到嘉立创EDA。

当前硬件基线为 `Board1/Schematic1/P1 + Board1/PCB2_1`。引脚、接口和电源
网络已经通过嘉立创EDA API读取 `PCB2_1` 的实际焊盘网络复核。

## 文档级别

发生冲突时，按下面的级别判断，不要用历史记录覆盖当前规范。

| 级别 | 用途 | 处理原则 |
| --- | --- | --- |
| A：当前规范 | 接口、协议、规则和实现必须遵守的定义 | 开发与评审的直接依据 |
| B：实施与验收 | 调试步骤、测试场景、内容和视觉交付要求 | 用于实现、验证和发布 |
| C：设计记录 | 已讨论方案、设计理由和阶段性决策 | 用于理解背景；冲突时以 A 级为准 |
| D：历史归档 | 已过期审查和旧版本记录 | 仅供追溯，不得用于当前接线或实现 |

## 目录结构

```text
Docs/
├─ README.md             项目文档入口
├─ product/              产品目标与版本边界
├─ hardware/             硬件基线、引脚、互连和首板调试
├─ firmware/             ESP32-S3 固件与主控通信协议
├─ game/                 玩法、状态机、地图经济和内容目录
├─ player-console/       玩家圆屏 UI、视觉、Demo 和验收
├─ design-records/       设计决策与方案记录
└─ archive/              过期审查和历史资料
```

## A 级：当前规范

| 文档 | 内容 |
| --- | --- |
| [产品目标](product/product-goals.md) | 产品定位、当前边界、设计原则和里程碑 |
| [硬件基线](hardware/hardware-baseline.md) | 当前工程、PCB、铜层、关键器件和发布边界 |
| [ESP32-S3 引脚与接口定义](hardware/esp32-s3-pin-map.md) | GPIO、J4屏幕、USB、RFID、RS485、ORDER、INA226、WS2812、电源和磁吸触点 |
| [模块互连方案](hardware/module-interconnect-5wire.md) | 5个电气网络、15个物理触点、自动枚举、RS485和40模块供电边界 |
| [固件开发指南](firmware/firmware-development-guide.md) | ESP-IDF 工程结构、启动顺序、外设参数、通信帧和故障处理 |
| [主控与玩家圆屏历史协议](firmware/main-controller-protocol.md) | 已被正式 Wi-Fi/UDP 协议取代的 WebSocket/JSON 设计记录 |
| [Raspberry Pi 5 权威服务端](firmware/raspberry-pi-server.md) | 正式权威进程、网页、UDP、存档、systemd 和双网络部署 |
| [玩家屏 Wi-Fi/UDP 协议](firmware/wifi-udp-player-protocol.md) | UDP envelope、HMAC 会话、重放窗口、投影同步和断线恢复 |
| [双向交易按需协议](firmware/trade-protocol.md) | 双方资产与现金报价、revision、幂等、机器人响应和原子结算 |
| [ESP32-S3 测试游戏服务端](firmware/test-game-server.md) | 临时权威服务端、网页 API、烧录、HTTP/ESP-NOW 可靠性和实机验收 |
| [测试服务端 ESP-NOW 全量同步协议](firmware/test-game-server-espnow-protocol.md) | ESP32-S3 配对、完整动态状态、事件补发和六屏同步 |
| [游戏规则](game/game-rules.md) | Gridopoly 原创地产经营规则基线 |
| [权威游戏状态机](game/game-state-machine.md) | 回合、交易、债务、破产和核心不变量 |
| [玩家圆屏 UI 规范](player-console/player-console-ui-spec.md) | 480×480 圆屏、机械补偿、旋钮、触摸和页面规范 |

## B 级：实施与验收

| 文档 | 内容 |
| --- | --- |
| [首板调试清单](hardware/bring-up-checklist.md) | 上电、分电源域、烧录、显示、LED、RS485、ORDER和RFID调谐 |
| [可变地图与经济规范](game/map-economy-spec.md) | 地图包、28格经济表、节奏换算和校验 |
| [游戏内容目录](game/game-content-catalog.md) | 40格内容母集、地产、角色和原创卡片 |
| [玩家圆屏 Demo 场景](player-console/player-console-demo-scenarios.md) | Demo Lab 场景和固定测试数据 |
| [玩家圆屏验收测试](player-console/player-console-acceptance-tests.md) | 圆屏软硬件验收矩阵和发布条件 |
| [玩家屏幕 ESP-NOW 接入](player-console/espnow-integration.md) | 测试服务端连接、reducer、Heartbeat 和全量同步验收 |
| [视觉与素材规范](player-console/grid-city-visual-guide.md) | 网格都市视觉语言、素材格式和资源预算 |

## C 级：设计记录

| 文档 | 内容 |
| --- | --- |
| [玩家圆屏与规则系统设计](design-records/2026-08-02-gridopoly-player-console-design.md) | 已批准范围、总体架构和实施顺序 |
| [玩家圆屏交互改版设计](design-records/2026-08-02-player-console-interaction-v2-design.md) | 轮盘、导航、确认、债务和RFID交互决策 |
| [未来玩家头像创建规格](player-console/player-avatar-setup-spec.md) | 新对局 40/60 外貌编辑器、组合配方、确认屏障、机器人头像和全局圆形头像；尚未实现 |

## D 级：历史归档

| 文档 | 内容 |
| --- | --- |
| [2026-07-27 原理图检查记录](archive/schematic-review-2026-07-27.md) | 历史问题追溯；不得作为当前接线依据 |

## 按任务阅读

| 任务 | 建议顺序 |
| --- | --- |
| ESP32-S3 固件 | [固件开发指南](firmware/firmware-development-guide.md) → [引脚定义](hardware/esp32-s3-pin-map.md) → [模块互连](hardware/module-interconnect-5wire.md) |
| 树莓派主控 | [服务端部署](firmware/raspberry-pi-server.md) → [Wi-Fi/UDP 协议](firmware/wifi-udp-player-protocol.md) → [游戏状态机](game/game-state-machine.md) → [游戏规则](game/game-rules.md) |
| 首板调试 | [硬件基线](hardware/hardware-baseline.md) → [首板调试清单](hardware/bring-up-checklist.md) → [引脚定义](hardware/esp32-s3-pin-map.md) |
| PCB或结构修改 | [硬件基线](hardware/hardware-baseline.md) → [引脚定义](hardware/esp32-s3-pin-map.md) → 当前嘉立创EDA工程 |
| 玩家圆屏开发 | [Wi-Fi/UDP协议](firmware/wifi-udp-player-protocol.md) → [UI规范](player-console/player-console-ui-spec.md) → [Demo场景](player-console/player-console-demo-scenarios.md) → [验收测试](player-console/player-console-acceptance-tests.md)；未来头像功能另见[头像创建规格](player-console/player-avatar-setup-spec.md)；[ESP-NOW接入](player-console/espnow-integration.md)仅作回退 |
| 玩法与内容 | [产品目标](product/product-goals.md) → [游戏规则](game/game-rules.md) → [地图经济](game/map-economy-spec.md) → [内容目录](game/game-content-catalog.md) |

## 当前硬件架构

- 主控：`ESP32-S3-WROOM-1-N16R8`
- 屏幕：ST7789，240×320，只写 SPI，J4 2×4 镜像接口
- RFID：HTRC110，125kHz，外接约 384µH 线圈并实物调谐
- LED：10 × `WS2812B-B-V6`，GPIO21 控制
- 通信：SN65HVD75 半双工 RS485
- 顺序检测：GPIO9 输入、GPIO10 控制 2N7002 开漏输出
- 模块母线：6 个并联 `24V_BUS` 触点、6 个并联 GND 触点
- 信号触点：`BUS_A`、`BUS_B`、`ORDER`
- 本地 24V 转 5V：LMR16030
- 双路 5V 选择：TPS2121，本地 5V 与 USB VBUS 二选一
- 本模块电流检测：INA226 与 10mΩ 分流电阻
- 3.3V：TPS62160DGKR
- RFID 电源：`5V_IN -> FB1 -> 5V_RFID`

完整电源路径：

```text
24V_BUS -> U24 -> LMR16030 -> 5V_FROM_24 -> U8 -> TPS2121 IN1
USB_VBUS -> U7 ------------------------------------> TPS2121 IN2
TPS2121 OUT -> 5V_SELECTED -> 10mΩ -> 5V_IN
5V_IN -> TPS62160DGKR -> 3V3_SYS
5V_IN -> FB1 -> 5V_RFID
```

## GPIO 摘要

| 功能 | GPIO |
| --- | --- |
| LCD RST/DC/MOSI/SCLK/CS/BL | 4/5/6/7/15/16 |
| RFID SCLK/DIN/DOUT | 17/18/8 |
| ORDER IN/OUT | 9/10 |
| INA226 SCL/SDA | 11/12 |
| RS485 DIR/TX/RX | 14/43/44 |
| USB D-/D+ | 19/20 |
| WS2812 数据 | 21 |

## 已确认的接口变更

1. 母线由旧版 12V 改为 `24V_BUS`。
2. 磁吸触点由每侧 3×3 改为每侧 3×5，共 15 个触点。
3. 屏幕接口当前位号为 `J4`，针序采用公母镜像。
4. GPIO21 已连接 10 颗 WS2812，不再是预留项。
5. 24V 转 5V 已改为 U23 `LMR16030PDDAR`。
6. TP5 当前为 `24V_BUCK_IN`。
7. 加入 BOM 的 136 个器件均已填写 MPN 和 LCSC；TP1～TP5 不加入 BOM。
8. 当前 PCB 为 6 个铜层：Top、Inner1～Inner4、Bottom。
9. 当前开发 PCB 是 `Board1/PCB2_1`；其他历史 PCB 不作为开发依据。

## 网络命名

| 名称 | 含义 |
| --- | --- |
| `24V_BUS` | 模块间 24V 直通母线 |
| `24V_BUCK_IN` | U24 后的本地降压输入 |
| `5V_FROM_24` | LMR16030 输出 |
| `5V_FROM_24_FUSED` | 本地 5V 保险支路 |
| `USB_VBUS` | Type-C 输入 5V |
| `USB_5V_FUSED` | USB 保险支路 |
| `5V_SELECTED` | TPS2121 输出、分流电阻前 |
| `5V_IN` | 分流电阻后、本模块主 5V |
| `5V_RFID` | RFID 滤波 5V |
| `3V3_SYS` | 本模块 3.3V |
| `BUS_A/B` | RS485 差分总线 |
| `ORDER_IN/OUT` | 模块物理顺序链 |
| `LCD_*` | ST7789 屏幕 |
| `ESP_RFID_*` | ESP32 侧 RFID 数字信号 |
| `HTRC_*` | HTRC110 侧 5V 数字信号 |
| `LED_DATA_*` | WS2812 级联数据 |
| `BUCK24_*` | 24V 转 5V 开关电源局部网络 |
| `ANT_*`、`U1_TX*`、`U1_RX` | RFID 天线模拟网络 |

## 维护规则

1. 新文档应放入对应子目录，不要直接堆在 `Docs/` 根目录。
2. 每次改原理图后，先通过 API 读取实际 PCB 焊盘网络，再更新文档。
3. 固件 GPIO 以 [ESP32-S3 引脚与接口定义](hardware/esp32-s3-pin-map.md) 为准。
4. 新选贴片器件优先要求嘉立创贴装库存不少于 1000。
5. 位号、网络名、Footprint、MPN、LCSC 在原理图、PCB、BOM和文档中一致。
6. C、D 级文档不得覆盖 A 级规范；冲突内容应明确标为历史信息。
7. 通信协议、GPIO 或连接器发生不兼容变化时，同时更新关联规范和版本号。
8. 提交可制造版本前，完整执行 [首板调试清单](hardware/bring-up-checklist.md)；DRC 为零不能替代电气和实物验证。
