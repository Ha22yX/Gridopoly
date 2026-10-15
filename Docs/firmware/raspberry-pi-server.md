# Raspberry Pi 5 权威服务端

## 2026-10-15 当前版本与流程

Pi 已部署源码检查点 `0fed0ce` 的 binary `ea6e1f517c597cc69f6f0724af0d501206cf3d57e457945c9cc138fc3f2d118d`，支持全部在线集合匹配的 16/24/32/40 四角环、自动四席及逐人 Tag 配对。[服务端部署记录](server-all-online-ring-progress-2026-10-15.md)保留构建、回滚和存档核验证据；USB 迁移及随后观察没有更新游戏程序或玩家/格子固件。

ALFA AWUS036ACM 已作为独立 USB 热点部署：固定接口 `gpusb0`、2.4 GHz、US／信道 6／HT20，SSID `gridopoly`、原密码及游戏地址 `10.42.0.1` 保持。板载 `wlan0` 保留管理，最近地址 `10.0.0.124`。2026-10-15 22:07:58、22:18:49 UTC 两个快照均为 16 格＋4 屏、20 台同时关联并注册；用户关闭 COM7 后，重启后的连续观察按实际开启的 16 格＋指定 3 屏验收。20 台证据不等于 36 台容量或四真人完整游戏通过，见[USB 部署与启动验收](usb-ap-deployment-2026-10-15.md)。

22:19 UTC 实际 Pi 软件重启后，四服务自动恢复且 enabled/active，原房间 `993580115` 保持；独立 121.18 秒检查中，16 个 reader stable、唯一 16 节点 ORDER 环 ready、0～15 自动映射及四角 0/4/8/12 持续满足。设备事件恢复也已通过定向驱动解绑/重绑验证。完全断电、没有家庭网络的冷上电及 32 格＋4 屏＝36 台仍待实测。

22:29～22:34 UTC 用户再次新建 16 格房间 `993580116`，150 组观察映射保持，独立 30 秒 230 次真实心跳响应全部 `assigned=true`，用户确认正常。首次部分屏 `WAITING FOR TILE` 的原因未定，本轮没有代码修复或部署；上下文并发窗口另列待复现，见[重开局观察](newgame16-assignment-observation-2026-10-15.md)。当前设备数、房间和版本均为带时间的记录，以[设备台账](current-device-connections.md)和[当前交接](../CURRENT-DEVELOPMENT-HANDOFF.md)为入口。

此前 11:21～11:25 UTC 的板载热点七台限制属于[迁移前调查](ap-capacity-investigation-2026-10-15.md)，已不再阻塞当前 16 格同时上线；不能推广为所有 Pi 的硬上限。用户已接 24V，线圈略高于 LCD 后确认 16 板可读 Tag；配对、自动到达、供电与实体整局仍分别验收。

网页主入口`POST /api/new?size=32&autoPlayers=1`，也接受16/24/40；冻结1～4台合格在线屏并补机器人至4席。`GET /api/player-devices`负责发现，`GET /api/tag-pairing`及版本化Retry/Cancel负责逐人配对。单块未分配reader/开放链可配对，不要求完整环；自动地图映射则要求全部当前在线模块恰好属于同一个匹配地图格数的环，4 个 corner 与实际地图四角一致，其余为 long。

TagPairing在Identity准备期进行，绑定成功即可并行外貌姓名，全部ready后五秒倒计时；成功拿回限制与冻结席位持久化，重启生成新的扫描session。元数据保留旧前缀并追加ONB1/PND1，旧存档兼容；完整字段与二进制布局只在[跨端契约](ring-onboarding-contract-2026-09-26.md)维护。

[服务端集成证据](server-onboarding-validation-2026-09-26.md)、[新局地图修复](newgame-map-compatibility-2026-09-26.md)、[设备台账](current-device-connections.md)、[外置AP可选方案](external-ap-deployment-2026-09-26.md)。软件回归及单reader实机成功不等于32模块/4屏/36客户端验收。

状态：当前正式实现、部署与验收基线

目标系统：Raspberry Pi OS Lite 64-bit

最后更新：2026-10-15（本轮仅文档；旧观测保留原日期）

## 1. 当前结论

`Server/RaspberryPi` 是 Gridopoly 当前唯一正式权威服务端。它直接链接
`GridopolyCore` 与 `GridopolyProtocol`，负责游戏规则、身份流程、机器人、交易、存档、
测试网页、玩家圆屏 UDP 会话和素材服务。ESP32-S3 `TestGameServer` 只保留为离线回归和
ESP-NOW 回退，不再代表正式部署架构。

当前服务器能力包括：

- 16/24/32/40 格地图，Core/codec 容量 6 席、28 项资产；网页自动模式为 1～4 真人加机器人共四席。
- 真人席位与 Bot 席位在网页建局时冻结；Bot 不能占用预留真人席位。
- 自动模式逐人 Tag 配对 → 绑定者独立 Avatar Setup → Name Setup → Ready → 统一 5 秒倒计时；身份状态和最终头像可持久化恢复。
- HMAC 认证的 Wi-Fi/UDP 玩家屏会话、动作幂等、事件补发、全量恢复和端点迁移。
- 玩家详情、双向交易和身份操作采用独立按需消息，不扩大常规快照。
- 网页棋盘、机器人间隔设置、指定玩家下一次投骰目的地、地产归属徽标和逐格移动动画。
- 格子2秒Heartbeat、15秒租约；V0.34 按完整匹配环或开放链手动锚点分配，最低空闲格自动认领仅兼容旧无ORDER模块。
- 36 张网页 PNG、36 张圆屏 RGB565 格子图、30 个中性 GAVC 头像组件和最终公共头像。
- 有界并发 HTTP、原子主状态存档、systemd 自启/自动重启和独立 `gridopoly` 玩家网络。

