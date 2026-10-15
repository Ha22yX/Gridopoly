# 固件与服务端文档入口

更新：2026-10-15。先读[当前交接](../CURRENT-DEVELOPMENT-HANDOFF.md)和[设备台账](current-device-connections.md)。源码、构建、部署、视觉、性能与电气验收分别记录。

| 工程 | 已交付实现 | 当前实物缺口 |
| --- | --- | --- |
| Firmware/TileModule | V0.34；显式long/corner；Wi-Fi/HTTP、ORDER直邻、新鲜RFID与配对屏灯，V6 LED时序修复 | 16板已分批运行；正常24V下全环、RFID、图文与整盘功耗待验，RS485未启用 |
| Firmware/PlayerConsole | core1/mode3；完整MAC身份、配对/外貌/姓名/终局，输入批处理与等待页保留渲染 | 四屏在线记录；COM27/28等待页正常，COM29的12MHz诊断等待页正常、普通候选未部署；精确旋钮、Avatar/游戏视觉及Retarget长帧待验 |
| 玩家电池扩展 | SC18IM704/MAX17048 UART及独立状态条，两种候选构建通过 | 默认关闭且未部署；实物UART、电量、充电/欠压待验 |
| Server/RaspberryPi | 全在线16/24/32/40完整环与实际四角，1～4屏冻结补足四席，逐人Tag、规则和持久化 | 四屏/多reader完整流程、16实体全环、整局与容量恢复待验 |
| Firmware/libraries | Core/Protocol：破产推进、建筑库存、整组卖房、配对/到达消息 | 维护旧存档兼容；roundNumber仍uint8 |
| Firmware/TestGameServer | ESP32/ESP-NOW历史测试服务 | 不替代正式Pi服务 |

## 发布与运行

当前包为 **`trial16-2026-10-15-v4`**，含V0.34 long/corner、普通player `dfc4b7e5…`及Pi `ea6e1f51…`；12长边/4角落/最多4屏。旧`batch32-2026-09-26-v4`是另一日期的历史包，不能只用“v4”区分。

- [本轮16格包、完整SHA和验收](trial16-release-2026-10-15.md)、[批量工具](../../Tools/release/README.md)。VERIFIED表示写验，不等于实物游戏通过；原厂分区不匹配需先备份迁移。
- [全在线闭环服务端](server-all-online-ring-progress-2026-10-15.md)、[16板分批部署](tile-16-trial-progress-2026-10-15.md)。
- [玩家输入修复](player-16-trial-progress-2026-10-15.md)、[等待页显示修复](player-new-screen-display-progress-2026-10-15.md)、[新增两屏](player-two-screen-flash-2026-10-15.md)、[第四屏](player-fourth-screen-flash-2026-10-15.md)。
- [当前拓扑与配对契约](ring-onboarding-contract-2026-09-26.md)、[ORDER V1](tile-order-protocol.md)、[格子分配/Tag/到达](tile-module-debug-assignment.md)。单模块/开链配对不要求完整环。

## 规范与开发

[路线图](development-roadmap-2026-09-21.md)、[格子开发指南](firmware-development-guide.md)、[格子工程](../../Firmware/TileModule/README.md)、[功耗要求](module-power-firmware.md)、[GPIO](../hardware/esp32-s3-pin-map.md)。GPIO13 NC，PD/NVM工作已取消。

服务端：[Pi部署](raspberry-pi-server.md)、[Wi-Fi/UDP](wifi-udp-player-protocol.md)、[头像](avatar-component-protocol.md)、[详情](player-detail-query-protocol.md)、[交易](trade-protocol.md)、[领域事件](generated/domain-events-v1.md)。

玩家：[当前实现](../player-console/current-player-console-baseline.md)、[头像/姓名](../player-console/player-avatar-setup-spec.md)、[UI](../player-console/player-console-ui-spec.md)、[验收](../player-console/player-console-acceptance-tests.md)、[手写](player-console-handwriting-neural.md)、[电池固件](player-battery-telemetry-progress-2026-09-27.md)。

网络：[外置AP方案](external-ap-deployment-2026-09-26.md)未迁移现场；AP认证恢复提案和未提交脚本不能写成生产部署，DHCP地址池大小不证明无线容量。

## 历史与协作

[9月批量记录](batch32-game-release-2026-09-26.md)、[协调沿革](development-coordination-2026-09-07.md)、[双板ORDER](tile-order-development-progress-2026-09-09.md)、[手写/旋钮旧修复](player-input-handwriting-fix-2026-09-26.md)保留对应版本证据。旧core0性能、8MHz/40MHz屏幕结果及有限RFID矩阵不能扩展到新板和全部场景。

[ESP32服务端](test-game-server.md)、[ESP-NOW协议](test-game-server-espnow-protocol.md)、[玩家ESP-NOW回退](../player-console/espnow-integration.md)、[旧WebSocket/JSON](main-controller-protocol.md)均为历史/回归入口。主会话统一提交；没有自动续派定时任务。

本日后续：[COM29显示诊断](player-com29-display-progress-2026-10-15.md)、[热点7关联调查](ap-capacity-investigation-2026-10-15.md)、[已购USB网卡与便携目标](portable-wifi-selection-2026-10-15.md)。24V已接入；普通12MHz候选尚未部署，v4不含此时钟修复；当前容量仍阻塞整盘。
