# Raspberry Pi 5 权威服务端

状态：迁移实施与验收入口
目标系统：Raspberry Pi OS Lite 64-bit（Debian Trixie）
最后更新：2026-08-07

## 1. 已迁移的能力

`Server/RaspberryPi` 直接链接现有纯 C++ `GridopolyCore` 与 `GridopolyProtocol`，不是另写一套规则。

- 16/24/32/40 格地图、地产/交通/设施、租金、抵押、建筑、债务、破产、机器人与拍卖屏障。
- 真人 PlayerConsole 完全由对应圆屏动作控制；机器人只自动处理 Bot 席位。
- 权威 `State/Auth/Roster/Event` 投影、动作幂等、玩家详情按需查询和断线全量恢复。
- 新的 HMAC 认证 UDP 会话，替代 ESP‑NOW；保留 ESP‑NOW 工程作为过渡兼容与回归基线。
- 原测试网页与 `/health`、`/api/sync`、`/api/board`、`/api/action`、`/api/new`。棋盘网页按资产组显示
  Monopoly 风格色带，并复用 `Assets/GridCity/StreetV3/device` 的 32 张正式地产、交通、设施和事件图片。
- 32 个固定 HTTP worker、512 accept backlog、1024 有界队列、3 s 读超时、5 s 写超时。
- HTTP/1.1 使用最多 100 请求、3 s 空闲上限的受限 keep-alive；只有当最老的新连接已经排队超过 500 ms 时，
  才允许一个完全空闲的 keep-alive worker 主动让位；同一时刻只发放一个让位资格，已经开始接收的请求不会
  被中断。这样既减少网页轮询制造的
  TCP `TIME_WAIT`，也避免多个长期打开的浏览器连接占满固定 worker。
- 状态/事件/未完成工作流原子文件持久化；room、serverDeviceId 和 device→seat 也持久化。
- 网页建局冻结真人席位与 Bot 席位，并由独立持久化身份投影实现 Avatar Setup → Name Setup → Ready →
  统一 5 秒倒计时；未连接真人不会被 Bot 或其他新设备替换。
- V1 头像由服务端从 290 个规范图层即时合成：编辑预览为 220×300 RGB565，最终公共头像为
  128×128 PNG/RGB565；结果使用内容寻址 immutable URL，合成完成前不公开真人配方或临时文件。
- systemd 自启、最小权限、自动重启和 5 s 存活日志。

## 2. 源码入口

```text
Server/RaspberryPi/src/main.cpp                 进程入口与生命周期
Server/RaspberryPi/src/AuthorityService.*       线程安全权威引擎/JSON/持久化
Server/RaspberryPi/src/UdpPlayerServer.*        玩家屏 UDP 会话与同步
Server/RaspberryPi/src/HttpServer.*             有界并发 HTTP/Web
Server/RaspberryPi/src/FileStateStore.*         原子状态文件
Server/RaspberryPi/src/IdentityModel.*          身份房间、冻结席位与确定性 Bot 配方
Server/RaspberryPi/src/FileIdentityStore.*      身份流程、幂等响应和倒计时持久化
Server/RaspberryPi/src/AvatarRenderer.*         V1 图层合成、预览缓存与最终头像原子发布
Server/RaspberryPi/deploy/                       systemd 与 AP 配置
Firmware/libraries/GridopolyCore/                唯一游戏核心
Firmware/libraries/GridopolyProtocol/            唯一二进制协议
```

玩家协议见 [Wi‑Fi/UDP 玩家屏协议](wifi-udp-player-protocol.md)。

## 3. 构建与测试

```bash
sudo apt-get install -y cmake ninja-build g++
cmake -S . -B build-pi -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-pi
ctest --test-dir build-pi --output-on-failure
```

系统没有 CMake 时，可直接使用仓库内的原生 `g++` 守门脚本；它会用相同的严格告警选项
编译并运行全部主机测试，最后链接正式服务端二进制：

```bash
GRIDOPOLY_NATIVE_BUILD_DIR=/tmp/gridopoly-build \
  Server/RaspberryPi/tools/build-and-test-native.sh
```