## 2. 运行架构

```text
家庭 Wi-Fi / wlan0（管理、可选上网）
  └─ Raspberry Pi 5
       ├─ gridopoly_server（唯一权威进程）
       ├─ AWUS036ACM / gpusb0：独立 USB 射频
       │    └─ gridopoly，2.4 GHz，10.42.0.1/24
       ├─ HTTP :80（网页、格子心跳、素材）
       ├─ 玩家协议 UDP :4242
       ├─ DHCP/DNS：10.42.0.20..200，gridopoly.local
       └─ /var/lib/gridopoly（存档与最终头像）

16 块格子和玩家圆屏
  ├─ 连接 gpusb0 提供的 WPA2 网络
  ├─ 格子 HTTP Heartbeat 与物理 ORDER；玩家认证 UDP
  └─ HTTP 按需下载素材
```

当前 USB 热点和板载管理网使用独立射频，玩家网络信道不再跟随家庭 `wlan0`。
`gpusb0` 按永久 MAC 和 `mt76x2u` 驱动持久命名，不依赖 `wlan1` 或 PHY 编号。
`.1/24` 只能属于一个接口；当前旧虚拟 `ap0` 不承载游戏网。早期板载方案的
`wlan0`＋`ap0` 共用射频/信道，保留作历史和经审核的恢复起点；旧 `eth0` 外置 AP
工具是另一条未现场部署的可选路径，不能套用到现役 USB 配置。

游戏与 AP 单元已去除家庭网络上线等待，实际软件重启时四服务约 4.55～4.58 秒启动，
早于家庭网 wait-online 约 11.73 秒结束；完全离线冷上电仍需单独实测。

## 3. 源码入口

| 文件 | 职责 |
| --- | --- |
| `Server/RaspberryPi/src/main.cpp` | 环境变量、进程生命周期、20 ms 权威 tick、HTTP/UDP 启停 |
| `AuthorityService.*` | 线程安全权威入口、机器人、网页控制、投影、交易与身份编排 |
| `UdpPlayerServer.*` | UDP 发现/配对、HMAC、重放窗口、Heartbeat、按需协议和全量同步 |
| `HttpServer.*` | 有界并发 HTTP、网页 API、素材路由与运行诊断 |
| `TileOrderTopology.h` / `TileOrderProjection.h` | 新鲜物理邻接、四角环、开放链锚点与派生冲突 |
| `TagPairingModel.h` | 单 reader 也可用的逐人配对、新鲜空场、幂等、取消/重试和拿回限制 |
| `TileDebugAssignments.*` | 进程内格子模块注册/租约、自动与手动分配、视觉派生和冻结下行 DTO |
| `FileStateStore.*` | 游戏状态 schema 4、兼容迁移和主状态原子提交 |
| `IdentityModel.*` | 冻结真人/Bot 席位、Identity 阶段、倒计时和确定性 Bot 外貌 |
| `FileIdentityStore.*` | Identity schema 1 持久化 |
| `AvatarComponentCodec.*` | GAVC 组件校验、RLE 解码和共享整数合成算法 |
| `AvatarRenderer.*` | 220×300 预览、128×128 最终头像、异步生成和原子发布 |
| `Server/RaspberryPi/deploy/` | systemd、AP、dnsmasq、watchdog 和安装器 |
| `Server/RaspberryPi/tools/` | 原生构建、素材 staging、校验、抓包和 HTTP 压测 |

共享业务代码位于：

```text
Firmware/libraries/GridopolyCore
Firmware/libraries/GridopolyProtocol
Firmware/TestGameServer/src/PlayerDetailProjection.*
```

正式 wire 以 [玩家屏 Wi-Fi/UDP 协议](wifi-udp-player-protocol.md) 为唯一入口；头像组件格式见
[头像组件流协议](avatar-component-protocol.md)，交易与玩家详情见对应按需协议。

## 4. 权威游戏与身份生命周期

### 4.1 建立新对局

网页主入口为 `POST /api/new?size=<16|24|32|40>&autoPlayers=1`：当前 1～4 台 eligible 在线屏按 deviceId 排序冻结真人，补机器人至四席；0 台或超过 4 台拒绝且不改房间。后来上线的屏幕不能替换机器人或已有真人，等下一次用户新局。

显式 `humans=<1..6>&bots=<0..5>` 保留调试兼容，总数 2..6，不是主入口。两种模式均冻结席位类型：

- 真人设备只能绑定预留真人席位。
- Bot 席位固定，不能被真人替换。
- Bot 名称固定为 `Bot 1`、`Bot 2`……；外貌由 room seed、player id 和 catalog version
  确定性生成，重连和服务重启不会改变。
- 新 room 会清除旧客户端草稿、会话幂等缓存和 Identity 生命周期。

### 4.2 真人身份流程

0. 自动模式未绑定真人先处于 `TagPairing` 席位阶段 7：新 session 下先见新鲜空场、再见新唯一 UID；绑定落盘后即可编辑，取回提示独立保留。
1. `AvatarSetup`：圆屏提交最终 recipe；服务端异步生成最终公共头像。
2. `NameSetup`：头像生成成功后提交最终姓名；名称必须是 1..16 字节有效 UTF-8，
   不得含控制字符，且忽略大小写后不得与任何真人或 Bot 重名。
