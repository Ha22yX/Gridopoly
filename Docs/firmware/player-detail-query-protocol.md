# 玩家详情按需查询协议

状态：当前正式协议；2026-10-15 按源码复核，payload/schema 未改变

本文定义玩家圆屏 `Players -> Player Detail` 页面使用的单次、按需查询。正式传输为
Raspberry Pi Wi-Fi/UDP；ESP-NOW 适配器只保留兼容回退。唯一代码来源是：

- `Firmware/libraries/GridopolyProtocol/src/gridopoly/protocol/Protocol.h`
- `Firmware/libraries/GridopolyProtocol/src/gridopoly/protocol/Protocol.cpp`
- `Firmware/TestGameServer/src/PlayerDetailProjection.cpp`
- `Server/RaspberryPi/src/UdpPlayerServer.cpp`
- `Firmware/PlayerConsole/wifi_udp_player_transport.cpp`

该查询不属于常规状态同步事务，不加入 `StateSnapshot`、`AuthoritySnapshot`、`RosterSnapshot` 或 `GameEvent` 广播。进入详情页时请求一次，只有用户手动刷新或请求超时才重发；离开页面后不订阅、不轮询。

## 消息类型与鉴权

| 值 | 名称 | 方向 | 正式 UDP 保护 |
| ---: | --- | --- | --- |
| `0x24` | `PlayerDetailRequest` | 玩家屏 -> 服务端 | session HMAC + replay window |
| `0x25` | `PlayerDetailResponse` | 服务端 -> 玩家屏 | session HMAC + replay window |

两种消息继续使用 32 字节公共帧头。正式 UDP 中，帧外再包 48 字节认证 envelope；
`room_id + device_id + session_id` 必须命中已绑定真人席位，并通过 HMAC/replay window 校验。
认证成功后来源 IP/UDP 端口可迁移，服务端更新 session endpoint，席位不变。
请求 payload 不重复携带 room 或 actor；服务端以认证 session 的 `seat_id` 鉴权。同一房间内
任意已认证真人屏幕可以查询任意现有玩家的公开详情。非法来源、目标、room、session 或认证
标签不会返回详情。HMAC 不加密 payload；无线保密性由 WPA2-CCMP 提供。ESP-NOW 回退使用
既有 MAC/LMK 会话，但不改变 payload。所有多字节整数均为 little-endian。

## `PlayerDetailRequest` payload

固定 12 字节，codec 为 `encodePlayerDetailRequest()` / `decodePlayerDetailRequest()`。

| 偏移 | 长度 | 字段 | 说明 |
| ---: | ---: | --- | --- |
| 0 | 1 | `schema` | 固定 1 |
| 1 | 1 | `target_player_id` | 要查看的玩家，1..6 |
| 2 | 2 | reserved | 固定 0 |
| 4 | 4 | `request_id` | 非 0；由屏幕生成的幂等键 |
| 8 | 4 | `expected_state_version` | 打开详情时屏幕已应用的版本；0 表示不声明版本 |

请求版本不匹配不会被拒绝。服务端返回生成详情时的实际版本，并设置响应 stale 标志。

## `PlayerDetailResponse` payload

codec 为 `encodePlayerDetailResponse()` / `decodePlayerDetailResponse()`。基础头固定 20 字节，
之后先放资产数组，再放财务流水数组：`20 + asset_count*2 + ledger_count*12`。最坏情况为
196 字节，加公共帧头后 Frame 为 228 字节；正式 UDP 再加 48 字节 envelope 后为 276 字节，
低于 298 字节数据报上限，也保持在 ESP-NOW 250 字节 Frame 回退边界内。