UDP 集成测试使用内核分配的临时端口，不与正在运行的正式 `4242/udp` 服务共享套接字，
因此故意构造的错误 HMAC/重放测试不会污染生产诊断计数。

严格编译选项为 `-Wall -Wextra -Werror`。至少需要通过：

- core rules
- protocol codec 与 Authority v1/v2/v3 兼容
- UDP envelope HMAC/replay
- UDP 真实 socket 配对、三投影、心跳、动作去重、查询、endpoint migration
- pending-card 持久化恢复
- player detail projection
- HTTP gzip/ETag/204/并发/长测

HTTP 发布测试同时覆盖顺序轮询、16 并发混合请求和慢连接占槽故障；必须检查请求失败、队列拒绝、读写错误和尾延迟，不能只看一次页面打开速度。

空闲 keep-alive 占槽故障可复现执行：

```bash
node Server/RaspberryPi/tools/stress-http-idle-keepalive.mjs http://<pi-ip> 64 750
```

它先让指定数量的完整 HTTP/1.1 响应停留在 keep-alive 空闲态，再从独立连接探测 `/health`；占槽和释放后的
两次延迟都必须小于给定门限。`/health.http.keepAlivePreemptions` 应在发生队列竞争时增长。

## 4. 安装服务

```bash
sudo sh Server/RaspberryPi/deploy/install.sh build-pi/gridopoly_server
sudoedit /etc/gridopoly/server.env
sudoedit /etc/gridopoly/ap.env
```

必须在 `/etc/gridopoly/server.env` 设置与圆屏本地配置相同的 `GRIDOPOLY_UDP_PSK`；真实值禁止写入仓库。
AP SSID/密码也只放在 root 可读的 `/etc/gridopoly/ap.env`。

机器人默认每 `1200 ms` 最多执行一个权威动作，因此掷骰、确认到位、购买/拍卖和结束回合会按步骤展示，
不会在一次网页刷新内瞬间完成。可在 `server.env` 使用
`GRIDOPOLY_BOT_ACTION_INTERVAL_MS=1200` 调整，允许范围为 `100..10000 ms`；修改后重启
`gridopoly.service` 生效。该间隔同样用于机器人对交易报价的接受、反价或拒绝。

配置完成并确认无线硬件支持后：

```bash
sudo systemctl enable --now gridopoly-ap gridopoly-dnsmasq gridopoly-ap-watchdog gridopoly
systemctl status gridopoly gridopoly-ap gridopoly-dnsmasq gridopoly-ap-watchdog
journalctl -u gridopoly -f
```

网页入口：

- AP 内：`http://10.42.0.1/`
- 当前家庭局域网调试：使用树莓派在家庭 Wi‑Fi 上取得的实际 IPv4 地址。

测试网页“建立新对局”分别选择真人数量与机器人数量，总人数必须为 `2..6`，真人至少 1 名。对应接口为：

```text
POST /api/new?size=<16|24|32|40>&humans=<1..5>&bots=<0..5>
```

真人席位从 P1 开始连续冻结，Bot 席位固定在其后且不能被圆屏认领。Bot 名称固定为 Bot 1、Bot 2…，
头像配方由 `roomSeed + playerId + avatarCatalogVersion` 确定性生成并持久化；服务重启和普通重同步不得
改变。ConfirmAvatar 只提交最终配方；合成成功后才能进入 Name Setup。ConfirmName 同时表示 Ready，姓名
立即进入 Roster；全部席位头像、姓名和生成状态满足后，服务端只创建一次持久化的 5 秒 epoch deadline，
倒计时中断线不会停止或重置。