3. `Ready`：`ConfirmName` 成功即完成真人 Ready，不存在第四个确认动作。
4. `Countdown`：所有必需真人的 Tag 配对、头像、姓名和生成任务完成后，只创建一次统一 5 秒 deadline。
5. `Active`：deadline 到达后进入正式游戏；倒计时中断线不会暂停或重建 deadline。

Identity 投影使用独立 `IdentitySnapshot/IdentityRequest`（`0x2A/0x29`）。Identity 非 Active
阶段，Heartbeat 完整性只要求 Identity 投影；Active 后才要求完整游戏和事件投影。
完整同步顺序为：

```text
TagPairSnapshot
-> Identity
-> 本席未完成 CardReveal（如有）
-> State
-> Authority
-> Roster
-> Trade resync
-> GameEvent 历史
```

## 5. 玩家网络与 systemd

### 5.1 四个正式单元

| 单元 | 作用 | 重启策略 |
| --- | --- | --- |
| `gridopoly.service` | 权威服务、HTTP 和 UDP | `Restart=always`，2 秒 |
| `gridopoly-ap.service` | 在已核验的 `gpusb0` 上启动 USB AP，guard 管理地址与身份 | 失败后 3 秒 |
| `gridopoly-dnsmasq.service` | 玩家网 DHCP 与本地 DNS | 失败后 2 秒 |
| `gridopoly-ap-watchdog.service` | 监测邻居、RX 停滞和 AP 自愈 | `Restart=always`，2 秒 |

现役四个单元已 enabled，并已完成实际 Pi 软件重启自启动验收。以下是新安装语义，不能据此重复覆盖现役 USB 单元。

`gridopoly.service` 安装到 `multi-user.target` 后可开机自启动，但
`deploy/install.sh` **只安装文件并执行 `daemon-reload`，不会自动 enable/start**。
完成本地配置后必须显式执行：

```bash
sudo systemctl enable --now \
  gridopoly-ap.service \
  gridopoly-dnsmasq.service \
  gridopoly-ap-watchdog.service \
  gridopoly.service
```

不要同时启用发行版默认 `hostapd.service` 或 `dnsmasq.service`，它们会争用无线接口或
53 端口；安装器会主动禁用这两个默认单元。

### 5.2 AP 与 DHCP

当前 USB 配置与迁移前板载脚本、工作区尚未部署的参数化修改分别记账：

- `gpusb0` 独占 `10.42.0.1/24`，AWUS036ACM 永久 MAC `00:c0:ca:be:2a:df`、USB ID `0e8d:7612`、驱动 `mt76x2u`。
- 热点 `gridopoly`，2.4 GHz／US／信道 6／HT20、WPA2-PSK／CCMP／802.11n／WMM；国家码按用户确认的实际使用地设置。
- DHCP `10.42.0.20`～`10.42.0.200`，12 小时；DNS 与网关均为 `10.42.0.1`，本地域名 `gridopoly.local`。格子固件要求非零网关，即使游戏仅使用本地服务也不能省略。
- USB AP 使用从已审核现场脚本复制的 `/usr/local/libexec/gridopoly-usb-ap`，保留 `ap_max_inactivity=10`、`disassoc_low_ack=0`；watchdog 仍为 20/45/2 秒。
- 原 `/usr/local/libexec/gridopoly-ap` 与工作区参数化修改不是同一部署物。后者缺参 15/1、模板 10/0 的行为不能当作当前 USB 的运行值；旧主动 ARP/定向清站提案未因此上线。
- 迁移前的七台关联瓶颈已有 USB 20 台同时关联/注册证据。DHCP 池 181 个地址和驱动 128 项 WCID 均不能替代未来 36 台及管理终端余量实测。
- 重启前保留 61 条 DHCP 租约是历史地址记录，不是 61 台在线。格子随机 Wi-Fi MAC 与 12 小时租约在反复上电时可能积累，扩容应核对剩余池，不能靠清租约制造验收通过。

四服务随实际 Pi 软件重启自动恢复；USB 设备重新枚举后约 6.9 秒自动恢复 AP/DHCP/watchdog 已验证。
这是定向驱动解绑/重绑证据，真实物理拔插和无家庭网冷上电仍另验。
初次设备恢复失败及安全回退记录保留，不能抹去，详见[USB 部署报告](usb-ap-deployment-2026-10-15.md)。

玩家网不转发外部 DNS，也不是面向互联网的通用热点。SSID 可公开；AP 密码、家庭 Wi-Fi 凭据和
UDP PSK 只写入树莓派本地配置，禁止提交 Git 或输出到日志。

## 6. 配置

正式文件：

```text
/etc/gridopoly/server.env  root:gridopoly 0640
/etc/gridopoly/ap.env      root:root       0600
```

`server.env`：

| 变量 | 默认/示例 | 说明 |
| --- | --- | --- |
| `GRIDOPOLY_DATA_DIR` | `/var/lib/gridopoly` | 存档与设备注册目录 |
| `GRIDOPOLY_HTTP_PORT` | `80` | 网页与素材端口 |
| `GRIDOPOLY_SERVICE_IP` | `10.42.0.1` | JSON 中发布给圆屏的服务地址 |
| `GRIDOPOLY_WEB_ASSET_DIR` | `/usr/local/share/gridopoly/tiles` | 72 个格子素材 |
| `GRIDOPOLY_AVATAR_COMPONENT_ROOT` | `/usr/local/share/gridopoly/avatar/components-v1` | 30 个 GAVC |
| `GRIDOPOLY_AVATAR_ASSET_ROOT` | `/var/lib/gridopoly/assets` | 最终/缓存头像根目录 |
| `GRIDOPOLY_UDP_BIND` | `0.0.0.0` | UDP 绑定地址 |
| `GRIDOPOLY_UDP_BROADCAST` | `10.42.0.255` | Discover 广播地址 |
| `GRIDOPOLY_BOT_ACTION_INTERVAL_MS` | `1200` | 机器人动作节拍，100..10000 ms |
| `GRIDOPOLY_UDP_PSK` | 无默认 | 至少 16 字节；缺失或过短时进程拒绝启动 |

