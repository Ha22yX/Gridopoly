# Gridopoly 文档中心

[软件与固件后续需求/验收](firmware/development-roadmap-2026-09-21.md)按格子、服务端、玩家屏和拓扑列出实施要求。

更新：2026-09-21。先读[当前开发交接](CURRENT-DEVELOPMENT-HANDOFF.md)了解已完成、已部署、待验证与下一步，再按下表进入规范。历史实测不等于当前设备在线状态。

当前产品有 **长边模块 PCB2_1、角落模块 PCB2_2 两种棋盘 PCB**，另有独立玩家圆形终端。正式权威服务为 Raspberry Pi 5；玩家走认证 Wi-Fi/UDP，格子 V0.32 走 Wi-Fi/HTTP 与 ORDER 物理邻接信标，RS485 尚未启用。

## 当前最重要的状态

- 两板共用本地 24V→5V 受控充电/限流设计；长边电源区是 17 原件 + 8 新件，不是新增25件。
- 角落提供 USB1 PD→24V_BUS，目标单板或16–40个约1W模块、源出口0–60W。需单口20V5A电源与5A线；0外接负载不等于零自身功耗。
- 原理图已有有条件理论复核；新增电源 PCB 同步、PD NVM 固件及整盘硬件验收尚未完成，不能标“稳定供电已全面实测”。
- J101 已取消，角落 ESP32 GPIO11/12共享I²C、GPIO13复位PD、VSYS=3.3V。首次用原有 J1 调试口供电配置，日常才仅接 USB1。
- 当前板间每侧是6Pin+3Pin共9触点，旧15触点磁吸表已废止。电源闭环与 ORDER/RS485 拓扑分开设计；当前 ORDER 拒绝闭环。
- 玩家显示 core0 曾性能全通过，但因用户反馈闪烁/错位已回到 core1；视觉验收与 Retarget 长帧仍待完成。

## 规范与开发入口

| 范围 | 文档 |
| --- | --- |
| 全项目进展/下一步 | [当前开发交接](CURRENT-DEVELOPMENT-HANDOFF.md) |
| 产品目标 | [目标与里程碑](product/product-goals.md) |
| PCB与硬件 | [两种 PCB](hardware/pcb-variants.md) → [硬件基线](hardware/hardware-baseline.md) → [GPIO/接口](hardware/esp32-s3-pin-map.md) → [模块互连](hardware/module-interconnect-5wire.md) |
| 固件全貌 | [固件文档入口](firmware/README.md) → [格子开发指南](firmware/firmware-development-guide.md) |
| PD与功耗开发 | [角落 PD 固件规格](firmware/corner-pd-firmware.md) → [供电验收](hardware/power-system-acceptance.md) |
| 制板和调试 | [首板清单](hardware/bring-up-checklist.md) → [采购核对](hardware/bom-review-2026-09-21.md) |
| 服务端 | [Raspberry Pi](firmware/raspberry-pi-server.md) → [玩家UDP](firmware/wifi-udp-player-protocol.md) |
| 格子联动 | [分配/Tag/自动到达](firmware/tile-module-debug-assignment.md) → [ORDER V1](firmware/tile-order-protocol.md) |
| 玩家终端 | [实现基线](player-console/current-player-console-baseline.md) → [UI规范](player-console/player-console-ui-spec.md) → [验收](player-console/player-console-acceptance-tests.md) |
| 头像 | [创建规格](player-console/player-avatar-setup-spec.md) → [组件流协议](firmware/avatar-component-protocol.md) |
| 玩家详情与交易 | [详情查询](firmware/player-detail-query-protocol.md) / [交易](firmware/trade-protocol.md) |
| 游戏 | [规则](game/game-rules.md) / [状态机](game/game-state-machine.md) / [地图经济](game/map-economy-spec.md) / [内容](game/game-content-catalog.md) |
| 素材与视觉 | [视觉规范](player-console/grid-city-visual-guide.md) / [Demo场景](player-console/player-console-demo-scenarios.md) |
| 设备 | [最后已知连接](firmware/current-device-connections.md) |

## 电源设计记录的阅读顺序

| 阶段 | 记录及当前效力 |
| --- | --- |
| 最初24模块方案 | [方案](hardware/corner-usbc-pd-24v-plan-2026-09-20.md)，历史需求 |
| 初版原理图与检查 | [绘制](hardware/corner-usbc-pd-24v-schematic-2026-09-20.md) / [电气审查](hardware/corner-usbc-pd-24v-electrical-review-2026-09-20.md)，历史电路 |
| 0–60W四开关入口 | [60W审查](hardware/corner-usbc-pd-24v-60w-review-2026-09-21.md)，随后又增加接收端保护及主控配置 |
| 双板紧凑支路 | [8件新增方案](hardware/unified-module-hotplug-power-2026-09-21.md) |
| R120及理论裕量 | [理论复核](hardware/unified-power-theory-review-2026-09-21.md)，不是硬件实测 |
| 最新角落编程连接 | [ESP32配置PD](hardware/corner-pd-esp32-programming-2026-09-21.md)，取代J101流程 |

同一文件的早期“当前”只指当时快照。最终角落原理图在 PCB Files/CornerModule/PDProgramming，长边在 PCB Files/ModulePower/PowerIntegrity；PCB布局导出早于这些原理图电源变更。

## 开发记录与历史

[恢复开发协调](firmware/development-coordination-2026-09-07.md)、[服务端进度](firmware/server-development-progress-2026-09-07.md)、[玩家进度](firmware/player-console-development-progress-2026-09-07.md)、[格子进度](firmware/tile-module-development-progress-2026-09-07.md)、[格子刷新](firmware/tile-display-refresh-progress-2026-09-08.md)、[显示时钟](firmware/tile-display-clock-progress-2026-09-08.md)、[头像/输入](firmware/avatar-input-recovery-progress-2026-09-08.md)、[ORDER双板](firmware/tile-order-development-progress-2026-09-09.md)保留原始阶段结论。

历史入口：[8月暂停交接](archive/development-handoff-2026-08-28.md)、[7月原理图检查](archive/schematic-review-2026-07-27.md)、[PCB接任记录](hardware/pcb-design-handoff-2026-09-19.md)、[旧连接器研究](hardware/connector-selection-2026-09-19.md)。design-records 与 superpowers 内方案用于追溯，不覆盖当前协议。

ESP32测试服务端、ESP-NOW、旧WebSocket/JSON保留回归用途，见[固件历史入口](firmware/README.md)，不替代树莓派正式架构。

## 维护与发布规则

当前规范解释接口；实施清单规定如何验收；带日期记录只证明其明确范围；归档不得用于当前接线。发生冲突先查对应源文件/网表和最新交接，不只比较文件日期。

每次设计变化分别记录原理图、PCB、BOM、源码、构建、设备部署和实测状态；需要PCB同步但尚未完成时明确标出。不得用DRC、仿真或正常串口代替电气/视觉实测。

新增文档归入对应子目录，更新关联链接。按用户要求使用嘉立创商城有库存器件；国内/贴装库存与查询日期区分，下单重查，不把历史数量当预留。SSH/网络密钥、本地缓存及备份不提交。当前无自动续派定时任务。
