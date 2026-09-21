# 固件与服务端文档入口

[软件与固件后续需求/验收](development-roadmap-2026-09-21.md)按格子、服务端、玩家屏和拓扑列出实施要求。

更新：2026-09-21。项目状态见[当前交接](../CURRENT-DEVELOPMENT-HANDOFF.md)，下列工程必须区分源码、构建、已部署与实机验收。

| 工程 | 当前技术/职责 | 下一阶段 |
| --- | --- | --- |
| Firmware/TileModule | PlatformIO + Arduino，V0.32；Wi-Fi/HTTP、显示、RFID、LED、INA226、ORDER物理信标 | 长边/角落板型、PD维护固件、功耗预算、全盘/拓扑验证 |
| Firmware/PlayerConsole | Arduino构建、LVGL与RGB圆屏、输入、认证UDP及HTTP素材 | core1下长帧优化，正常游戏视觉确认与回归 |
| Server/RaspberryPi | C++权威服务、HTTP/UDP、游戏/持久化、模块映射与ORDER锚点 | 新能力诊断、全盘恢复/容量、功率估算与拓扑产品化 |
| Firmware/libraries | GridopolyCore / GridopolyProtocol | 跨端契约变更统一版本化，保留兼容测试 |
| Firmware/TestGameServer | ESP32/ESP-NOW回归服务 | 历史/回退用途，不作为正式主控 |

## 板级与格子开发

- [两种PCB设计](../hardware/pcb-variants.md)、[GPIO接口](../hardware/esp32-s3-pin-map.md)、[格子固件指南](firmware-development-guide.md)。
- [PD固件与功耗要求（待实现）](corner-pd-firmware.md)、[电源验收](../hardware/power-system-acceptance.md)。
- [ORDER V1](tile-order-protocol.md)、[分配/Tag/自动到达](tile-module-debug-assignment.md)。
- [格子工程实际构建说明](../../Firmware/TileModule/README.md)、[ORDER双板记录](tile-order-development-progress-2026-09-09.md)。

两种棋盘PCB复用格子业务，角落才具有STUSB4500；玩家圆屏是第三类设备，不复用这些针脚。现有V0.32还没有新PD配置逻辑。

## 正式服务与玩家链路

1. [Raspberry Pi部署](raspberry-pi-server.md)
2. [Wi-Fi/UDP认证协议](wifi-udp-player-protocol.md)
3. [头像组件](avatar-component-protocol.md)
4. [玩家详情](player-detail-query-protocol.md)
5. [双向交易](trade-protocol.md)
6. [领域事件表](generated/domain-events-v1.md)
7. [玩家终端基线](../player-console/current-player-console-baseline.md)

## 交接与验收

[协调记录](development-coordination-2026-09-07.md)、[服务端](server-development-progress-2026-09-07.md)、[玩家](player-console-development-progress-2026-09-07.md)、[设备](current-device-connections.md)。设备口/二进制哈希是最后已知记录，不能代替再次部署前识别。

## 回退与历史

[ESP32测试服务端](test-game-server.md)、[ESP-NOW协议](test-game-server-espnow-protocol.md)、[玩家ESP-NOW回退](../player-console/espnow-integration.md)、[旧WebSocket/JSON](main-controller-protocol.md)。回退链路没有当前完整Identity/Avatar/交易能力，旧RS485令牌提案也不是已部署ORDER V1。
