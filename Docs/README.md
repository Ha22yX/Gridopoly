# Gridopoly 文档中心

更新：2026-10-17晚间。先读[当前开发交接](CURRENT-DEVELOPMENT-HANDOFF.md)：COM10/11、COM16～18、COM20～26共12板已写入V0.39旋转流星提示，余COM12～15四板仍V0.37；COM10尾两灯在旧版也不亮，单板故障点待查，详见[流星分批升级](firmware/tile-meteor-cues-2026-10-17.md)。16格已具备按需房屋/旅馆图片。Pi原右键菜单内资金/归属编辑已获用户确认，配对稳定性修复已部署；四台玩家屏英文/配对提示、弧形电池和防抖已部署，画面、电量及LOCKED已确认，COM7首次电量通信异常经手动重开恢复。[前阶段部署记录](firmware/debug-tools-pairing-rollout-2026-10-17.md)保留。真实新局配对、完整四真人实体一局、动态显示/性能、电气和新机械实装仍分项待验。冻结包trial16-2026-10-15-v4不包含后续全部单台升级。

## 当前状态入口

2026-10-16新增：[房屋/旅馆按需下载与实板记录](firmware/development-artwork-runtime-2026-10-16.md)。服务器素材与HTTP读取期限修复已部署，16块已完成V0.37写入、联网/环形分配检查和用户目视验收，保留V0.36已验证的方向/低亮设置。默认图片仍在格子固件，升级图片只缓存到RAM，重启清空。

| 要查的内容 | 文档 |
| --- | --- |
| 全项目当前结果与责任 | [当前交接](CURRENT-DEVELOPMENT-HANDOFF.md) |
| 配对/来源/目标流星灯、COM10尾两灯不亮 | [V0.39实现、分批部署及同板旧版对照](firmware/tile-meteor-cues-2026-10-17.md) |
| 右键调试、开局配对、柔和呼吸、英文UI | [本轮集成与实际部署](firmware/debug-tools-pairing-rollout-2026-10-17.md)、[服务器修复](firmware/server-debug-pairing-2026-10-17.md)、[格子V0.38](firmware/tile-cue-pairing-2026-10-17.md)、[玩家英文/配对](firmware/player-tag-pair-freshness-2026-10-17.md) |
| 16 格试玩开发、四板一批烧录 | [本轮发布与验收](firmware/trial16-release-2026-10-15.md) |
| 当前设备IP、MAC、串口、实际固件 | [最后有证据的设备台账](firmware/current-device-connections.md)，不是实时在线状态 |
| 候选包与批量刷写 | [本轮16格冻结包](firmware/trial16-release-2026-10-15.md)、[批量工具](../Tools/release/README.md)；[历史32格v4](firmware/batch32-game-release-2026-09-26.md) |
| 全在线16/24/32/40环、开链测试与配对 | [跨端契约](firmware/ring-onboarding-contract-2026-09-26.md)、[单模块/地图修复](firmware/newgame-open-chain-fix-2026-09-26.md) |
| 玩家输入 | [输入批处理与局部刷新](firmware/player-16-trial-progress-2026-10-15.md)、[9月26日旋钮/手写修复](firmware/player-input-handwriting-fix-2026-09-26.md)；当前普通固件见设备台账，COM28手写和精确卡点仍待现场验证 |
| 玩家电池底板 | [电路/接线/采购](hardware/player-usbc-battery-base-2026-09-27.md)、[RC6-TP当前设计候选](../PCB%20Files/PlayerConsolePower/Testpoints-2026-10-15/README.md) |
| 电池功能是否已部署 | [弧形电池/充电防抖](firmware/player-battery-arc-ui-2026-10-17.md)、[上传后自动恢复](firmware/player-battery-warm-recovery-2026-10-17.md)：COM27/28/29已启用并有用户反馈，COM7仍关闭；长期精度/异常恢复另验 |
| 新屏显示与第四屏 | [等待页修复](firmware/player-new-screen-display-progress-2026-10-15.md)、[COM29首次部署](firmware/player-fourth-screen-flash-2026-10-15.md)、[12MHz显示结果](firmware/player-com29-display-progress-2026-10-15.md) |
| 热点与便携联网 | [7台关联调查](firmware/ap-capacity-investigation-2026-10-15.md)、[已购AWUS036ACM](firmware/portable-wifi-selection-2026-10-15.md)；[USB已部署及重启自启验收](firmware/usb-ap-deployment-2026-10-15.md)，20台同时接入已实证、36台待验 |
| 线圈包围屏幕时的RFID故障 | [COM8单板实机调查](firmware/tile-rfid-screen-coupling-2026-10-15.md)；区分供电、芯片告警与实际UID读取 |
| 3D与机械 | [Full16 R1、玩家R3、实物边界及装配PDF](mechanical/README.md)；R3仅更换02支架，其余四件保持R2 |
| 格子屏方向及本次合并升级 | [180度旋转＋LED降亮V0.36](firmware/tile-display-rotation-2026-10-16.md)，12长边＋4角落已分批升级，目视结果及逐板记录见内 |
| 格子LED过亮 | [V0.35低亮实现](firmware/tile-led-brightness-2026-10-15.md)已合入[V0.36逐板升级](firmware/tile-display-rotation-2026-10-16.md)，以最新实机记录为准 |
| 重开局分配异常 | [150组状态/230实际心跳及用户恢复确认](firmware/newgame16-assignment-observation-2026-10-15.md)，首次原因未定、未部署修复 |
| 本轮会话与变更汇总 | [晚间全面整合](documentation-refresh-2026-10-15-evening.md) |
| 接下来做什么 | [实施与验收路线图](firmware/development-roadmap-2026-09-21.md) |