`ap.env`（区分仓库板载模板与现役 USB 值）：

| 变量 | 默认/示例 | 说明 |
| --- | --- | --- |
| `GRIDOPOLY_AP_SSID` | `gridopoly` | 玩家网络名 |
| `GRIDOPOLY_AP_PASSPHRASE` | 本机设置 | WPA2 密码，不提交仓库 |
| `GRIDOPOLY_UPLINK_IFACE` | `wlan0` | 家庭网络接口 |
| `GRIDOPOLY_AP_PARENT_IFACE` | 旧模板 `wlan0`；现役 `gpusb0` | 现役使用 USB 独立接口 |
| `GRIDOPOLY_AP_IFACE` | 旧模板 `ap0`；现役 `gpusb0` | USB 物理接口，不创建虚拟 ap0 |
| `GRIDOPOLY_AP_ADDRESS` | `10.42.0.1/24` | AP 地址 |
| `GRIDOPOLY_AP_CHANNEL` | 旧模板空；现役 `6` | 旧空值跟随上行；USB 使用已核验信道 |
| `GRIDOPOLY_AP_MAX_INACTIVITY_SECONDS` | 回退 `15`，模板 `10` | hostapd station inactivity 秒数 |
| `GRIDOPOLY_AP_DISASSOC_LOW_ACK` | 回退 `1`，模板 `0` | 是否按驱动 low-ACK 立即断站，只允许 0/1 |
| `GRIDOPOLY_STALE_STATION_GRACE_SECONDS` | 回退 `6`，模板 `20` | FAILED/INCOMPLETE 邻居持续多久才回收 |
| `GRIDOPOLY_STALE_STATION_RX_GRACE_SECONDS` | 回退 `10`，模板 `45` | station RX 无进展持续多久才回收 |
| `GRIDOPOLY_STALE_STATION_POLL_SECONDS` | `2` | watchdog 轮询间隔 |

环境变量中的 Bot 间隔只是新安装/缺省值；网页保存后的实际值写入 `authority.meta`，之后重启
继续采用持久化值。

上述 inactivity/low-ACK 参数化行为属于尚未部署的工作区方案；现役 USB 复制脚本保留硬编码 10/0。USB 的国家码来自已审核专用脚本，不能只改表中接口或信道就代替完整迁移。安装器虽然保留现有 `ap.env`，仍会安装板载网络文件；现役机更新游戏程序不得重跑未经适配的全量安装器。
运行验收只能筛选 `/run/gridopoly/hostapd.conf` 的 inactivity/low-ACK 行，禁止输出含 WPA
口令的完整文件。

## 7. 构建与严格测试

### 7.1 完整 CMake/CTest

```bash
cmake -S . -B build-pi -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-pi
ctest --test-dir build-pi --output-on-failure
```

CMake/CTest 是完整门禁，覆盖核心、协议、UDP envelope、可靠性策略、网络恢复、玩家详情、
Identity、Avatar renderer/GAVC、交易、存档、UDP 集成、HTTP 素材，以及圆屏端头像组件
golden client。

### 7.2 树莓派原生严格脚本

```bash
Server/RaspberryPi/tools/build-and-test-native.sh
```

该脚本使用 `-std=c++17 -O2 -Wall -Wextra -Werror -pthread` 编译并运行大多数 Linux/ARM64
主机测试，最后生成 `build-pi-native/gridopoly_server`，当前脚本也包含
`player_avatar_component_client_tests`。具体发布记录须列明实际执行目标，不能把选择运行三个目标写成完整 CTest 通过。

素材门禁：

```bash
python3 Server/RaspberryPi/tools/test-stage-web-tile-assets.py
python3 Server/RaspberryPi/tools/verify-avatar-components.py
python3 Server/RaspberryPi/tools/test-stage-avatar-components.py
```

AP 部署静态门禁：

```bash
python3 tests/deploy/test_gridopoly_ap_config.py
sh -n Server/RaspberryPi/deploy/gridopoly-ap
sh -n Server/RaspberryPi/deploy/gridopoly-ap-cleanup
sh -n Server/RaspberryPi/deploy/install.sh
bash -n Server/RaspberryPi/deploy/gridopoly-ap-watchdog
```

## 8. 安装、升级与回滚

### 8.1 安装

以下全量安装器用于对应的初始部署基线。现役 USB AP 的完整单元、guard、持久命名与 udev 规则按[USB 部署记录](usb-ap-deployment-2026-10-15.md)维护；不要用旧板载或 eth0 安装流程覆盖。

```bash
sudo Server/RaspberryPi/deploy/install.sh \
  "$(pwd)/build-pi/gridopoly_server"
```

安装器会：

- 创建无登录系统用户 `gridopoly`。
- 安装二进制到 `/usr/local/bin/gridopoly_server`。
- 精确 staging 36 张 PNG + 36 张 RGB565，然后原子切换 `tiles` 目录。
- 校验并 staging 30 个 GAVC + manifest，然后原子切换头像组件目录。
- 安装头像 runtime pack、四个 systemd 单元、dnsmasq 和 NetworkManager 配置。
- 保留已存在的 `/etc/gridopoly/server.env` 与 `ap.env`。
- 不启动服务；配置完成后由维护者显式 enable/start。

