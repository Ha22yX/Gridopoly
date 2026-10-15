<h1 align="center">Gridopoly</h1>

<p align="center">
  一个模块化电子桌游平台：由智能格子、玩家控制屏幕、实体棋子和 Raspberry Pi 中控服务器组成。
</p>

<p align="center">
  <a href="README.md">English</a> &middot;
  <a href="#项目图集">项目图集</a> &middot;
  <a href="#当前进度">当前进度</a> &middot;
  <a href="#快速开始">快速开始</a> &middot;
  <a href="Docs/README.md">开发文档</a> &middot;
  <a href="Server/RaspberryPi/README.md">树莓派服务器</a>
</p>

<p align="center">
  <img alt="状态" src="https://img.shields.io/badge/status-integrated%20prototype-D97706?style=for-the-badge" />
  <img alt="格子主控" src="https://img.shields.io/badge/tile-ESP32--S3-205A4B?style=for-the-badge" />
  <img alt="中控" src="https://img.shields.io/badge/authority-Raspberry%20Pi%205-A22846?style=for-the-badge&logo=raspberrypi&logoColor=white" />
  <img alt="玩家屏幕" src="https://img.shields.io/badge/player%20console-480x480%20round%20LCD-6B7FD7?style=for-the-badge" />
  <img alt="总线" src="https://img.shields.io/badge/link-WiFi%20%2B%20ORDER-2563EB?style=for-the-badge" />
</p>

## 项目图集

<table>
  <tr>
    <td width="50%" align="center">
      <img src=".github/assets/gridopoly-tile-pcb.png" alt="Gridopoly 格子模块 PCB 设计" />
      <br />
      <strong>格子模块 PCB 设计。</strong> 单个可重排智能格子集成 ESP32-S3、屏幕、RFID、RS485、24 V 供电、电流检测和边缘连接触点。
    </td>
    <td width="50%" align="center">
      <img src=".github/assets/gridopoly-player-console.png" alt="Gridopoly 玩家控制屏幕设计" />
      <br />
      <strong>玩家控制屏幕。</strong> 480x480 圆形终端，用于显示回合、现金、资产、骰子、交易和游戏提示。
    </td>
  </tr>
  <tr>
    <td width="50%" align="center">
      <img src=".github/assets/gridopoly-tile-module.png" alt="Gridopoly 格子模块实体展示" />
      <br />
      <strong>格子模块实体展示。</strong> 已经进入实物调试阶段的智能格子，包含屏幕、USB-C、侧边触点、LED 和板级验证硬件。
    </td>
    <td width="50%" align="center">
      <img src=".github/assets/gridopoly-raspberry-pi.png" alt="作为 Gridopoly 游戏中控服务器的 Raspberry Pi 5" />
      <br />
      <strong>树莓派游戏中控服务器。</strong> 负责规则、账号、棋盘状态、网页界面、玩家同步和持久化。
    </td>
  </tr>
</table>

## 项目动机

Gridopoly 想探索一件很有意思的事：如果桌游的棋盘本身是可编程的，实体桌游会变成什么样？

它不是一张固定印刷的纸板，而是由一块块独立智能格子组成。每个格子都能显示自己的状态、识别实体棋子、驱动灯效，并且通过Wi-Fi/HTTP与游戏中控通信。玩家仍然围在真实桌面前移动棋子，但现金、资产、租金、交易、事件和地图内容都由软件系统实时管理。

这个项目不是复刻任何现有商业桌游的品牌、地图、美术、卡牌或产品外观。它保留实体桌游面对面互动的感觉，同时把纸币结算、租金计算、固定棋盘和手动规则执行，升级成由硬件和软件共同驱动的原创 Grid City 游戏系统。

## 当前进度

更新于2026-10-15。当前目标为 **12长边＋4角落的16格试玩、最多4台玩家屏**。项目已有集成实现与部署证据，完整实体游戏和制造放行仍待验收。

