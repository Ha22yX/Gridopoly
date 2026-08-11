# ESP32-S3 测试游戏服务端

状态：2026-08-03 已在独立 ESP32-S3 开发板 COM5 上编译、烧录并完成实机验证。

## 1. 用途与边界

该固件是玩家控制屏幕开发期间的临时权威主控，目标是用一块真实玩家屏幕配合网页棋盘和机器人完成交互验证。它不是最终树莓派服务端，但核心规则和二进制协议都放在无 Arduino 依赖的 C++ 库中，后续可以原样迁移。

固件同时运行两条逻辑链路：

- Wi-Fi STA：加入现有 2.4GHz 网络，提供局域网 HTTP 测试网站。
- ESP-NOW：连接玩家控制屏幕，传输配对、动作、完整动态状态、玩家名单和游戏事件。

ESP32-S3 只有一套 2.4GHz 射频，两条链路共享 Wi-Fi STA 当前信道；固件不创建 AP，也不存在第二套独立射频网络。

## 2. 已实现模块

| 模块 | 位置 | 职责 |
| --- | --- | --- |
| 纯 C++ 游戏核心 | `Firmware/libraries/GridopolyCore` | 地图、经济、回合、骰子、资产、租金、限制区、建设、抵押、交易、破产和机器人 |
| 纯 C++ 协议库 | `Firmware/libraries/GridopolyProtocol` | 250 字节内 ESP-NOW 帧、CRC32、配对、动作、完整状态、名单和事件批次编解码 |
| 应用协调器 | `Firmware/TestGameServer/src/ServerApp.*` | 游戏生命周期、网页 API、机器人节拍和状态投影 |
| ESP-NOW 适配器 | `Firmware/TestGameServer/src/EspNowTransport.*` | 广播发现、配对、逐屏 LMK、六屏公平调度、动作幂等、累计确认、事件补发和断线恢复 |
| 状态存储 | `Firmware/TestGameServer/src/StateStore.*` | Preferences/NVS 二进制快照、schema 与 CRC32 校验 |
| 测试网站 | `Firmware/TestGameServer/src/WebUi.h` | 可变棋盘、玩家、棋子、资产、事件和操作面板 |
| HTTP 适配器 | `Firmware/TestGameServer/src/HttpServer.*` | 8 连接固定槽、非阻塞响应、有界 keep-alive、慢连接回收和运行诊断 |
| 网络监督器 | `Firmware/TestGameServer/src/NetworkSupervisor.*` | STA/IP/网关联合判定、异步网关探测、HTTP 服务重建和假在线 STA 自愈 |
| 板级入口 | `Firmware/TestGameServer/TestGameServer.ino` | Wi-Fi、启动诊断、服务初始化和存活日志 |

回调层只把收到的 ESP-NOW 帧复制到固定队列；游戏状态只在主循环中修改。最多支持 6 个玩家席位和 6 块加密玩家屏幕。

每次网页操作、玩家屏幕操作、机器人操作、配对状态变化或新对局导致 `stateVersion` 增加时，所有已连接屏幕都会收到同一完整动态状态和对应事件；每块屏幕另外收到自己的动作掩码。静态地图内容来自双方共享的 `BoardCatalog`，并通过 `board_id_hash` 校验一致性。

## 3. 地图与经济基线

测试服务端加载用户要求的四套完整地图。每套地图的四个角落分别位于 `0`、`N/4`、`N/2`、`3N/4`。

| 格数 | 地图 ID | 资产数 | 起始现金 | 经过起点 | 限制区离场费 | 建议玩家 |
| ---: | --- | ---: | ---: | ---: | ---: | --- |
| 16 | `grid-city-16-v1` | 9 | 500 | 80 | 20 | 2～3 |
| 24 | `grid-city-24-v1` | 16 | 800 | 120 | 30 | 2～4 |
| 32 | `grid-city-32-v1` | 23 | 1200 | 160 | 40 | 2～6 |
| 40 | `grid-city-40-v1` | 28 | 1500 | 200 | 50 | 2～6 |

地图定义包含每格顺序、地产分组、购买价、L0～地标租金、建设成本、抵押值、交通枢纽和基础设施。正式内容规范仍以 `Docs/game/` 为准。