已有生产机仅更新游戏 binary/Web 时，不重复运行可能改 AP/unit 的全量安装器。先核对候选 SHA、实际进程 SHA、room/玩家/状态并保存旧程序；正常 stop 让存档 flush，再复制整个数据目录取得一致备份、核对文件哈希，原子替换 binary 后 start。仅重启 gridopoly，保留 AP、凭据、配置和全部存档。启动或存档异常须保留失败数据再回滚一致备份。

部署后有界 GET 检查 health/sync/devices/assignments、原席位恢复、实际 `/proc/<pid>/exe` 与网页 ETag/gzip。正常停服会把真人 connected 持久化为 false，重连恢复 true；仅可在核对具体字段和最终完整 presence 后解释版本/CRC/真人 connected 的变化，不能忽略现金、资产、Tag 或其它 flags。2026-10-15 的首轮验收脚本曾因此误判并回滚，证据和第二轮成功完整保留在部署记录，不能把运行中的简单 `cp` 当一致快照。

涉及 AP/unit 配置时才单独重启对应单元。部署不应操作或重刷玩家屏COM7（COM4仅历史端口）；圆屏会根据
device id、room 和会话规则自动恢复。

## 9. HTTP 与网页

HTTP 在全部 IPv4 接口监听，当前没有登录或 CSRF 保护。因此管理 API 只允许在可信家庭
局域网或隔离玩家网使用，不能直接暴露到互联网。

### 9.1 页面与管理 API

| 方法 | 路径 | 作用 |
| --- | --- | --- |
| GET | `/` | gzip 测试网页；`ETag` + `no-cache` |
| GET | `/health` | 服务、HTTP、UDP、详情、交易和身份诊断 |
| GET | `/api/sync` | 网页增量入口；全部游戏/控制/身份/配对版本键相同返回 204 |
| GET | `/api/state` | 完整动态状态 |
| GET | `/api/board?room=<id>` | 当前房间静态棋盘；room 改变返回 409 |
| GET | `/api/settings` | Bot 间隔和允许范围 |
| POST | `/api/settings?botIntervalMs=<100..10000>` | 保存 Bot 间隔 |
| POST | `/api/action?...` | Web 控制席位动作 |
| POST | `/api/new?size=&autoPlayers=1` | 主入口：冻结 1～4 合格在线屏，补机器人至四席 |
| GET | `/api/player-devices` | 实时发现、eligible 与当前 seatId；不同于本局冻结名单 |
| GET / POST | `/api/tag-pairing` | 查询或带 room/session/revision/requestId 的 Retry/Cancel |
| POST | `/api/new?size=&humans=&bots=` | 显式调试兼容新局 |
| POST | `/api/forced-roll?player=&target=&expected=` | 指定某玩家下一次投骰落点 |
| POST | `/api/forced-roll?cancel=1` | 取消待执行强制落点 |
| POST | `/api/tile-modules/heartbeat?moduleId=&deviceId=` | 注册/续租、上报稳定 Tag 集合并返回 assignment + movement cue |
| GET | `/api/tile-debug/assignments` | 列出当前地图、在线模块、租约和内存临时分配 |
| POST | `/api/tile-debug/assignment?moduleId=&deviceId=&tileId=` | 右键把在线模块手动移动到当前格；owner 只读投影自权威状态 |
| DELETE | `/api/tile-debug/assignment?moduleId=` | 清除manual并保持在线；恢复合法ORDER投影或等待，旧无ORDER模块才自动认领 |
| GET | `/api/tile-tags` | 全局去重 Tag 目录、module/tile sighting 与玩家绑定 |
| POST | `/api/player-tag-binding?playerId=&uid=&expectedRevision=` | 非自动调试局兼容绑定；自动模式返回 NotAllowed |
| DELETE | `/api/player-tag-binding?playerId=&expectedRevision=` | 非自动调试局兼容解除；自动模式返回 NotAllowed |
| POST | `/api/web-detach` | 网页卸载提示，返回 204 |

`/api/sync` 的 unchanged 判断同时比较 `stateVersion`、`peers`、`roomId`、`networkId`、
`controlVersion`、`tagBindingRevision`、`identityRevision`及独立配对revision（query `pairing`），避免网页只看游戏版本而漏掉
连接、强制投骰、Tag 绑定、设置或身份变化。

强制投骰保存的是“指定玩家下一次投骰后的目标格”。服务端根据当前位置和棋盘长度计算步数；
它只影响匹配玩家的下一次合法 Roll，执行一次后清除，并通过 `authority.meta` 保持重启语义。

### 9.2 当前网页能力

- 可切换 16/24/32/40 格测试棋盘。
- 主入口自动发现1～4台合格屏、冻结真人并补机器人至4席；0台或超过4台拒绝。显式humans/bots调试接口保留兼容，不是主入口默认。
- 地产色带、正式图片、棋子、建筑/抵押状态和 P1..P6 彩色归属徽标。
- 悬停归属徽标显示完整玩家名。
- 玩家确认到达后，棋子按经过格子逐格过渡到目标位置。
- 选择玩家后直接点击棋盘格，设置下一次投骰目标。
- 运行时设置机器人动作间隔。
- 棋盘格右键菜单只需从在线模块中选择一个并分配到当前格；不提供“临时所有者”控件，也不
  提交 `ownerPlayerId`。名称、类型、颜色、素材键、价格和 owner 均由服务器派生，菜单不再
  要求浏览器选择第二次 tile。
