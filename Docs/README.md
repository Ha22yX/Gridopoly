# Gridopoly 硬件文档索引

更新日期：2026-07-29

引脚、接口和电源网络以嘉立创EDA当前原理图同步后的 PCB 焊盘网络为准。

## 当前文档

| 文件 | 内容 |
| --- | --- |
| `esp32-s3-pin-map.md` | ESP32-S3、J4屏幕、USB、RFID、RS485、ORDER、INA226、WS2812、电源和3×5磁吸触点 |
| `module-interconnect-5wire.md` | 5个电气网络、15个物理触点、自动枚举、RS485协议和40模块供电边界 |
| `product-goals.md` | 产品目标和版本范围 |
| `schematic-review-2026-07-27.md` | 历史审查，仅用于追溯，不得作为当前接线依据 |

## 玩家圆屏与游戏规则

| 文件 | 内容 |
| --- | --- |
| [game-rules.md](game-rules.md) | 美国经典地产经营机制的 Gridopoly 原创规则基线 |
| [game-content-catalog.md](game-content-catalog.md) | 40 格内容母集、22 块地产、角色与 32 张原创卡片 |
| [map-economy-spec.md](map-economy-spec.md) | 可变地图、28 格经济表与三档节奏 |
| [game-state-machine.md](game-state-machine.md) | 主控回合、交易、债务与破产状态机 |
| [player-console-ui-spec.md](player-console-ui-spec.md) | 480×480 圆屏、50°补偿、旋钮和触摸规范 |
| [player-console-demo-scenarios.md](player-console-demo-scenarios.md) | Demo Lab 场景和固定测试数据 |
| [main-controller-protocol.md](main-controller-protocol.md) | 树莓派热点、配对、WebSocket 与恢复协议 |
| [grid-city-visual-guide.md](grid-city-visual-guide.md) | 网格都市原创视觉与素材规范 |
| [player-console-acceptance-tests.md](player-console-acceptance-tests.md) | 圆屏软硬件验收矩阵 |
| [总设计规格](superpowers/specs/2026-08-02-gridopoly-player-console-design.md) | 已批准的范围、架构和实施顺序 |

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
- 3.3V：AP63203
- RFID 电源：`5V_IN -> FB1 -> 5V_RFID`

完整电源路径：

```text
24V_BUS -> U24 -> LMR16030 -> 5V_FROM_24 -> U8 -> TPS2121 IN1
USB_VBUS -> U7 ------------------------------------> TPS2121 IN2
TPS2121 OUT -> 5V_SELECTED -> 10mΩ -> 5V_IN
5V_IN -> AP63203 -> 3V3_SYS
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

1. 每次改原理图后，先通过 API 读取实际 PCB 焊盘网络，再更新文档。
2. 固件 GPIO 以 `esp32-s3-pin-map.md` 为准。
3. 新选贴片器件优先要求嘉立创贴装库存不少于 1000。
4. 位号、网络名、Footprint、MPN、LCSC 在原理图、PCB、BOM和文档中一致。
5. 历史审查文件只用于追溯，不得覆盖当前引脚表。