| 偏移 | 长度 | 字段 | 说明 |
| ---: | ---: | --- | --- |
| 0 | 1 | `schema` | 固定 1 |
| 1 | 1 | `flags` | bit0 资产截断；bit1 流水截断；bit2 请求版本已过期 |
| 2 | 1 | `target_player_id` | 被查询玩家 |
| 3 | 1 | `position` | 当前格号；格名由本地 `BoardCatalog` 查找 |
| 4 | 4 | `request_id` | 原样回显 |
| 8 | 4 | `state_version` | 生成详情时的权威版本 |
| 12 | 4 | `cash` | int32 |
| 16 | 1 | `asset_count` | 本帧资产记录数，最大 28 |
| 17 | 1 | `ledger_count` | 本帧流水记录数，最大 10 |
| 18 | 1 | `total_owned_assets` | 实际持有资产总数 |
| 19 | 1 | reserved | 固定 0 |
| 20 | `2*A` | assets | 资产数组 |
| `20+2*A` | `12*L` | ledger | 财务流水，新到旧 |

资产记录固定 2 字节：`asset_index:u8, state:u8`。`state` bits0..2 为建筑等级，bit3 为抵押，其余位必须为 0。

流水记录固定 12 字节：

| 相对偏移 | 长度 | 字段 | 说明 |
| ---: | ---: | --- | --- |
| 0 | 4 | `event_sequence` | 权威事件序号 |
| 4 | 4 | `signed_amount` | int32；被查询玩家收入为正、支出为负 |
| 8 | 1 | `event_kind` | `core::EventKind` 数值 |
| 9 | 1 | `counterparty_id` | 对手玩家；0 表示银行/系统 |
| 10 | 1 | `asset_index` | 无关联资产时 `0xFF` |
| 11 | 1 | `flags` | bit0 收入、bit1 银行对手、bit2 有关联资产 |

核心引擎为每位玩家独立维护最近 10 条权威现金流水，只在查询时编码，不增加日常广播负载。开局资金、经过起点奖励、购买、租金、所有费用、卡片现金变化、抵押/赎回、建设/出售建筑、拍卖成交、交易、债务付款以及破产剩余现金转移都会在余额实际改变的同一事务中入账；一次现金变化只生成一条玩家视角记录，不会因 `DebtPaid` 等结算事件重复计账。非财务的骰子、移动和回合事件不会挤掉流水。流水随游戏状态持久化，服务端重启后继续保留最近 10 条；旧 schema 存档会从仍保留的事件窗口迁移一次。

## 幂等、重试与生命周期

- 服务端为每个已认证 session 缓存最近一次成功响应的完整 payload。
- 幂等键是 `session/peer + room_id + request_id`。同一 ID 且 target/version 相同，逐字节回放第一次结果，即使游戏版本已推进也不重新计算。
- 同一 ID 携带不同 target 或 expected version 是调用方错误并被拒绝；手动刷新必须使用新 ID。
- 新房间或重新配对会清空缓存。
- 响应帧头 `acknowledgement` 等于本次请求帧 sequence；payload 的 `request_id` 用于页面匹配。
- 进入详情页生成新 ID 并请求一次；超时用相同 payload/ID 重发。离开页面后丢弃迟到响应，不订阅。
- UDP 本身不提供 MAC 层业务重试。客户端缓存原始请求，700 ms 无匹配响应时以同一
  request ID 和 payload 重发，最多发送 4 次（含首次）；4 秒仍失败时显示 Retry，不要周期轮询。服务端收到
  重复请求后逐字节回放第一次响应。

当前自动开局固定总共四席，codec 仍保留最多六席、28 项资产的容量；不要把传输容量当作自动真人上限。详情请求仅来自本局已认证席位，晚加入且 `seatId=0` 的屏幕等待新局，不能以发现列表在线身份查询本局详情。TagPairing 的独立 requestId/session/revision 不与详情请求缓存混用。

## 验收边界

1. 查询不改变 `state_version`，不执行游戏动作。
2. 常规 State/Auth/Roster payload 长度保持不变。
3. 28 项资产加 10 条流水仍为单帧。
4. 相同 request ID 只回放第一次结果；新 ID 才生成新详情。
5. 错误 room、未配对来源、非法 target 和 ID 碰撞不能泄露详情。