- 只有无ORDER能力的旧模块首次Heartbeat自动认领最低空闲mapIndex；V0.34 按物理链锚点或全部在线匹配环派生，无锚时可未分配。手动移动会释放原格，
  但目标被另一 manual 锚点占用时返回 409；order/auto 按优先级撤销或重投影，环内还须满足四角相位。
- 手工Tag绑定接口仅保留旧调试局兼容。自动新局必须逐人配对；手动set/clear返回NotAllowed，已绑定UID不能转移给下一人。
- AwaitMoveConfirm 建立时 LED 仍保持 none；对应圆屏完成骰子动画、结果保持并呈现 MoveGuide
  首帧后，以 `ActionCode::MovementCueReady(17)` 放行。之后原格模块才收到
  departure/orange-breathe，目标格模块才收到 destination/green-flash。目标模块稳定且无
  overflow 地识别到当前玩家绑定 UID 时，服务器
  复用既有 ConfirmPosition 权威事务自动确认到达；圆屏 wire 不增加消息，手动确认入口保留。
- 机器人与单块真实圆屏混合测试。

格子每2秒Heartbeat，在线租约15秒。模块、device、tile各自唯一；清除manual后保持在线，按合法ORDER派生或等待锚点，只有旧无ORDER模块自动认领。新room/地图清除人工意图并重算；注册顺序不决定物理线序。正常续租不增加`serverRevision`；注册、实际
分配、过期回收和 room 重排才增加版本。GET 返回 `online`、`lastSeenMs`、
`leaseRemainingMs` 和 `source=auto|manual|order|none`，右键菜单只列在线模块。

assignment 的 `owner_player/owner_display_name/owner_color` 在每次 Heartbeat、GET 或 POST 前
从当前权威 `GameState` 刷新：非资产或未拥有资产返回 `0/""/0`，有效 owner 返回权威席位、
当前显示名和正式席位色。一次刷新中 owner 三元组有任意变化时，全局 `serverRevision` 只推进
一次，所有受影响 assignment 使用同一 revision/更新时间；格子绑定和 `source` 不变。这个
投影不会反向修改资产 owner、现金或任何权威版本。

注册表和临时分配只在服务器进程内存中存在，不写游戏存档、不改变 room、GameState 或任何
权威版本；服务重启后需新物理邻接证据。数组顺序不表示物理顺序，当前 ORDER 邻接/16、24、32、40 完整环按契约构图；配对扫描可经权威更新绑定，不能与仅分配owner投影混为一谈。HTTP管理与
Heartbeat 当前位于可信 LAN 边界，没有单独登录。精确 API、DTO、租约和 revision 语义见
[格子模块临时分配调试契约](tile-module-debug-assignment.md)。

### 9.3 HTTP 并发边界

- accept backlog：512
- worker：32
- 有界等待队列：1024
- 每连接最多 100 个请求
- 请求读取超时：3 秒
- 响应发送超时：5 秒
- header 上限：16 KiB
- keep-alive：3 秒
- 新连接排队超过 500 ms 时，最多一个完全空闲 keep-alive worker 主动让位

这套策略用于避免浏览器预连接、慢客户端和持续轮询占满服务，同时不阻塞 20 ms 权威循环或
UDP Heartbeat。

待复现的独立风险：HTTP 路由分别读取 `stateCopy()` 与 `roomId()`，旧请求可能在新上下文后触发再次清理 manual/Tag。尚无确定性并发复现或修复；同尺寸稳定环在锁内立即重新投影，不能将该窗口认定为本次等待页根因。见[重开局观察](newgame16-assignment-observation-2026-10-15.md)。

## 10. HTTP 素材契约

| 路径 | 内容 | 尺寸/格式 | 缓存 |
| --- | --- | --- | --- |
| `/assets/tiles/<key>.png` | 网页格子图 | 36 张 PNG | immutable |
| `/assets/tiles/<key>.rgb565` | 圆屏格子图 | 128×128，LE RGB565，32768 B | immutable |
| `/assets/avatar-components/v1/<hair|face|outfit>/<id>.gavc` | 中性透明结构层 | 220×300 画布，GAVC/RLE/CRC | strong ETag + immutable |
| `/assets/avatar-previews/v1/hH-cC-fF-sS-oO.rgb565` | 兼容整图预览 | 220×300，132000 B | strong ETag + immutable |
| `/assets/avatars/<content-key>.<png|rgb565>` | 最终公共头像 | 128×128 | 内容 hash ETag + immutable |

30 个 GAVC 由 Hair/Face/Outfit 各 10 个中性组件组成。HairColor 与 SkinTone 在圆屏本地按
冻结的整数色表即时着色；服务端最终头像使用同一 `face -> outfit -> hair`、round-half-up 和
source-over 算法，保证预览与公共头像逐像素同源。旧 290 个预着色图层不再是运行时来源。

格子 PNG/RGB565 路由会严格校验文件名，RGB565 长度不等于 32768 B 时返回 404。格子路由
当前使用 immutable cache-control，但不生成 ETag；GAVC、整图预览和最终头像支持强 ETag/304。

## 11. Wi-Fi/UDP 会话

- UDP 端口：4242
- Gridopoly Frame：32 B header + 最多 218 B payload = 250 B
- UDP envelope：48 B；最大 datagram 298 B
- 认证：截断 16 B HMAC；64 帧重放窗口
- Discover：每秒一次
- peer timeout：15 秒
- session 保留：2 分钟
- 同 device 重新配对复用真人 seat；DHCP 端点变化可在认证后迁移

HMAC 提供完整性、来源认证和重放保护，不加密 payload；无线保密性由 WPA2-CCMP 提供。
PSK 和 WPA2 密码是两套独立凭据。