测试网页提供一次性的“指定下一次落点”：右键玩家列表中的目标玩家，选择“指定下一次目的地”，
再直接点击棋盘上高亮的合法格。高亮范围严格是该玩家当前位置顺时针 `2..12` 格，服务端把目标换算
成两颗真实六面骰。除总点数 2/12 外优先使用非双骰组合；若 2/12 会
形成第三次连续双骰，则拒绝该目标，不能绕过进入限制区的规则。设置同时适用于真人和机器人，只在
匹配玩家下一次成功 `Roll` 时消费；新对局清除设置，服务重启则从 `authority.meta` 恢复并重新校验。
该管理状态使用独立 `controlVersion`，不会增加游戏 `stateVersion`、不会触发圆屏全量重同步，也不进入
UDP/ESP-NOW 协议。

HTTP 管理入口为：

- `POST /api/forced-roll?player=<id>&target=<tile>&expected=<stateVersion>`：设置一次。
- `POST /api/forced-roll?cancel=1`：取消。

玩家确认棋子到位后，网页把棋子按顺时针每 `180 ms` 前进一格；这只是浏览器视觉层，权威状态和后续
购买、债务、卡片流程仍立即执行。普通轮询不会打断相同目标的动画；新 room、换地图、破产、传送或
更高版本出现冲突位置时，动画立即取消并校正到权威位置。

网页与圆屏图片统一安装到 `/usr/local/share/gridopoly/tiles`，通过 `/assets/tiles/<name>.<format>`
按需读取：网页使用 36 张 PNG（含四张由 manifest `source` 在 staging 阶段规范化命名的角落图），圆屏使用 36 张 128×128 RGB565 little-endian 原始图（每张固定
32768 字节）。PNG 响应为 `image/png`，RGB565 响应为 `application/octet-stream`；两者都使用一年
`immutable` 缓存。HTML 内只保存稳定文件名和色组映射，不把图片塞入每次网页响应。圆屏低优先级
下载当前可见图片，并使用严格 1 MiB 的 LRU PSRAM 缓存（最多 32 张）；当前显示图片不会被淘汰，
被淘汰图片再次出现时重新下载。固件只保留一张内置占位图。部署前后可运行：

```bash
node Server/RaspberryPi/tools/verify-web-tile-assets.mjs http://<pi-ip>
```

校验器会逐个请求 36 条 PNG 与 36 条 RGB565 路由，比对 manifest 指向的本地正式素材 SHA-256，并对四个角落单独断言，同时检查固定长度、
MIME、缓存头、HTML gzip/ETag 和路径穿越拒绝。图片缺失、内容不一致或目录含额外文件时发布失败。

身份头像的 canonical 中性组件独立安装到
`/usr/local/share/gridopoly/avatar/components-v1`，旧 `/usr/local/share/gridopoly/avatar/avatar-v1.layers`
仅为回滚兼容；合成缓存位于 `/var/lib/gridopoly/assets`。HTTP 契约为：

- `/assets/avatar-components/v1/hair/h<1..10>.gavc`、`face/f<1..10>.gavc`、
  `outfit/o<1..10>.gavc`：220×300 坐标系内裁剪的 straight-alpha RGBA8888 RLE 中性结构层。
  圆屏只在结构 preset 变化时请求；HairColor/SkinTone 在本地即时定点着色。

- `/assets/avatar-previews/v1/h<hair>-c<hairColor>-f<face>-s<skin>-o<outfit>.rgb565`：
  220×300、132000 B、背景 `#061017`，用于圆屏编辑预览。
- `/assets/avatars/<roomId>/p<playerId>-a<revision>-<hash16>.rgb565`：
  128×128、32768 B，供圆屏准备页缓存。
- 同一路径 `.png`：128×128 圆形 RGBA，供网页玩家列表/准备页使用。

四类响应都使用强 ETag 和一年 immutable 缓存；只接受规范 ID/文件名，未原子发布、长度畸形或路径非法
一律 404。真实图层包、缓存文件和头像不得嵌入每次 `/api/sync` 响应。网页 JSON 的 `avatarUrl`
保持 `/assets/avatars/...` 同源相对路径，使家庭上联 IP 与玩家 AP `10.42.0.1` 两个入口都能正常加载；
圆屏则在该路径前使用固定服务端 `http://10.42.0.1`。

