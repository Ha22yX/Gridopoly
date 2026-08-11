# Gridopoly ESP32-S3 测试服务端

这是玩家控制屏幕开发阶段使用的临时权威服务端。它同时连接现有 Wi-Fi、运行测试网站，并通过 ESP-NOW 与 1～6 块玩家屏幕通信；没有创建 Wi-Fi AP。

控制器硬规则：只有 `Bot` 可以自动决策。任何 `PlayerConsole` 席位都会在购买、移动确认、人工筹款、拍卖和结束回合阶段等待该玩家屏幕指令，断线也不会自动托管。

## 快速入口

- [部署、模块与网页 API](../../Docs/firmware/test-game-server.md)
- [ESP-NOW 玩家屏幕协议 v1](../../Docs/firmware/test-game-server-espnow-protocol.md)
- [共享协议 C++ 头文件](../libraries/GridopolyProtocol/src/gridopoly/protocol/Protocol.h)
- [玩家屏幕会话交接清单](../../Docs/firmware/test-game-server-espnow-protocol.md#玩家控制屏幕实现清单)

玩家控制屏幕开发会话：`019fbb98-c4f6-7e20-9ccc-5f6dc5b00413`。

## 编译与烧录

在仓库根目录运行：

```powershell
.\Firmware\TestGameServer\tools\compile.ps1
.\Firmware\TestGameServer\tools\verify-build.ps1
.\Firmware\TestGameServer\tools\upload.ps1 -Port COM5
.\Firmware\TestGameServer\tools\smoke-com5.ps1 -Port COM5
node .\Firmware\TestGameServer\tools\stress-http.mjs http://设备IP 180
node .\Firmware\TestGameServer\tools\fault-http-idle.mjs http://设备IP 8
```

脚本固定使用 ESP32-S3、16MB Quad Flash、8MB OPI PSRAM、UART0 和自定义 16MB 分区；烧录脚本不会自动选择其他串口。

## 本地秘密配置

把 `config/secrets.example.h` 复制为 `config/secrets.local.h`，填写 Wi-Fi 和测试 PSK。真实文件已被 `.gitignore` 排除，固件与串口日志也不会输出 SSID、密码或 PSK。

测试网站优先使用 `http://gridopoly-test.local/`。如果电脑的代理软件拦截 `.local`，请从串口 `GRIDOPOLY_WIFI` 行读取 DHCP 地址。
