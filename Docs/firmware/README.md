# 固件与服务端文档入口

更新：2026-10-15晚间。先读[当前交接](../CURRENT-DEVELOPMENT-HANDOFF.md)和[设备台账](current-device-connections.md)。源码、构建、部署、功能、视觉、性能与电气分别记录。

| 工程 | 已交付及当前运行 | 仍需验证 |
| --- | --- | --- |
| Firmware/TileModule | normal V0.34、显式long/corner、Wi-Fi/HTTP、ORDER、新鲜RFID/配对屏灯、V6 LED时序；16板在线/reader stable/闭环映射已验 | 三块长边灯效、完整图文、锚点/物理断环、多Tag相邻线圈及功耗；RS485禁用 |
| Firmware/PlayerConsole | core1/mode3、完整MAC、配对/外貌/姓名/终局、输入批处理与焦点/等待页优化 | COM29普通12MHz部署，精确输入、动态页面与≥24FPS/42ms |
| 玩家电池扩展 | SC18IM704/MAX17048 UART与状态条源码/两种候选构建通过 | 四屏BatteryBoard=none；启用候选和真实UART/SOC/充电/欠压未验 |
| Server/RaspberryPi | 16/24/32/40全在线环与实际四角、1～4屏补四席、逐人Tag/身份/规则/持久化；USB热点及自启已部署 | 四真人完整实体流程、异常恢复、36台和离线冷上电 |
| Firmware/libraries | 破产推进、楼件库存、整组卖房、交易、配对/到达协议 | roundNumber仍uint8；不宣称机器人自动谈判或全部长局终结 |
| Firmware/TestGameServer | ESP32/ESP-NOW历史测试服务及共享网页资源 | 不替代正式Pi服务 |

## 发布与设备

最新冻结包为 **trial16-2026-10-15-v4**：V0.34 long/corner、普通player `dfc4b7e5…` /16MHz、Pi `ea6e1f51…`。COM7实际仍`22ca08e6…`；COM27/28为dfc4；COM29为12MHz诊断`716ff623…`，日志已结束、静止等待页正常，普通`3da4c493…`已构建未部署。**v4不含COM29的12MHz修复或USB热点部署配置。**

- [发布包、完整SHA和验收](trial16-release-2026-10-15.md)、[批量工具](../../Tools/release/README.md)：VERIFIED是写验，不是实体游戏通过；原厂分区需先备份迁移。
- [全在线环实现](server-all-online-ring-progress-2026-10-15.md)、[16板分批部署](tile-16-trial-progress-2026-10-15.md)、[重开局恢复观察](newgame16-assignment-observation-2026-10-15.md)。
- [玩家输入](player-16-trial-progress-2026-10-15.md)、[两屏等待页](player-new-screen-display-progress-2026-10-15.md)、[第四屏](player-fourth-screen-flash-2026-10-15.md)、[COM29 12MHz](player-com29-display-progress-2026-10-15.md)。
- [围屏RFID诊断](tile-rfid-screen-coupling-2026-10-15.md)：COM8额外诊断板与计划16块normal V0.34分开；16板通过抬高线圈恢复识别，不降低阈值。

## 当前协议与开发

[路线图](development-roadmap-2026-09-21.md)、[格子指南](firmware-development-guide.md)、[功耗](module-power-firmware.md)、[GPIO](../hardware/esp32-s3-pin-map.md)、[环/配对契约](ring-onboarding-contract-2026-09-26.md)、[ORDER V1](tile-order-protocol.md)、[分配/Tag/到达](tile-module-debug-assignment.md)。单模块/开链配对不要求闭环；GPIO13 NC，角色来自profile，PD/NVM取消。

服务端：[Pi运行与部署](raspberry-pi-server.md)、[Wi-Fi/UDP](wifi-udp-player-protocol.md)、[头像](avatar-component-protocol.md)、[详情](player-detail-query-protocol.md)、[交易](trade-protocol.md)、[领域事件](generated/domain-events-v1.md)。

玩家：[当前实现](../player-console/current-player-console-baseline.md)、[头像/姓名](../player-console/player-avatar-setup-spec.md)、[UI](../player-console/player-console-ui-spec.md)、[验收](../player-console/player-console-acceptance-tests.md)、[手写](player-console-handwriting-neural.md)、[电池固件](player-battery-telemetry-progress-2026-09-27.md)。

网络：[USB已部署及自启验收](usb-ap-deployment-2026-10-15.md)、[已购网卡来源](portable-wifi-selection-2026-10-15.md)、[旧7台调查](ap-capacity-investigation-2026-10-15.md)、[以太网AP备用方案](external-ap-deployment-2026-09-26.md)。目前US/信道6/HT20、gpusb0、游戏地址10.42.0.1；20台同时注册、USB恢复及Pi软件重启通过，36台与离线冷上电仍待验。

## 历史与协作

[9月批量记录](batch32-game-release-2026-09-26.md)、[协调沿革](development-coordination-2026-09-07.md)、[双板ORDER](tile-order-development-progress-2026-09-09.md)、[旧输入修复](player-input-handwriting-fix-2026-09-26.md)保留对应设备/版本范围；旧core0性能、单板40MHz与有限RFID矩阵不推广到新设备。

[ESP32服务端](test-game-server.md)、[ESP-NOW协议](test-game-server-espnow-protocol.md)、[玩家ESP-NOW回退](../player-console/espnow-integration.md)、[旧WebSocket/JSON](main-controller-protocol.md)为历史/回归入口。主会话统一Git提交，当前没有自动续派定时任务。