## 4. 当前游戏能力

- 双骰、连续双骰和第三次双骰进入限制区。
- 物理棋子目标位置确认；错误位置不会推进权威状态。
- 经过起点奖励、无主资产购买、逐玩家拍卖、自有资产和他人资产处理。
- 普通地产、完整地区组双倍基础租金、交通枢纽阶梯租金、基础设施骰点租金。
- Chance、Community Chest、服务费、自由中庭和前往限制区。
- 三次失败后强制支付离场费，以及主动支付离场费。
- 完整地区组、均匀建设、均匀出售、抵押、赎回和核心交易事务。
- `PlayerConsole` 和 Web 玩家遇到债务时进入人工筹款，手动出售建筑、抵押、确认付款或确认破产；交易核心可以参与筹款，但完整屏幕交易草案尚未接入。
- 只有 `Bot` 可以自动筹款、竞价和推进决策；机器人调度器遇到任意非 Bot 决策者都会停止。
- 最后存活者获胜；机器人可以自动完成整轮行动。
- 游戏状态写入 NVS，断电或软重启后恢复；事件环仅保存最近 32 条且不跨重启恢复。NVS 固定为 `0x9000 + 0x5000`，`0xE000 + 0x2000` 专门保留给 Arduino 的 `boot_app0`/OTA data；不得把 NVS 扩到 `0xE000`，否则每次烧录都会覆盖 NVS 尾页并导致进度无法恢复。
- NVS 写入采用 750ms 静默窗口、最长 5 秒落盘，避免每次机器人或网页操作都同步擦写 Flash 并阻塞 HTTP。
- 服务端对每个合法 ESP-NOW Heartbeat 先立即 ACK，需要恢复时再额外排队同步事务；PairAccept、ActionResult、Heartbeat Ack 使用高优先发送队列，Discover 和状态同步使用普通队列，ESP-NOW 任意时刻只保留一帧无线发送在途，投递失败按帧类型有界重试。15 秒无合法帧后标记屏幕离线，保留原 MAC/席位 60 秒，重连后发送 `FlagResync` 完整事务。
- `GRIDOPOLY_ALIVE` 将单播投递失败、Discover 广播无人接收、两级队列深度和在途状态分开统计，避免把无人监听时正常出现的广播失败误判为玩家链路故障。
- 使用 USB HWCDC 的固件必须在 `Serial.begin()` 后调用 `Serial.setTxTimeoutMs(0)`；服务端用编译期兼容助手在 HWCDC 上启用零超时，在没有该 API 的 HardwareSerial 上保持原生非流控发送。串口已枚举但监视器未消费时，日志允许短写或丢弃，绝不能阻塞游戏、HTTP、ESP-NOW 或配对时序。
- 玩家动作结果按 sequence 幂等缓存；结果帧丢失时屏幕可重发同一原始帧，服务端只重发结果而不会二次执行。
- 拍卖先进入 Opening 屏障：真人参与者屏幕按资产和 generation 幂等 Ready，全部到齐前不开放竞价且机器人不推进。
- Wi-Fi STA 启动失败、真实掉线或“仍显示 connected 但网关不可达”的假在线都会由网络监督器处理；恢复后自动重建 HTTP、`gridopoly-test.local` 和全量同步，不需要复位主控。

当前版本用于屏幕效果和基础流程验证。PlayerConsole 的拍卖和债务筹款已经是人工流程；机器人仍使用确定性策略。地主限时收租确认、完整交易草案/双方确认、最终卡牌文案与卡组持有物仍由正式主控完善。协议和核心模块已为替换这些策略保留边界。

## 5. 测试网站

启动后访问：

```text
http://gridopoly-test.local/
```

若 `.local` 被系统代理映射到假地址，用 COM5 以 115200 baud 查看 `GRIDOPOLY_WIFI` 行中的 DHCP 地址。网站无登录且使用明文 HTTP，只应在受信任的测试局域网使用。

### HTTP API

