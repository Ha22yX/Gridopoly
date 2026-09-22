# Gridopoly 文档中心

> 2026-09-22硬件同步：见[完整硬件开发汇报](hardware/development-handoff-2026-09-22.md)，PD方案已取消，当前证据与限制以该汇报及所链接的直接24V审查为准。

> 2026-09-21 最新变更：用户已取消角落 PD→24V，改为 5.5×2.1mm 中心正极的直接24V输入；见[当前直流入口设计](hardware/corner-dc24-input-2026-09-21.md)。当前角落为PCB2_5；原理图与PCB器件/网络已更新，入口布局布线和整盘验收待完成。

[软件与固件后续需求/验收](firmware/development-roadmap-2026-09-21.md)按格子、服务端、玩家屏和拓扑列出实施要求。

更新：2026-09-21。先读[当前开发交接](CURRENT-DEVELOPMENT-HANDOFF.md)了解已完成、已部署、待验证与下一步，再按下表进入规范。历史实测不等于当前设备在线状态。

当前产品有 **长边模块 PCB2_1、角落模块 PCB2_5 两种棋盘 PCB**，另有独立玩家圆形终端。正式权威服务为 Raspberry Pi 5；玩家走认证 Wi-Fi/UDP，格子 V0.32 走 Wi-Fi/HTTP 与 ORDER 物理邻接信标，RS485 尚未启用。

## 当前最重要的状态

- 两板共用本地 24V→5V 受控充电/限流设计；长边电源区是 17 原件 + 8 新件，不是新增25件。
- 角落改为 J301 直接24V输入，5.5×2.1mm中心正极，6件入口保护电路；建议稳压24V/5A电源，60W目标尚待热设计及整盘验收。
- 新增供电原理图181项引脚断言通过，见[供电复核](hardware/power-schematic-audit-2026-09-21.md)。旧PD布局候选已被替代；当前角落同步重复问题已修复，最终布局布线、两板制造放行及整盘验收仍未完成。
- PD电路与NVM配置需求已取消；GPIO11/12仍连接INA226，GPIO13未连接。J1保留本地USB调试用途。
- 当前板间每侧是6Pin+3Pin共9触点，旧15触点磁吸表已废止。电源闭环与 ORDER/RS485 拓扑分开设计；当前 ORDER 拒绝闭环。
- 玩家显示 core0 曾性能全通过，但因用户反馈闪烁/错位已回到 core1；视觉验收与 Retarget 长帧仍待完成。

## 规范与开发入口

| 范围 | 文档 |
| --- | --- |
| 全项目进展/下一步 | [当前开发交接](CURRENT-DEVELOPMENT-HANDOFF.md) |
| 产品目标 | [目标与里程碑](product/product-goals.md) |
| PCB与硬件 | [两种 PCB](hardware/pcb-variants.md) → [硬件基线](hardware/hardware-baseline.md) → [GPIO/接口](hardware/esp32-s3-pin-map.md) → [模块互连](hardware/module-interconnect-5wire.md) |
| 固件全貌 | [固件文档入口](firmware/README.md) → [格子开发指南](firmware/firmware-development-guide.md) |
| 电源与功耗开发 | [直流板固件要求](firmware/module-power-firmware.md) → [供电验收](hardware/power-system-acceptance.md) |
| 硬件开发汇报 | [硬件总索引](hardware/README.md) → [2026-09-22交接](hardware/development-handoff-2026-09-22.md) → [电源选购](hardware/external-24v-supply.md) |
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
| 历史角落编程连接 | [ESP32配置PD](hardware/corner-pd-esp32-programming-2026-09-21.md)，已取消，不实施NVM固件 |
| 当前直接24V | [DC入口](hardware/corner-dc24-input-2026-09-21.md)及[181引脚供电复核](hardware/power-schematic-audit-2026-09-21.md) |
| 当前接口/BOM | [36针对接核查](hardware/interconnect-audit-2026-09-21.md)及[13张匹配提示](hardware/bom-review-2026-09-21.md) |

同一文件的早期“当前”只指当时快照。角落优先使用DC24-2026-09-21/SyncRepair同步修复记录，两板供电审查网表位于PCB Files/DesignAudit-2026-09-21。PDProgramming、PowerIntegrity中的PD部分及LayoutCandidate-2026-09-21均为历史；不得用于覆盖直接24V工程。

## 开发记录与历史

[恢复开发协调](firmware/development-coordination-2026-09-07.md)、[服务端进度](firmware/server-development-progress-2026-09-07.md)、[玩家进度](firmware/player-console-development-progress-2026-09-07.md)、[格子进度](firmware/tile-module-development-progress-2026-09-07.md)、[格子刷新](firmware/tile-display-refresh-progress-2026-09-08.md)、[显示时钟](firmware/tile-display-clock-progress-2026-09-08.md)、[头像/输入](firmware/avatar-input-recovery-progress-2026-09-08.md)、[ORDER双板](firmware/tile-order-development-progress-2026-09-09.md)保留原始阶段结论。

历史入口：[8月暂停交接](archive/development-handoff-2026-08-28.md)、[7月原理图检查](archive/schematic-review-2026-07-27.md)、[PCB接任记录](hardware/pcb-design-handoff-2026-09-19.md)、[旧连接器研究](hardware/connector-selection-2026-09-19.md)。design-records 与 superpowers 内方案用于追溯，不覆盖当前协议。

ESP32测试服务端、ESP-NOW、旧WebSocket/JSON保留回归用途，见[固件历史入口](firmware/README.md)，不替代树莓派正式架构。

## 维护与发布规则

当前规范解释接口；实施清单规定如何验收；带日期记录只证明其明确范围；归档不得用于当前接线。发生冲突先查对应源文件/网表和最新交接，不只比较文件日期。

每次设计变化分别记录原理图、PCB、BOM、源码、构建、设备部署和实测状态；需要PCB同步但尚未完成时明确标出。不得用DRC、仿真或正常串口代替电气/视觉实测。

新增文档归入对应子目录，更新关联链接。按用户要求使用嘉立创商城有库存器件；国内/贴装库存与查询日期区分，下单重查，不把历史数量当预留。SSH/网络密钥、本地缓存及备份不提交。当前无自动续派定时任务。
