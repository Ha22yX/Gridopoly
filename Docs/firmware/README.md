# 固件与服务端文档入口

更新：2026-09-27。先读[当前交接](../CURRENT-DEVELOPMENT-HANDOFF.md)与[设备台账](current-device-connections.md)。源码、候选构建、实际部署和实物验收分别记录。

| 工程 | 当前实现 | 当前缺口 |
| --- | --- | --- |
| Firmware/TileModule | V0.33；long/corner/unknown profile；Wi-Fi/HTTP、ORDER直邻信标、RFID新鲜扫描及配对屏/灯 | 32实体环、批量显示/RFID、板级功耗策略；RS485未启用 |
| Firmware/PlayerConsole | LVGL/RGB core1、完整MAC身份、配对/外貌/姓名、终局；旋钮可信定位点与手写修复 | 最新旋钮实机反馈、频闪/错位原场景、Retarget长帧 |
| 玩家电池扩展 | SC18IM704/MAX17048非阻塞UART及独立状态条，默认关闭 | 两种普通构建已通过；未部署，等待实物底板 |
| Server/RaspberryPi | 32环/角落约束、1～4屏冻结并补机器人、16/24/32/40建局配对、规则与存档 | 四屏、多reader、36客户端、真实整局/恢复 |
| Firmware/libraries | GridopolyCore / GridopolyProtocol，破产/库存/整组卖房与配对消息 | 保持旧协议/存档兼容；roundNumber仍uint8 |
| Firmware/TestGameServer | ESP32/ESP-NOW历史回归服务 | 不作为当前正式主控 |

## 当前发布与跨端契约

- [批量开发/部署记录](batch32-game-release-2026-09-26.md)、[批量打包和烧录](../../Tools/release/README.md)：最新冻结包v4，不能等同当前源码HEAD。
- [32环、自动席位与配对契约](ring-onboarding-contract-2026-09-26.md)：仅32格完整环自动映射，非环形单模块也能配对。
- [ORDER V1波形/新鲜度](tile-order-protocol.md)、[格子分配/Tag/到达](tile-module-debug-assignment.md)。
- [新局地图兼容与单reader记录](newgame-open-chain-fix-2026-09-26.md)、[服务端集成验收](server-onboarding-validation-2026-09-26.md)。
- [旋钮/手写修复与部署](player-input-handwriting-fix-2026-09-26.md)、[电池遥测固件](player-battery-telemetry-progress-2026-09-27.md)。

## 开发与验收入口

[实施路线图](development-roadmap-2026-09-21.md)、[两种棋盘PCB](../hardware/pcb-variants.md)、[GPIO](../hardware/esp32-s3-pin-map.md)、[格子指南](firmware-development-guide.md)、[格子工程](../../Firmware/TileModule/README.md)、[功耗开发](module-power-firmware.md)、[供电验收](../hardware/power-system-acceptance.md)。板型profile已实现，功耗限制和整盘电气仍待完成；PD电路/NVM取消，GPIO13 NC。

正式服务：[Pi部署](raspberry-pi-server.md)、[UDP](wifi-udp-player-protocol.md)、[头像](avatar-component-protocol.md)、[详情](player-detail-query-protocol.md)、[交易](trade-protocol.md)、[领域事件](generated/domain-events-v1.md)。

玩家：[当前实现](../player-console/current-player-console-baseline.md)、[头像/姓名规格](../player-console/player-avatar-setup-spec.md)、[UI](../player-console/player-console-ui-spec.md)、[验收](../player-console/player-console-acceptance-tests.md)、[电池接口](../hardware/player-battery-telemetry-interface-2026-09-27.md)。

网络：[外置AP可选迁移](external-ap-deployment-2026-09-26.md)，未在现场执行，不能凭DHCP地址池判断无线容量。

## 历史与协作

[协调记录](development-coordination-2026-09-07.md)、[服务端早期进度](server-development-progress-2026-09-07.md)、[玩家早期进度](player-console-development-progress-2026-09-07.md)、[双板ORDER记录](tile-order-development-progress-2026-09-09.md)保留当时证据；其“当前版本”不覆盖本页和设备台账。

[ESP32测试服务端](test-game-server.md)、[ESP-NOW协议](test-game-server-espnow-protocol.md)、[玩家ESP-NOW回退](../player-console/espnow-integration.md)、[旧WebSocket/JSON](main-controller-protocol.md)仅为历史/回归入口。没有自动续派定时任务。
