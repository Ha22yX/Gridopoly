# 固件文档入口

- [Raspberry Pi 5 权威服务端](raspberry-pi-server.md)
- [玩家屏 Wi-Fi/UDP 协议](wifi-udp-player-protocol.md)
- [头像组件流与逐像素同源合成协议](avatar-component-protocol.md)
- [ESP32-S3 测试游戏服务端](test-game-server.md)
- [测试服务端与玩家屏幕 ESP-NOW 协议 v1](test-game-server-espnow-protocol.md)
- [玩家详情按需查询协议](player-detail-query-protocol.md)
- [双向交易按需协议](trade-protocol.md)
- [玩家控制屏幕 ESP-NOW 接入与全量同步](../player-console/espnow-integration.md)
- [正式主控与玩家终端协议](main-controller-protocol.md)
- [固件开发指南](firmware-development-guide.md)
- [生成的领域事件表](generated/domain-events-v1.md)

正式玩家控制屏联调采用树莓派 Wi-Fi/UDP 协议；ESP-NOW 协议与 ESP32-S3 测试服务端继续作为硬件回归和离线回退。两种传输共用纯 C++ 游戏核心、协议 codec、reducer 和动作幂等语义。