- 计划16板均已完成V0.34分批写验和正常运行，修正WS2812B-B-V6时序；13板灯效获确认，3板目视待补。正常24V下RFID和完整实体环尚待验。
- Pi现支持全在线16/24/32/40格ORDER环：全部在线模块必须属于同一有效环，数量等于地图且实际四角角色匹配；一致手动锚点重派全环跟随，开链调试仍可用。RS485保持禁用。
- 已发现4台在线玩家屏。新游戏冻结1～4真人并补足四席，逐人新鲜Tag配对后设置外貌姓名；单reader和非环形场景可用。新增屏等下一局加入，不抢占现有席位。
- 玩家输入批处理、积压短按保护和焦点局部刷新已实现。COM27/28的等待页重绘修复获用户确认；COM29需另降至12MHz，当前诊断等待页正常、普通候选未部署；COM7仍保留前版。精确旋钮、Avatar/游戏视觉和Retarget长帧仍待验。
- 最新冻结包 **trial16-2026-10-15-v4** 包含V0.34 long/corner、普通player `dfc4b7e5…`和Pi `ea6e1f51…`，不要与9月batch32-v4混淆；电池遥测默认关闭、未部署。
- 棋盘采用直接24V，独立玩家供电板当前设计候选为 **R1 RC6-TP**，新增8个测试点。四块在用底板返修已获用户确认，充电/欠压/温升和制造验收仍未完成。
- 3D已交付R2三模块一体底板，几何检查通过、打印试配待验；旧CW47.5玩家支架获用户装配确认，新版仍在开发。

详情见[当前交接](Docs/CURRENT-DEVELOPMENT-HANDOFF.md)、[设备台账](Docs/firmware/current-device-connections.md)、[16格发布与验收](Docs/firmware/trial16-release-2026-10-15.md)和[机械进展](Docs/mechanical/README.md)。编译、烧录和局部页面目视通过均不等于整盘游戏、网络容量或电气验收。

## 系统架构

```mermaid
flowchart TB
    Pi["Raspberry Pi 5 中控<br/>规则、回合、账号、持久化"]
    Web["Web 棋盘 / 测试 UI"]
    Console["玩家圆形屏幕<br/>Wi-Fi/UDP 操作与快照"]
    Bus["24 V + ORDER；RS485预留"]
    TileA["智能格子 A<br/>ESP32-S3 / LCD / LED / RFID"]
    TileB["智能格子 B"]
    TileN["智能格子 N"]
    Piece["实体棋子<br/>125 kHz RFID 标签"]

    Pi <--> Web
    Pi <-->|"WPA2 / HMAC UDP / HTTP assets"| Console
    Power["24 V DC input"] --> Bus
    Pi <-->|"Wi-Fi / HTTP"| TileA
    Bus --> TileA
    TileA --> TileB
    TileB --> TileN
    Piece -. 放置在格子上 .-> TileA
```

## 核心能力

- **可重排智能格子**：每个格子模块都可以独立制造、测试、更换和重新排列。
- **实体棋子识别**：每个格子围绕 125 kHz RFID 设计，用于识别带标签的棋子。
- **动态格子显示**：ST7789 屏幕和 WS2812 灯效显示归属、事件、状态和反馈。
- **中央规则中控**：Raspberry Pi 负责规则、回合、现金、资产、交易、拍卖、债务和持久化。
- **玩家终端**：圆形控制屏给每个玩家独立的现金、资产、骰子、交易、付款和重连界面。
- **原创 Grid City 内容**：仓库包含棋盘地图、视觉系统、格子素材、头像、合约和游戏规则。
- **协议优先设计**：C++ 核心、二进制协议、UDP 包络、HTTP 资源和生成合约都作为一等工程资产进行测试。

## 技术栈

| 层级 | 技术 | 用途 |
| --- | --- | --- |
| 格子硬件 | ESP32-S3、ST7789、WS2812、HTRC110、RS485、INA226 | 本地显示、灯效、RFID 识别、Wi-Fi/HTTP通信、预留RS485和电流监测。 |
| 玩家控制屏 | ESP32-S3、480x480 圆形 LCD、面向 LVGL 的 UI 规格、Wi-Fi/UDP | 玩家操作、状态快照、动作提交、重连和交互流程。 |
| 中控服务器 | Raspberry Pi 5、C++17、systemd | 规则、持久化、HTTP 棋盘界面、UDP 玩家会话和部署。 |
| 游戏核心 | C++ libraries | 棋盘目录、游戏引擎、协议编解码、状态机、交易、拍卖、卡牌和机器人。 |
| 素材/合约 | JSON schemas、生成文档、PNG/RGB565/GAVC 资源 | 稳定游戏内容、格子美术、头像和协议合约。 |
| 硬件设计 | EasyEDA快照；棋盘六层、玩家电池底板双层 | 智能格子 PCB、BOM、供电、总线和连接器文档。 |