GAVC 的 32 字节头、RLE、颜色表、定点 Hair/Skin 算法、source-over、golden vectors 和
384 KiB 客户端缓存边界见 [头像组件流协议](avatar-component-protocol.md)。Pi 的 preview/final
compositor 与圆屏必须逐像素同源；Confirm Avatar 只执行一次最终合成与原子发布。

## 5. 双网络说明

当前树莓派只有内置 `wlan0`，没有 USB Wi‑Fi。驱动若支持 `managed + AP` 并发，部署脚本创建
虚拟 `ap0`：`wlan0` 继续连接家庭 Wi‑Fi，`ap0` 提供 `gridopoly`，地址为 `10.42.0.1/24`。
两边是不同 IP 子网，不桥接玩家 AP 到家庭网络；但它们仍共享同一个 2.4 GHz 射频和信道。

ESP32‑S3 只支持 2.4 GHz。单无线芯片部署时，上联家庭 Wi‑Fi 也必须固定在同一 SSID 的
2.4 GHz BSSID；如果上联自动漫游到 5 GHz，Linux 的单信道并发约束会让玩家 AP 无法继续提供给
圆屏。可先扫描并固定实际 BSSID：

```bash
sudo iw dev wlan0 scan
sudo nmcli connection modify <connection-uuid> \
  802-11-wireless.band bg 802-11-wireless.bssid <2.4GHz-bssid>
sudo nmcli connection up <connection-uuid>
```

正式部署已验证芯片声明 `managed <= 1, AP <= 1, #channels <= 1`；`gridopoly-ap` 始终读取当前
`wlan0` 信道，不自行选择不同信道。

因此：

- 逻辑网络和 DHCP 独立可以由单网卡实现。
- 射频、信道、带宽完全独立不能由一块无线芯片实现。
- 需要真正独立、最稳定的正式方案时，使用有线 Ethernet 作为上联，或增加一块受 Linux AP 模式支持的
  USB Wi‑Fi，把 `GRIDOPOLY_AP_PARENT_IFACE` 改为该接口。

服务启动前必须通过 `iw list` 的 valid interface combinations 验证 AP+managed 能力；若驱动拒绝创建
`ap0`，不得通过反复重连 `wlan0` 强行维持热点。

`gridopoly-ap-watchdog` 会观察玩家子网的失败邻居项和每个 station 的接收进度。屏幕断电或 USB
复位后，如果内核仍保留旧的 authorized station，ARP 失败持续超过 6 秒，或接收字节连续 10 秒
不增长时，只清理对应 MAC，不重启 AP，也不影响其他在线屏幕。配合 hostapd 的 15 秒 inactivity
上限，可避免旧 station 阻塞同一屏幕自动重连。

## 6. 持久化与恢复

默认数据目录 `/var/lib/gridopoly`：

- `state.bin`：玩家、资产、最近事件、pending move/purchase/card/debt/auction、RNG、版本。
- `authority.meta`：roomId、稳定 serverDeviceId、机器人动作间隔与一次性网页落点管理状态。
- `device-seats.bin`：deviceId 到 seatId 的稳定绑定。
- `identity.bin`：冻结的真人/Bot 席位、最终配方/姓名、逐席位 revision、幂等请求缓存和倒计时 deadline。
- `assets/`：按 immutable key 缓存的预览与最终头像；不是权威状态，丢失后可由 `identity.bin` 重建。

写入采用临时文件、flush/fsync、原子 rename。服务启动会清除旧的 `connected=true`，但保留游戏与
未完成交易；圆屏重新配对后恢复 seat，并按 pending-card/债务/拍卖阶段继续。

## 7. 运维健康标准

`GET /health` 返回平台、room、version、peers、uptime、HTTP 队列/错误以及 UDP HMAC/replay/心跳计数。
UDP 诊断还包含当前/历史最大 peer silence，以及按需玩家详情的请求、响应、幂等回放、错误、最后一次
requestId、目标玩家、请求版本和响应字节数。由此可以在不抓取正文、不记录密钥的前提下区分“屏幕没有发出请求”、
“服务端拒绝请求”和“响应已发送但客户端未接收”。这些计数只用于诊断，不进入权威状态，也不会触发持久化或常规投影广播。