棋盘为长边PCB2_1与角落PCB2_5，直接24V供电；另有独立玩家屏和RC6-TP电池底板候选。玩家Wi-Fi/UDP、格子Wi-Fi/HTTP＋ORDER，RS485未启用。全在线ORDER自动闭环现支持目录16/24/32/40节点及实际四角校验，已部署Pi；16实体环ready及四角0/4/8/12自动分配已验，锚点改派/物理断环另验。单模块配对不要求闭环。玩家底板旧订单实物短路与最新制造候选的区别见当前交接。

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
| 地产建房与旅馆美术 | [22块地产六级设计](game/property-development-art-v1.md)、[DevelopmentV1素材与互动图鉴](../Assets/GridCity/DevelopmentV1/README.md)；132状态及离线检查已完成，尚未接入运行时 |
| 玩家电池 | [底板](hardware/player-usbc-battery-base-2026-09-27.md)、[UART约定](hardware/player-battery-telemetry-interface-2026-09-27.md)、[GPIO证据](hardware/player-screen-uart-gpio-evidence-2026-09-27.md) |

## 版本沿革与历史效力

- 玩家底板：DesignDraft原始设计 → 按键内部常通组修复 → RC1布线 → RC2 USB路径调整 → RC3电源走线/丝印 → RC4 PH2.0 J3 → **RC5 J3黑1/红2及地铜修复**。后续RC6-TP增加8个测试点并完成静态复核，当前候选仍未制造放行；旧版原始网表、报告和哈希不改写。
- 棋盘：旧24模块PD方案、PD NVM与218器件布局已取消；当前为直接24V。保留[9月22日硬件汇报](hardware/development-handoff-2026-09-22.md)、[历史PD设计](hardware/corner-usbc-pd-24v-plan-2026-09-20.md)、[接口核查](hardware/interconnect-audit-2026-09-21.md)供追溯，历史“ORDER拒绝环”不覆盖最新闭环契约。
- 软件：[协调](firmware/development-coordination-2026-09-07.md)、[服务端早期进度](firmware/server-development-progress-2026-09-07.md)、[玩家早期进度](firmware/player-console-development-progress-2026-09-07.md)、[格子早期进度](firmware/tile-module-development-progress-2026-09-07.md)、[双板ORDER](firmware/tile-order-development-progress-2026-09-09.md)、[刷新](firmware/tile-display-refresh-progress-2026-09-08.md)、[显示时钟](firmware/tile-display-clock-progress-2026-09-08.md)、[早期输入](firmware/avatar-input-recovery-progress-2026-09-08.md)。历史用户确认只对应其版本/场景。
- 旧入口：[8月交接](archive/development-handoff-2026-08-28.md)、[整合前交接](archive/development-handoff-before-2026-09-27-refresh.md)、[PCB接任](hardware/pcb-design-handoff-2026-09-19.md)、[旧连接器](hardware/connector-selection-2026-09-19.md)。`design-records`、`superpowers`及ESP32测试服务端仅供沿革/回归，不替代正式Pi架构。

## 维护与发布

当前规范解释接口，实施清单规定验收，带日期记录只证明其明确范围。冲突时核对当前源码/网表、部署证据和最新交接，不能只比较文档日期。新增实现应同步这里及对应规范，不继续堆叠多个互相矛盾的“当前版本”。

构建、测试、部署、功能、性能、视觉和电气分别记录；DRC或串口正常不代替实物验收。供应库存注明查询日期，下单前重查。凭据、固件私有配置和本地备份不提交。当前无自动续派定时任务。

本轮范围及核对见[2026-10-15晚间整合](documentation-refresh-2026-10-15-evening.md)；[同日早期整合](documentation-refresh-2026-10-15.md)、[9月整合](documentation-refresh-2026-09-27.md)保留对应时点。