| 方法 | 路径 | 参数 | 结果 |
| --- | --- | --- | --- |
| GET | `/health` | 无 | 运行状态、堆内存、HTTP 缓存和 ESP-NOW 丢包/重连诊断 |
| GET | `/api/sync` | 可选 `since`, `peers`, `room`, `network` | 网页高频使用的紧凑动态投影；四项均未变化返回 204 |
| GET | `/api/board` | 可选 `room` | 当前房间的紧凑静态棋盘定义；room 已变化返回 409 |
| GET | `/api/state` | 可选 `since`, `peers`, `room`, `network` | 兼容诊断用完整 JSON；网页不再用它做高频轮询 |
| POST | `/api/new` | `size=16|24|32|40`, `bots=0..5` | 建立 1 个玩家屏幕席位加机器人对局 |
| POST | `/api/action` | `action`, `player`, `asset`, 可选 `arg`, `expected` | 执行权威游戏动作；expected 防止慢页面提交旧状态动作 |

`action` 可取 `roll`、`confirm`、`buy`、`decline`、`end`、`holdfee`、`mortgage`、`unmortgage`、`build`、`sell`、`paydebt`、`bankrupt`、`bid`、`passbid`、`auctionready`。拍卖出价通过 `arg` 传入；`auctionready` 通过 `asset` 和 `arg=generation` 绑定本轮拍卖。成功返回 HTTP 200；规则拒绝返回 HTTP 409 和稳定错误码。

服务端不只依赖网页按钮禁用状态：除幂等 `auctionready` 外，所有动作在统一入口再次校验该玩家当前 `availableActions`，随后才进入核心的金额、所有权和具体参数检查。手工构造或延迟到达的跨阶段请求不能绕过拍卖、债务或回合屏障。

网页使用单飞轮询：上一请求完成或超时前不会发起下一请求；操作请求会暂停并取消状态轮询，后台标签页自动降频。静态格子 ID、类型和价格只通过 `/api/board` 获取一次；玩家、资产、阶段、动作和最近 10 条事件通过 `/api/sync` 更新。网页每 30 秒强制读取一次完整紧凑投影，其他轮询在版本未变化时返回 204，断线恢复后即使先收到 204 也会正确恢复“在线”提示。维护源码会生成约 5.7KB 的 gzip 首页资源及内容派生 ETag，刷新同一固件时只返回 304。

HTTP 不再使用 Arduino 单客户端 `WebServer`，而是独立的固定容量适配器：底层监听队列与上层状态槽均为 8 个连接（覆盖 Chromium 对同一主机常见的 6 条预连接并保留诊断余量），每个请求行、头部和响应体都有硬上限。每条连接拥有独立 8KB 响应缓冲，发送器通过底层 socket 的 `MSG_DONTWAIT` 每轮最多发送 1KB，因此慢客户端不会阻塞游戏、`GRIDOPOLY_ALIVE` 或 ESP-NOW。大响应填满 lwIP 发送窗后允许最多 10 秒完成 TCP 重传；旧版 2.5 秒回收会错误截断本可恢复的页面下载。

keep-alive 最多复用 512 个请求；完整空闲连接保留 10 秒，收到部分请求后 1.5 秒仍不完整才回收。新连接到达而槽已满时优先淘汰最旧的空闲预连接。Wi-Fi 恢复时会清空全部旧槽并重建监听 socket。`/health` 额外暴露完整/紧凑状态字节数、最大响应、超限次数、持久化版本、已接收/拒绝/完成连接、写阻塞、发送错误、请求/响应超时、活动槽和待发送响应等指标。

网络监督器不再只相信 `WiFi.status()`：它同时验证 STA 关联、DHCP 本机地址、网关地址，并每 5 秒向局域网网关发送非阻塞 UDP DNS 请求，只有收到匹配事务号的响应才判定探测成功（避免部分路由器丢弃 ICMP 或 TCP/53 导致误判）。每次新关联先给予最多 30 秒的首次探测宽限，避免路由器刚完成 DHCP 时 DNS 尚未就绪造成误恢复；基线建立后，连续 3 次失败（约 15 秒）只重建一次 HTTP/mDNS。DNS 探测属于建议性诊断，绝不在 STA 关联、DHCP 地址和网关仍有效时主动断开共享无线接口；只有真实掉关联或丢失本机/网关地址并持续 10 秒才重连 STA。这样 HTTP 假在线恢复不会破坏同一射频上的 ESP-NOW 加密会话。监听暂停、旧 TCP 槽清理、Wi-Fi 恢复和 ESP-NOW 全量重同步都有明确生命周期；`/health.networkHealth` 与 `GRIDOPOLY_ALIVE` 可查看 RSSI、探测失败、服务重启和 STA 重连次数。