发布门槛：

- 30 分钟网页轮询和动作压力中无超时、无队列持续增长、状态版本单调。
- 8 条空闲/慢 TCP 连接存在时健康接口仍能在超时边界内恢复。
- 圆屏每 2 s 心跳；正常链路 peer silence < 3 s；9 s 才显示 degraded，15 s 才重配对。
- 重复动作只产生一个状态版本推进和一组领域事件。
- 服务重启后 room/game/pending workflow 恢复，圆屏自动回原 seat。
- 断电恢复测试必须在原子状态文件和 systemd 自启均通过后执行。

## 8. 与 ESP32 测试服务端的关系

ESP32-S3 测试服务端不再承担正式 Web/权威服务。它保留用于硬件回归和 ESP‑NOW fallback；共享核心、
消息编号和 codec 仍需同步，不能出现两套规则。新的功能优先进入纯 C++ core/protocol，然后分别由
树莓派 UDP、网页、ESP‑NOW fallback 适配器调用。

## 9. 2026-08-05 实机迁移基线

- Raspberry Pi 5 整机重启后，四个 systemd 服务均自动恢复且 `NRestarts=0`；room 保持不变，未创建新局。
- `wlan0` 家庭上联与 `ap0` 玩家子网自动恢复；COM4 在无人串口、无需人工清 station 的情况下回到原 seat。
- COM4 的 State/Auth/Roster 同版本闭环已由投影日志与“普通 Heartbeat 直接 Ack、resync 不增长”共同验证。
- 32-worker 最终候选的 30 秒、16 并发预检达到约 2177 req/s、65320 次请求，失败、拒绝、超时和发送错误均为 0；
  p95 约 13.4 ms、p99 约 20.9 ms。
- 同一候选的 5 分钟、16 并发长测完成 562107 次请求（约 1874 req/s），失败、拒绝、超时和发送错误均为 0；
  p50 约 6.8 ms、p95 约 16 ms、p99 约 43.9 ms，正常压力期间没有触发 keep-alive worker 让位。
- 16/32/64 条空闲 keep-alive 故障注入下，新健康请求分别约为 10.8/632.8/636.4 ms；连接释放后均在 11 ms 内恢复。
- 长测后 Heartbeat 191→196、Ack 191→196，resync 固定为 2；最大 peer silence 约 4.2 s，低于玩家端
  9 s degraded 门限；HMAC、replay 与 UDP 发送错误均为 0。
- ARM64 原生严格构建与 core、protocol、UDP envelope、恢复策略、详情投影、持久化、真实 UDP socket 集成测试全部通过；
  PlayerDetail 首次响应和同 requestId 缓存回放已在真实 UDP 集成测试中验证。
- PlayerDetail 的实机人工入口需要操作者实际在圆屏打开详情页；未操作时诊断应保持 request/response 为 0，不能误判为服务端丢包。
- Web 棋盘已接入 Street V3 的 36 张正式图片和八组地产色带；四张 manifest `source` 角落素材会在
  staging 中规范化为 `corner-*.png`，Web UI 不依赖部署目录之外的仓库路径。生产 16 格页面以及隔离的
  24/32/40 格浏览器 DOM 验收中，四角图片均完成解码，格子横纵溢出为 0，控制台 warning/error 为 0。
  36 PNG + 36 RGB565 共 72 条路由逐项校验 7611379 bytes，MIME、immutable cache 与 SHA-256 全部
  与 manifest 指向的本地正式素材一致，路径穿越和非法文件名请求返回 404。
- Web 美术版本发布后的 30 秒、16 并发回归完成 56731 次请求（约 1891 req/s），失败、拒绝、超时和
  发送错误均为 0；p95 约 15.5 ms、p99 约 28.7 ms。32 条空闲 keep-alive 占槽时健康请求约
  607.4 ms，释放后约 4.7 ms；压力后玩家 Heartbeat/Ack 继续逐帧一致。
