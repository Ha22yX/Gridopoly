# Gridopoly 文档中心

更新：2026-09-27。先读[当前开发交接](CURRENT-DEVELOPMENT-HANDOFF.md)了解已有实现、最后部署和未完成验收，再按下表进入规范。当前说明已同步V0.33、32格闭环、四屏/Tag流程、玩家输入修复和电池底板RC5。

## 当前状态入口

| 要查的内容 | 文档 |
| --- | --- |
| 全项目当前结果与责任 | [当前交接](CURRENT-DEVELOPMENT-HANDOFF.md) |
| 当前设备IP、MAC、串口、实际固件 | [最后有证据的设备台账](firmware/current-device-connections.md)，不是实时在线状态 |
| 32块/4屏及批量刷写 | [软件发布记录](firmware/batch32-game-release-2026-09-26.md)、[批量工具](../Tools/release/README.md)；当前冻结包v4 |
| 32环与非环形测试、配对流程 | [跨端契约](firmware/ring-onboarding-contract-2026-09-26.md)、[单模块/地图修复](firmware/newgame-open-chain-fix-2026-09-26.md) |
| 玩家输入 | [9月26日旋钮/手写修复](firmware/player-input-handwriting-fix-2026-09-26.md)；手写已确认、最新旋钮待复验 |
| 玩家电池底板 | [电路/接线/采购](hardware/player-usbc-battery-base-2026-09-27.md)、[RC5制造候选](../PCB%20Files/PlayerConsolePower/Layout-2026-09-27-RC5/README.md) |
| 电池功能是否已部署 | [固件接入证据](firmware/player-battery-telemetry-progress-2026-09-27.md)：源码/构建已完成，默认关闭，未部署 |
| 接下来做什么 | [实施与验收路线图](firmware/development-roadmap-2026-09-21.md) |

棋盘为长边PCB2_1与角落PCB2_5，直接24V供电；另有独立玩家屏和新增RC5电池底板。玩家Wi-Fi/UDP、格子Wi-Fi/HTTP＋ORDER，RS485未启用。完整ORDER自动闭环仅支持32节点及四角约束；其他尺寸地图和单模块配对不要求闭环。

## 规范与开发入口

| 范围 | 文档 |
| --- | --- |
| 产品 | [目标与里程碑](product/product-goals.md) |
| PCB与硬件 | [硬件入口](hardware/README.md)、[两种棋盘PCB](hardware/pcb-variants.md)、[硬件基线](hardware/hardware-baseline.md) |
| GPIO与板间连接 | [GPIO/接口](hardware/esp32-s3-pin-map.md)、[5网络/9触点互连](hardware/module-interconnect-5wire.md) |
| 棋盘电源 | [直接24V](hardware/corner-dc24-input-2026-09-21.md)、[181项供电复核](hardware/power-schematic-audit-2026-09-21.md)、[功耗固件](firmware/module-power-firmware.md)、[供电验收](hardware/power-system-acceptance.md) |
| 制板与采购 | [首板检查](hardware/bring-up-checklist.md)、[历史BOM核对](hardware/bom-review-2026-09-21.md)、[24V电源选型](hardware/external-24v-supply.md) |
| 固件 | [固件入口](firmware/README.md)、[格子指南](firmware/firmware-development-guide.md)、[格子工程](../Firmware/TileModule/README.md) |
| 服务端 | [Pi部署](firmware/raspberry-pi-server.md)、[玩家UDP](firmware/wifi-udp-player-protocol.md)、[外置AP](firmware/external-ap-deployment-2026-09-26.md) |
| 格子联动 | [分配/Tag/自动到达](firmware/tile-module-debug-assignment.md)、[ORDER V1](firmware/tile-order-protocol.md) |
| 玩家终端 | [当前实现](player-console/current-player-console-baseline.md)、[UI](player-console/player-console-ui-spec.md)、[验收](player-console/player-console-acceptance-tests.md) |
| 头像与姓名 | [创建规格](player-console/player-avatar-setup-spec.md)、[组件协议](firmware/avatar-component-protocol.md)、[手写识别](firmware/player-console-handwriting-neural.md) |
| 详情/交易 | [详情查询](firmware/player-detail-query-protocol.md)、[交易](firmware/trade-protocol.md) |
| 游戏 | [规则](game/game-rules.md)、[状态机](game/game-state-machine.md)、[地图经济](game/map-economy-spec.md)、[内容](game/game-content-catalog.md) |
| 视觉 | [视觉规范](player-console/grid-city-visual-guide.md)、[Demo场景](player-console/player-console-demo-scenarios.md) |
| 玩家电池 | [底板](hardware/player-usbc-battery-base-2026-09-27.md)、[UART约定](hardware/player-battery-telemetry-interface-2026-09-27.md)、[GPIO证据](hardware/player-screen-uart-gpio-evidence-2026-09-27.md) |

## 版本沿革与历史效力

- 玩家底板：DesignDraft原始设计 → 按键内部常通组修复 → RC1布线 → RC2 USB路径调整 → RC3电源走线/丝印 → RC4 PH2.0 J3 → **RC5 J3黑1/红2及地铜修复**。仅RC5为当前归档候选，仍未制造放行；旧版原始网表、报告和哈希不改写。
- 棋盘：旧24模块PD方案、PD NVM与218器件布局已取消；当前为直接24V。保留[9月22日硬件汇报](hardware/development-handoff-2026-09-22.md)、[历史PD设计](hardware/corner-usbc-pd-24v-plan-2026-09-20.md)、[接口核查](hardware/interconnect-audit-2026-09-21.md)供追溯，历史“ORDER拒绝环”不覆盖最新闭环契约。
- 软件：[协调](firmware/development-coordination-2026-09-07.md)、[服务端早期进度](firmware/server-development-progress-2026-09-07.md)、[玩家早期进度](firmware/player-console-development-progress-2026-09-07.md)、[格子早期进度](firmware/tile-module-development-progress-2026-09-07.md)、[双板ORDER](firmware/tile-order-development-progress-2026-09-09.md)、[刷新](firmware/tile-display-refresh-progress-2026-09-08.md)、[显示时钟](firmware/tile-display-clock-progress-2026-09-08.md)、[早期输入](firmware/avatar-input-recovery-progress-2026-09-08.md)。历史用户确认只对应其版本/场景。
- 旧入口：[8月交接](archive/development-handoff-2026-08-28.md)、[整合前交接](archive/development-handoff-before-2026-09-27-refresh.md)、[PCB接任](hardware/pcb-design-handoff-2026-09-19.md)、[旧连接器](hardware/connector-selection-2026-09-19.md)。`design-records`、`superpowers`及ESP32测试服务端仅供沿革/回归，不替代正式Pi架构。

## 维护与发布

当前规范解释接口，实施清单规定验收，带日期记录只证明其明确范围。冲突时核对当前源码/网表、部署证据和最新交接，不能只比较文档日期。新增实现应同步这里及对应规范，不继续堆叠多个互相矛盾的“当前版本”。

构建、测试、部署、功能、性能、视觉和电气分别记录；DRC或串口正常不代替实物验收。供应库存注明查询日期，下单前重查。凭据、固件私有配置和本地备份不提交。当前无自动续派定时任务。

本轮整理范围及证据见[文档更新记录](documentation-refresh-2026-09-27.md)。
