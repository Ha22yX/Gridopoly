# Gridopoly Raspberry Pi Server

## 离线启动核验（2026-10-20）

家庭Wi-Fi不可用时的软件重启已实测：游戏、模块AP/DHCP、watchdog和管理AP五服务自动运行，30秒时16格及ORDER已恢复；真实客户端连接 `gridopoly-admin` 访问 `http://10.0.0.1` 通过。仓库通用单元已移除网络上线等待，现场USB专用配置保持。详见[服务清单、验证和限制](../../Docs/firmware/offline-boot-verification-2026-10-20.md)。

## 网页建筑图片与排列（2026-10-19）

当前服务端二进制06cff422…已部署。网页根据地产等级显示1～4栋房屋和酒店，下载排队并自动重试；网页排列已反转为起点右下、编号沿右侧向上，实体ORDER和资产编号保持。已验证线上B3的真实L1图片，16在线/16分配；见[实现与验证记录](../../Docs/firmware/web-development-artwork-2026-10-19.md)。以下部署版本为此前阶段。

## 调试页面语言（2026-10-19）

右上角「设置」旁的 **English / 中文** 切换主面板、右键编辑、配对/ORDER和设置提示，当前浏览器记住选择。Start new game默认16格，仍可选择24/32/40格。同日后续已部署26a2c4c7…，无需更新模块固件；操作与验证见[语言切换记录](../../Docs/firmware/web-language-switch-2026-10-19.md)。

## 管理热点（2026-10-19）

新增板载 Wi-Fi 管理热点 **`gridopoly-admin`**，连接后访问 **http://10.0.0.1**；保留家庭 Wi-Fi 自动连接及 ALFA USB 模块热点 `gridopoly` / `10.42.0.1`。管理服务已部署为开机自启，使用独立路由处理家庭网关同地址的情况。见[安装与运维](deploy/admin-ap/README.md)、[现场部署与验证](../../Docs/firmware/admin-hotspot-2026-10-19.md)。下面10月15日交付段落为历史时点，当前设备与网络状态以最新交接和上述记录为准。

## 当前交付（2026-10-15）

全部在线格子模块属于同一 ORDER 完整环、数量等于当前地图、实际四角类型匹配时，服务器自动沿环序分配；支持目录中的16/24/32/40格。额外在线模块、第二个环或错误角落不能被忽略，开放链人工锚点仍可调试。完整MAC屏幕发现、1～4真人补足4席、逐人Tag配对、并行外貌/姓名和有限库存/破产/终局保持。已部署Pi binary `ea6e1f51…`，管理网`10.0.0.124`；源码检查点`0fed0ce`，三个完整Linux目标及网页回归通过，真实全环仍待实体供电联调。[本轮构建、部署和回滚诊断记录](../../Docs/firmware/server-all-online-ring-progress-2026-10-15.md)。

当前契约见[闭环/席位/配对](../../Docs/firmware/ring-onboarding-contract-2026-09-26.md)、[新局兼容](../../Docs/firmware/newgame-map-compatibility-2026-09-26.md)、[部署台账](../../Docs/firmware/current-device-connections.md)。完整32实体/四屏/36客户端仍待验收；[外置AP工具](../../Docs/firmware/external-ap-deployment-2026-09-26.md)已交付但未切换现场网络。

当前正式权威服务端。详细架构、部署、运维和故障处理见
[`Docs/firmware/raspberry-pi-server.md`](../../Docs/firmware/raspberry-pi-server.md)。

## 组成

- `AuthorityService`：唯一游戏/身份/交易权威入口。
- `UdpPlayerServer`：玩家圆屏 Wi-Fi/UDP 会话、HMAC、重放保护和恢复。
- `HttpServer`：测试网页、管理 API、格子/头像素材和 `/health`。
- `TileDebugAssignments`：进程内格子模块注册/租约、自动与手动分配、正式目录投影和冻结
  下行 DTO；不接触权威状态。
- `FileStateStore` / `FileIdentityStore`：游戏与 Identity 持久化。
- `AvatarComponentCodec` / `AvatarRenderer`：30 个中性 GAVC、预览和最终头像。
- `deploy/`：四个 systemd 单元、AP、dnsmasq、watchdog 和安装器。

ESP32-S3 `TestGameServer` 仅为 ESP-NOW 回退与硬件回归，不是生产主控。

## 构建与测试

完整门禁：

```bash
cmake -S ../.. -B ../../build-pi -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build ../../build-pi
ctest --test-dir ../../build-pi --output-on-failure
```