常规全量同步使用 5 ms 帧间隔。服务端对合法 Heartbeat 立即 Ack；若客户端版本落后、
事件缺口或主动请求 resync，再额外发送投影。活跃机器人推进时 `resyncs` 随版本变化增加
不一定是故障；判断标准是每次同步后游标最终追平，静止版本的后续 Heartbeat 直接 Ack，
且不存在同一版本的持续 resync 循环。

隐私事件 `CardDrawn` 只向目标席位发送完整内容；旁观者收到同序号、字段清零的 tombstone，
从而保持事件序列连续而不泄露卡片。

按需消息：

| 类型 | 编号 | 用途 |
| --- | ---: | --- |
| `PlayerDetailRequest/Response` | `0x24/0x25` | 28 项资产 + 最近 10 条财务流水 |
| `PlayerCardEvent` | `0x26` | 两阶段 Card Drawn/Applied |
| `TradeRequest/Response` | `0x27/0x28` | 双向资产/现金交易 |
| `IdentityRequest/Snapshot` | `0x29/0x2A` | TagPairing 席位阶段、Avatar、Name、Ready、Countdown |
| `TagPairRequest/Snapshot` | `0x2B/0x2C` | 独立配对查询、Retry/Cancel、新扫描会话与拿回提示 |

## 12. 机器人调度

机器人只在 Identity 已进入 Active 后运行。每个间隔最多处理一项交易决策和一项普通游戏
决策；因此设置值描述机器人权威决策节拍，不是每个 Bot 独立拥有一条并发定时器。
推荐实机观察使用 800..2000 ms；100 ms 适合压力测试，较大会让 Bot 更易观察。

机器人永远不能替真人 PlayerConsole 做决定。只要当前决策者是真人，服务端等待对应圆屏或
Web 控制席位，不自动 Roll、购买、筹款、交易或结束回合。

## 13. 持久化

| 文件 | schema/内容 | 提交语义 |
| --- | --- | --- |
| `state.bin` | schema 4；兼容 v2/v3，游戏、财务历史、最多 3 笔交易 | CRC、0600 临时文件、fsync、rename |
| `identity.bin` | schema 1；席位、recipe、姓名、掩码、deadline | 独立 Identity 存储 |
| `authority.meta` | room、Tag 绑定、Bot 间隔、control/gate/forced-roll；ONB1 冻结身份和 PND1 未取回来源 | 临时文件 + rename |
| `device-seats.bin` | device → 真人 seat 绑定 | 临时文件 + rename |
| `assets/avatars/` | 内容寻址最终头像 | 临时生成后原子发布 |

游戏状态使用 750 ms 静默窗口合并写入，持续变化时最多 5 秒必须落盘，避免 Bot 推进造成同步
写放大。不要声称所有元数据文件都具有与 `state.bin` 相同的 CRC/fsync 级别；`state.bin` 与 `identity.bin` 的各自写入均实现 CRC/临时文件/fsync/rename，但多个文件不是一个跨文件原子事务；metadata/设备注册不能据此宣称同等耐久级别。

## 14. 健康检查与日志

```bash
curl --max-time 3 http://127.0.0.1/health
journalctl -u gridopoly.service -n 100 --no-pager
systemctl is-enabled gridopoly.service
systemctl is-active gridopoly.service
```

`/health` 顶层应关注：

- `roomId`、`version`、`peers`
- `botActionIntervalMs`、`controlVersion`
- `identityRevision`、`identityPhase`
- `uptimeMs`

HTTP：

- `accepted/rejected/completed/active/queued`
- `timeouts/sendErrors/keepAlivePreemptions`

UDP：

- `rx/valid/authFailures/replayDrops`
- `tx/txErrors/heartbeats/acks/resyncs`
- `peerSilenceMs/maxPeerSilenceMs`
- `detail`、`trade`、`identity` 子计数及最近请求

稳定链路的基本证据是 Heartbeat 与 Ack 同步增长、`peers` 与当前实际已入席会话数相符、认证/重放/发送错误不增长，
而不是强求活跃游戏中的 `resyncs` 永远不变。

## 15. 常见故障

### 网页刚重启很快，随后超时

1. 先请求 Pi 本机 `/health`，区分服务进程与电脑到 Pi 的网络路径。
2. 检查 `http.active/queued/timeouts/sendErrors`，不要只看浏览器错误页。
3. 检查代理/TUN 是否把 `.local` 解析到 `198.18.0.0/15` fake-IP。
4. 直接使用路由器显示的 Pi 家庭网 IPv4，不要用 fake-IP。
5. 若本机健康但电脑超时，排查家庭路由 AP isolation、代理、ARP 和有线/无线转发。

### 圆屏扫描到 AP 但不能配对

1. 确认 `gpusb0` 及 AP、dnsmasq、watchdog 三个单元 active，游戏地址只在该接口；旧 ap0 仅用于经审核的回退。
2. 确认圆屏获得 `10.42.0.x`，服务为 `10.42.0.1:4242`。
3. 核对双方 PSK，不在日志中打印明文。
4. 查看 `authFailures/replayDrops` 和 device-seat 注册。
5. 若 Pi 或 AP 重启，允许圆屏按 15 秒生命周期自动重配对，不要立即反复复位。

### AP station 反复关联/断开，格子 Heartbeat 消失

1. 若现役 `gpusb0`、`10.42.0.1/24`、直连路由和 `0.0.0.0:80` 均正常，而日志出现
   `rx-stalled`、failed-neighbor eviction 或密集 associated/disassociated，应判为无线
   station 数据面抖动，不要重启健康的 `gridopoly.service`。