## 快速开始

构建并运行主机侧 C++ 测试：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

使用原生保护脚本构建并测试 Raspberry Pi 服务器：

```bash
GRIDOPOLY_NATIVE_BUILD_DIR=/tmp/gridopoly-build \
  Server/RaspberryPi/tools/build-and-test-native.sh
```

构建服务器二进制后，在 Raspberry Pi 上部署：

```bash
sudo sh Server/RaspberryPi/deploy/install.sh build-pi/gridopoly_server
sudoedit /etc/gridopoly/server.env
sudoedit /etc/gridopoly/ap.env
sudo systemctl enable --now gridopoly-ap gridopoly-dnsmasq gridopoly-ap-watchdog gridopoly
```

最后记录的管理网地址为`http://10.0.0.124/`，可能随网络改变；下列AP入口属于设备专网。[外置AP可选迁移](Docs/firmware/external-ap-deployment-2026-09-26.md)已有工具，尚未在现场切换。

部署后的玩家网络入口：

- 玩家 AP 上的 Web 棋盘：`http://10.42.0.1/`
- 玩家二进制协议：UDP `10.42.0.1:4242`

## 文档导航

按工作区域从这里开始：

- [文档中心](Docs/README.md)
- [产品目标](Docs/product/product-goals.md)
- [硬件基线](Docs/hardware/hardware-baseline.md)
- [ESP32-S3 引脚图](Docs/hardware/esp32-s3-pin-map.md)
- [模块互连](Docs/hardware/module-interconnect-5wire.md)
- [固件开发指南](Docs/firmware/firmware-development-guide.md)
- [Raspberry Pi 中控服务器](Docs/firmware/raspberry-pi-server.md)
- [Wi-Fi/UDP 玩家协议](Docs/firmware/wifi-udp-player-protocol.md)
- [头像组件流](Docs/firmware/avatar-component-protocol.md)
- [玩家详情查询](Docs/firmware/player-detail-query-protocol.md)
- [交易协议](Docs/firmware/trade-protocol.md)
- [玩家控制屏 UI 规格](Docs/player-console/player-console-ui-spec.md)
- [游戏规则](Docs/game/game-rules.md)

## 仓库结构

```text
Gridopoly/
|-- Assets/                  Grid City 格子美术、头像、清单和生成资源
|-- Docs/                    产品、硬件、固件、游戏、玩家屏幕和设计文档
|-- Firmware/                ESP32 测试服务器、共享核心/协议库、LVGL 基线
|-- GameData/                Schema、生成合约和游戏数据产物
|-- Mechanical/              玩家控制屏支架模型、脚本、测试和报告
|-- PCB Files/               EasyEDA 项目快照和 PCB 备份
|-- Server/RaspberryPi/      正式中控服务器、部署脚本、Web/UDP 服务
|-- Tools/                   合约生成工具和测试
|-- tests/host/              核心、协议、中控、UDP、HTTP、头像等主机测试
`-- CMakeLists.txt
```

## 安全与发布说明

Gridopoly 是一个集成原型，不是量产发布版本。在把任何板卡版本视为生产就绪之前，还需要完成最终 DRC、无飞线检查、元件方向复核、开关稳压器布局复核、电流路径计算、RFID 调谐和实测多模块联调。

目前仓库还没有选择开源许可证。公开代码和设计文件可供查看，但并不自动授权复用、修改或商业使用。

2026-10-15后续：用户已给16板接24V，但内置热点实测仅7台活跃关联，阻塞完整整盘。已购买ALFA AWUS036ACM作为便携热点，尚未到货验证或迁移；[采购与20/36台验收边界](Docs/firmware/portable-wifi-selection-2026-10-15.md)。