共享 2.4GHz 射频仍可能造成单次延迟抖动；正式树莓派服务端可以替换 HTTP 和网络适配器而不改游戏核心或 API。

## 6. 编译、验证与烧录

环境使用 Arduino-ESP32 `3.3.11` 和 bundled esptool `5.3.1`：

```powershell
.\Firmware\TestGameServer\tools\compile.ps1
.\Firmware\TestGameServer\tools\verify-build.ps1
.\Firmware\TestGameServer\tools\probe-com5.ps1 -Port COM5
.\Firmware\TestGameServer\tools\upload.ps1 -Port COM5
.\Firmware\TestGameServer\tools\smoke-com5.ps1 -Port COM5 -Seconds 25
node .\Firmware\TestGameServer\tools\stress-http.mjs http://设备IP 180
node .\Firmware\TestGameServer\tools\fault-http-idle.mjs http://设备IP 8
```

烧录脚本只接受显式 `COM数字`，默认固定 COM5，不会选择“第一个串口”。当前分区为 16MB：4MB factory app、约 11.9MB 数据区、20KB NVS、8KB OTA data 和 64KB coredump。构建校验会拒绝恢复为与 `0xE000` 烧录区重叠的旧 NVS 布局。

本次实机验收结果：

- ESP32-S3 rev 0.2，16MB Quad Flash，8MB PSRAM。
- 应用镜像约 0.99MB，静态 RAM 约 139KB，编译期仍保留约 188KB 动态空间。
- Wi-Fi STA、ESP-NOW、网页服务和游戏循环均启动成功。
- 实体圆屏完成掷骰、物理落点确认、购买和结束回合；重复 ActionRequest 只执行一次，State/Auth/Roster 始终同版本，随后机器人自动推进并持续同步。
- 修正分区后，不擦除数据复位前 `persistedVersion=8`，复位后先恢复为 v9（仅清理旧在线标记），玩家重连后为 v10；后续从 v12 烧录重启同样恢复为 v13、重连为 v14，证明没有重新开局。
- 实机有效负载：`/api/sync` 1194B、`/api/board` 676B；一次样本分别约 0.54s 与 0.66s。gzip 首页 5734B 在 2.06s 内完整返回，再次刷新通过 ETag 返回 304/0B，约 0.15s。
- 最新 120 轮有线电脑到 Wi-Fi STA 的持续压测在第 48 次连接前因 TCP 建连超时中止；同时服务端只记录 47 accepted/47 completed/0 active，说明失败请求的 SYN 没有到达 ESP32，而非 HTTP 槽、JSON 或游戏循环堵塞。该剩余问题属于当前路由器的有线到无线单播路径；换同 SSID 的 Wi-Fi 客户端验证、关闭客户端隔离，或把正式网页服务迁移到有线树莓派才是网络层闭环。
- 8 条空闲 TCP 连接占满槽的故障注入通过，额外 `/health` 在约 212ms 内返回，释放后活动槽自动回收。

## 7. 秘密与仓库边界

真实配置只放在：

```text
Firmware/TestGameServer/config/secrets.local.h
```

该文件、`build/` 和 `runtime/` 已被 `.gitignore` 排除。仓库只保留 `secrets.example.h`。编译、烧录、串口日志、网页 API 和文档都不得输出 Wi-Fi 密码或 ESP-NOW 测试 PSK。

## 8. 迁移到正式主控

树莓派版本保留 `GridopolyCore`、`GridopolyProtocol`、地图数据和动作语义，替换以下适配器：

- 固定容量 `HttpServer` → 正式 HTTP/WebSocket 服务。
- Preferences/NVS → 文件或数据库事件存储。
- ESP-NOW transport → ESP32 无线网关或正式玩家终端传输层。
- 当前机器人调度器 → 可配置策略调度器。

这样玩家控制屏幕只依赖协议 v1 的状态和动作，不依赖临时网页或 Arduino 对象。