2. 实体 RST/断电不保证发送 deauth，旧 station 可导致客户端 `AUTH_FAIL reason202`；
   软件 RESTART 的优雅 deauth 正常不能代替这条硬复位门禁。现役 USB 保留 10/0 和 watchdog 20/45/2，参数化修改仍未部署；20 台容量和设备恢复依据实机窗口，不由参数值本身证明。只有独立审核并授权 AP 变更后，才在维护窗口调整并重启 `gridopoly-ap.service`、`gridopoly-dnsmasq.service` 和
   `gridopoly-ap-watchdog.service`。
3. 只用下列命令确认运行配置，避免泄露 WPA 口令：

   ```bash
   sudo grep -E '^(ap_max_inactivity|disassoc_low_ack)=' /run/gridopoly/hostapd.conf
   ```

4. 当前摆位至少连续回归 15 分钟，要求 snapshot 租约不归零、无 watchdog eviction、
   无关联循环且 systemd `NRestarts` 不增长。
5. 若弱信号位置仍不能连续 Heartbeat，不再继续放宽超时；应改善模块与 Pi 的距离、遮挡、
   天线方向或供电。

### 重开 16 格后个别模块等待分配

2026-10-15 的首次报告在开始采集时已恢复。随后同尺寸重开局的 150 组观察与 230 次真实回包均正确，用户确认正常；未改源码或部署，根因未定。若复发，先保留当前房间和异常画面/module ID，由主会话协调同步采集 ORDER/assignment、实际心跳回包及该板有界串口。不要先反复重开抹去现场，也不要把 `ready` 拓扑或回包正确单独当作屏幕已刷新。

### Avatar Loading 或 Identity 卡住

1. 查看 `identityPhase/identityRevision` 和 identity 请求/响应/error 计数。
2. 校验 30 个 GAVC 路由为 200，非法路径为 404。
3. Confirm Avatar 后确认 final avatar 已发布，再允许 Confirm Name。
4. Active 房间不会重新进入 Avatar Setup；要重测必须由网页建立新 room。

## 16. 发布门禁

每次服务端代码、协议、网页、素材或存档修改后至少完成：

1. CMake Release 构建和完整 CTest。
2. Raspberry Pi ARM64 严格构建；关键 UDP 集成测试建议重复运行。
3. 36 PNG + 36 RGB565、30 GAVC 和 manifest 精确校验。
4. 部署前备份二进制与 `/var/lib/gridopoly`。
5. 只重启 `gridopoly.service`，确认 room/存档恢复。
6. `/health` 本机和家庭网均可访问。
7. 圆屏同 device → 原 seat，Heartbeat/Ack 连续，错误为 0。
8. Identity 阶段与 Active 阶段各验证一次全量恢复。
9. 16/24/32/40 网页棋盘检查 overflow、控制台错误、角落图、地产归属和移动动画。
10. 按需验证 PlayerDetail、Trade、Identity 和 Avatar HTTP，不把它们加入常规轮询。
11. 旧无 ORDER 模块验证最低索引唯一认领；当前模块验证全部在线单环、四尺寸真实地图四角、最后一条边触发、额外孤岛/旧模块/第二环拒绝、断开撤销和重连恢复，以及开放链锚点/清除、冲突和换图。仅分配操作不改变 GameState；另验证 16 格 B3
    未购买时显示 `$220`/owner 0，权威改为 P1 购买后无需重绑即自动显示 P1 和正式席位色。
12. 验证 Tag 全局去重、sighting/租约过期、玩家与 UID 一对一原子改绑、metadata 重启恢复和
    新 room 清理；验证 departure/destination cue、overflow 禁止自动触发、目标 Tag 只执行一次
    ConfirmPosition，并确认圆屏/网页手动确认仍可用。
13. AP 脚本修改需通过部署静态门禁和当前摆位 15 分钟 soak；确认无 station churn 后再判定
    可发布。

## 17. 2026-08-24 历史运行状态记录（不代表当前可达性）

以下保留 2026-08-24 当时记录；2026-10-15 已有新的部署/在线证据，见页首。该次审计无法对树莓派做新的在线确认：

- 先前使用的家庭网地址 `192.168.3.31` 当前没有有效 ARP，HTTP `/health` 超时。
- `Gridopoly.local` 当前被本机 Clash/TUN 解析到 `198.18.1.123`，不是树莓派真实地址。
- SSH 到上述地址立即关闭，因此本次没有取得实时 room/version/binary hash。
- 最近一次已确认配置为 `gridopoly.service` 已 enable 且 active，`Restart=always`、
  `RestartSec=2s`；历史端到端验收已证明 room/seat 持久化和 Heartbeat/Ack 自动恢复。

因此“开机自启动”是已配置基线，但上面的 room/version/peers 必须在树莓派重新可达后用
`/health` 再确认。文档不把不可验证的旧数值冒充当前在线状态。

## 18. 与旧实现的关系

- [ESP32-S3 测试游戏服务端](test-game-server.md)：保留用于 COM5/ESP-NOW 回归；不是生产服务。
- [测试服务端 ESP-NOW 协议](test-game-server-espnow-protocol.md)：只约束回退 transport。
- [历史主控 WebSocket/JSON 协议](main-controller-protocol.md)：设计记录，不是现行 wire。
- [玩家屏 Wi-Fi/UDP 协议](wifi-udp-player-protocol.md)：当前正式协议。
- [玩家开局头像创建规格](../player-console/player-avatar-setup-spec.md)：当前 Identity UI/体验规范。

遇到冲突时，以当前源码、`GridopolyProtocol`、本文件和 Wi-Fi/UDP 协议为准；历史设计记录
不能覆盖正式实现。