Linux/ARM64 快速严格脚本：

```bash
tools/build-and-test-native.sh
```

该脚本不包含 `player_avatar_component_client_tests`；发布前仍需执行完整 CTest。

## 安装

```bash
sudo deploy/install.sh ../../build-pi/gridopoly_server
```

安装器会原子安装 36 PNG + 36 RGB565、30 GAVC + manifest，并安装四个 systemd 单元；
它不会 enable/start。先在树莓派本地填写：

```text
/etc/gridopoly/server.env
/etc/gridopoly/ap.env
```

然后：

```bash
sudo systemctl enable --now \
  gridopoly-ap.service \
  gridopoly-dnsmasq.service \
  gridopoly-ap-watchdog.service \
  gridopoly.service
```

真实家庭 Wi-Fi 凭据、AP 密码和 UDP PSK 不得写入仓库或日志。

## 入口

- 家庭局域网网页：`http://<Pi 家庭网 IPv4>/`
- 玩家网网页/素材：`http://10.42.0.1/`
- 玩家二进制协议：UDP `10.42.0.1:4242`
- 健康检查：`GET /health`
- 正式协议：[`wifi-udp-player-protocol.md`](../../Docs/firmware/wifi-udp-player-protocol.md)
- 头像组件：[`avatar-component-protocol.md`](../../Docs/firmware/avatar-component-protocol.md)
- 玩家详情：[`player-detail-query-protocol.md`](../../Docs/firmware/player-detail-query-protocol.md)
- 双向交易：[`trade-protocol.md`](../../Docs/firmware/trade-protocol.md)
- 格子模块临时分配：[`tile-module-debug-assignment.md`](../../Docs/firmware/tile-module-debug-assignment.md)

网页支持 16/24/32/40 格棋盘、真人/Bot 分离建局、机器人间隔、强制下一次投骰目标、
地产归属徽标和逐格棋子动画。棋盘格右键只需选择一个在线模块并手动移动到当前格；没有
“临时所有者”控件，也不提交 `ownerPlayerId`。目标已被另一在线模块占用时返回 409。

格子模块每 2 秒调用
`POST /api/tile-modules/heartbeat?moduleId=<id>&deviceId=<id>`，15 秒租约内视为在线。
只有缺少ORDER能力的旧模块首次注册/清除后走最低空闲mapIndex兼容路径。V0.33模块依物理邻接与人工锚点/合法32环投影，单板无锚可保持未分配并用于配对。手动改派释放原格；换房间清除人工意图并重算，不按网络注册顺序推断物理位置。正常续租不推进
`serverRevision`；注册、实际分配、过期和重排才推进。GET
`/api/tile-debug/assignments` 返回 `online/lastSeenMs/leaseRemainingMs/source`，右键
菜单只列在线模块；DELETE清除manual后恢复合法ORDER派生或等待锚点，只有旧无ORDER模块自动认领。

手动接口只有
`POST /api/tile-debug/assignment?moduleId=<id>&deviceId=<id>&tileId=<tile>`。assignment 的
`owner_player/owner_display_name/owner_color` 在每次 Heartbeat、GET 或 POST 前从当前权威
`GameState` 刷新；非资产或未拥有资产为 `0/""/0`，已拥有资产使用权威玩家名和正式席位色。
owner 投影变化会批量推进一次 `serverRevision` 和所有受影响 assignment 的 revision/更新时间，
但不改变格子绑定或 `source`。

注册表、ORDER邻接/租约及人工锚点仅在进程内存；冻结席位、Tag绑定和未取回限制另有持久化。物理ORDER以V1报告建图，配对由权威服务消费可信扫描，不能由模块自行绑定。Heartbeat响应包含 `ok/assigned/source/leaseMs/serverRevision`、
module/device，以及已分配时的冻结格子 DTO；调试接口绝不能伪造或反向修改权威 owner。管理
API 当前无登录，只能部署在可信局域网。

## 快速验收

```bash
systemctl is-enabled gridopoly.service
systemctl is-active gridopoly.service
curl --max-time 3 http://127.0.0.1/health
journalctl -u gridopoly.service -n 100 --no-pager
```

单屏稳定链路历史可见`peers=1`；多屏应与实际有效会话数核对，不把1作为固定通过值。Heartbeat/Ack应持续增长，且
`authFailures/replayDrops/txErrors` 不增长。活跃 Bot 推进时 resync 可随 state version
合理增加；同一静止版本持续 resync 才是故障。
