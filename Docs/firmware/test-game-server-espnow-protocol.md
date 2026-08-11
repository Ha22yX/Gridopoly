# 测试服务端与玩家屏幕 ESP-NOW 协议 v1

本文件是玩家控制屏幕开发会话 `019fbb98-c4f6-7e20-9ccc-5f6dc5b00413` 的对接入口。代码权威定义位于：

- `Firmware/libraries/GridopolyProtocol/src/gridopoly/protocol/Protocol.h`
- `Firmware/libraries/GridopolyProtocol/src/gridopoly/protocol/Protocol.cpp`
- `Firmware/TestGameServer/src/EspNowTransport.cpp`

## 1. 传输约束

- ESP-NOW 单帧上限：250 字节。
- 所有多字节整数：little-endian。
- 帧头：固定 32 字节；payload 最大 218 字节。
- 协议版本：1。
- ESP-NOW 接口：`WIFI_IF_STA`。
- 游戏帧在配对完成后使用 ESP-NOW CCMP/LMK 加密。
- Wi-Fi STA 与 ESP-NOW 必须处于同一 2.4GHz 信道；服务端不创建 AP。

## 2. 固定帧头

| 偏移 | 长度 | 字段 | 说明 |
| ---: | ---: | --- | --- |
| 0 | 4 | `magic` | `0x44495247`，线字节为 `47 52 49 44`（`GRID`） |
| 4 | 1 | `version` | 固定 `1` |
| 5 | 1 | `message_type` | 见消息表 |
| 6 | 2 | `flags` | 位掩码 |
| 8 | 4 | `sequence` | 发送方单调递增序号 |
| 12 | 4 | `acknowledgement` | 响应所确认的请求序号 |
| 16 | 4 | `room_id` | 当前房间 ID |
| 20 | 4 | `device_id` | 发送设备 ID |
| 24 | 2 | `payload_length` | `0..218` |
| 26 | 2 | `header_length` | 固定 `32` |
| 28 | 4 | `payload_crc32` | IEEE CRC32，多项式 `0xEDB88320` |

解码顺序必须是：长度范围 → magic/version/header length → payload 精确长度 → CRC32 → 消息 payload。禁止直接 `memcpy` C/C++ struct。

### Flags

| 位 | 常量 | 含义 |
| ---: | --- | --- |
| 0 | `FlagAckRequired` | 发送方需要语义响应或后续同步 |
| 1 | `FlagResponse` | 当前帧是响应 |
| 2 | `FlagBroadcast` | 广播发现帧 |
| 3 | `FlagResync` | 接收方应以该快照覆盖本地状态 |

## 3. 消息类型

| 值 | 名称 | 方向 | 当前用途 |
| ---: | --- | --- | --- |
| `0x01` | `Discover` | 服务端 → 广播 | 每 2 秒发现一次 |
| `0x02` | `PairRequest` | 屏幕 → 服务端 | 未加密配对请求 |
| `0x03` | `PairAccept` | 服务端 → 屏幕 | 未加密配对结果 |
| `0x04` | `Heartbeat` | 屏幕 → 服务端 | 加密保活并请求最新快照 |
| `0x10` | `StateSnapshot` | 服务端 → 屏幕 | 加密权威状态 |
| `0x11` | `GameEvent` | 服务端 → 屏幕 | 加密事件批次；用于动画、提示和操作历史 |
| `0x12` | `AuthoritySnapshot` | 服务端 → 屏幕 | 加密完整动态游戏状态；所有屏幕内容相同 |
| `0x13` | `RosterSnapshot` | 服务端 → 屏幕 | 加密玩家编号与显示名 |
| `0x20` | `ActionRequest` | 屏幕 → 服务端 | 加密玩家动作 |
| `0x21` | `ActionResult` | 服务端 → 屏幕 | 加密动作结果 |
| `0x22` | `Ack` | 服务端 → 屏幕 | 每个合法 Heartbeat 的即时轻量应答 |
| `0x23` | `Error` | 服务端 → 屏幕 | 保留 |
| `0x24` | `PlayerDetailRequest` | 屏幕 → 服务端 | 加密、按需玩家详情查询 |
| `0x25` | `PlayerDetailResponse` | 服务端 → 屏幕 | 加密、单帧玩家详情结果 |

每次权威 `state_version` 变化后，服务端都为每块已加密配对的屏幕排入一个同步事务：个人快照、完整权威快照、玩家名单，以及零个或多个事件批次。发送器以 8ms 间隔在最多 6 个 peer 之间轮询，避免一块屏幕独占发送队列。丢帧时由 Heartbeat 携带的累计应用版本触发重发或完整恢复。

玩家详情是例外的按需查询，不属于上述同步事务，也不随版本变化广播。完整 payload 偏移、幂等和重试规则见[玩家详情按需查询协议](player-detail-query-protocol.md)。

## 4. 发现与配对

### 4.1 `Discover` payload（16 字节）

| 偏移 | 长度 | 字段 |
| ---: | ---: | --- |
| 0 | 1 | payload schema，固定 1 |
| 1 | 1 | Wi-Fi/ESP-NOW 信道 |
| 2 | 1 | 协议版本 |
| 3 | 1 | 服务端最大屏幕数，当前 6 |
| 4 | 4 | `server_device_id` |
| 8 | 4 | `room_id` |
| 12 | 4 | `state_version` |

玩家屏幕启动后使用 STA 模式逐信道监听；收到合法 Discover 后锁定该信道。若连续数秒失联，应重新扫描，而不是自行创建或加入另一个 AP。

### 4.2 `PairRequest` payload（26 字节）

| 偏移 | 长度 | 字段 |
| ---: | ---: | --- |
| 0 | 1 | schema 1 |
| 1 | 4 | 随机 `device_nonce` |
| 5 | 4 | capabilities 位图，未知位填 0 |
| 9 | 17 | UTF-8 显示名，最多 16 字节并以 NUL 结束 |

帧头 `device_id` 必须是玩家屏幕稳定设备 ID，建议取 eFuse MAC 的低 32 位。PairRequest 使用临时未加密 peer 发送。

### 4.3 `PairAccept` payload（13 字节）

| 偏移 | 长度 | 字段 |
| ---: | ---: | --- |
| 0 | 1 | schema 1 |
| 1 | 1 | `accepted`，0/1 |
| 2 | 1 | `seat_id`，1..6 |
| 3 | 1 | 当前信道 |
| 4 | 1 | 保留 0 |
| 5 | 4 | `server_device_id` |
| 9 | 4 | 当前 `state_version` |

PairAccept 仍为未加密帧。服务端发送后等待约 350ms，再把该 peer 提升为逐屏 LMK 加密并发送 `FlagResync` 快照。

### 4.4 测试密钥派生

本协议的密钥配置是本地测试配置，不是生产认证方案：

```text
PSK_HASH = SHA256(TEST_PSK)                           // 32 bytes
PMK      = first16(PSK_HASH)
LMK      = first16(SHA256(
             PSK_HASH ||
             LE32(server_device_id) ||
             LE32(console_device_id) ||
             LE32(device_nonce)))
```

玩家屏幕的正确顺序：

1. 发现服务端，记录 channel、server ID、room ID。
2. 设置 `PMK`，添加临时未加密服务端 peer。
3. 发送未加密 PairRequest。
4. 收到未加密 PairAccept 并确认 `accepted=1`。
5. 删除临时 peer，按上式添加加密服务端 peer。
6. 等待首个加密 `StateSnapshot + FlagResync`。
7. 每 2 秒发送加密 Heartbeat；服务端 15 秒未收到合法帧会标记离线并移除无线 peer，但为同一 MAC 保留原席位 60 秒。

服务端重启后需要重新配对，启动时会生成新的非零 `room_id`，避免把重启前滞留的帧误认为当前会话。60 秒保留期内同一 MAC 重新配对仍回到原席位；保留期结束后才释放给其他屏幕。新屏幕总是取得最低可用席位；机器人席位会被真实屏幕接管，因此同一固件可从 1 块扩展到 6 块玩家屏幕。

## 5. 状态同步

### 5.1 `StateSnapshot`：屏幕个人投影

`StateSnapshot` payload schema 为 2，长度为 `44 + 7 × player_count`，最多 86 字节。

| 偏移 | 长度 | 字段 | 说明 |
| ---: | ---: | --- | --- |
| 0 | 1 | schema | 2 |
| 1 | 1 | `seat_id` | 当前屏幕自身席位 |
| 2 | 1 | `phase` | 见阶段表 |
| 3 | 1 | `active_player_id` | 当前行动玩家 |
| 4 | 2 | `round` | 回合号 |
| 6 | 1 | `board_size` | 16/24/32/40 |
| 7 | 1 | `self_position` | 自身棋子位置 |
| 8 | 4 | `self_cash` | int32 |
| 12 | 4 | `available_actions` | 动作位掩码 |
| 16 | 1 | `player_count` | 1..6 |
| 17 | 1 | `tile_asset_index` | 当前格资产；`0xFF` 表示无资产 |
| 18 | 1 | `tile_owner_id` | 0 表示系统/无主 |
| 19 | 1 | `tile_building_level` | 0..5 |
| 20 | 1 | `tile_flags` | bit0 抵押 |
| 21 | 1 | `pending_target` | 待确认棋子位置；`0xFF` 表示无 |
| 22 | 4 | `state_version` | 乐观并发版本 |
| 26 | 1 | `decision_player_id` | 当前必须作出决定的玩家 |
| 27 | 1 | `debt_creditor_id` | 0 表示系统；无债务时为 0 |
| 28 | 1 | `debt_asset_index` | 关联资产；`0xFF` 表示无 |
| 29 | 1 | `auction_asset_index` | 拍卖资产；`0xFF` 表示无 |
| 30 | 4 | `debt_amount` | 当前待支付金额，int32 |
| 34 | 4 | `auction_current_bid` | 当前最高价，int32 |
| 38 | 4 | `auction_minimum_bid` | 当前玩家最低合法出价，int32 |
| 42 | 1 | `auction_highest_bidder_id` | 当前最高出价者 |
| 43 | 1 | reserved | 必须为 0 |
| 44 | 7×N | players | 玩家摘要数组 |

每个玩家摘要：`player_id:u8, position:u8, cash:i32, flags:u8`。flags 为：bit0 在限制区、bit1 已破产、bit2 已连接、bits3..4 控制器类型（0 真屏、1 Web、2 Bot、3 未分配）。

### 5.2 `AuthoritySnapshot`：完整动态状态

`AuthoritySnapshot` 当前 payload schema 为 2。长度为 `60 + 9 × player_count + 3 × asset_count`；40 格、6 玩家、28 资产时为 198 字节，加上 32 字节帧头后为 230 字节。解码器继续兼容 schema 1：旧格式动态数组从偏移 56 开始，三个拍卖 Opening 字段按 0 处理。

| 偏移 | 长度 | 字段 | 说明 |
| ---: | ---: | --- | --- |
| 0 | 1 | schema | 当前为 2；解码兼容 1 |
| 1 | 1 | `phase` | 当前阶段 |
| 2 | 1 | `active_player_id` | 正常回合玩家 |
| 3 | 1 | `decision_player_id` | 当前必须操作的玩家 |
| 4 | 1 | `winner_player_id` | 未结束时为 0 |
| 5 | 1 | `board_size` | 16/24/32/40 |
| 6 | 1 | `player_count` | 1..6 |
| 7 | 1 | `asset_count` | 0..28 |
| 8 | 2 | `round` | 回合号 |
| 10 | 4 | `state_version` | 完整状态版本 |
| 14 | 4 | `last_event_sequence` | 生成该状态时的最新事件序号 |
| 18 | 4 | `board_id_hash` | 地图 ID UTF-8 字节的 IEEE CRC32 |
| 22 | 1 | `pending_move_flags` | bit0 active，bit1 passed_start |
| 23 | 1 | `pending_move_player_id` | 移动玩家 |
| 24 | 1 | `pending_move_origin` | 移动起点 |
| 25 | 1 | `pending_move_target` | 待确认终点；无时 `0xFF` |
| 26 | 1 | `pending_move_die_a` | 第一颗骰子 |
| 27 | 1 | `pending_move_die_b` | 第二颗骰子 |
| 28 | 1 | `pending_purchase_flags` | bit0 active |
| 29 | 1 | `pending_purchase_player_id` | 获得购买选择的玩家 |
| 30 | 1 | `pending_purchase_asset_index` | 无时 `0xFF` |
| 31 | 1 | `debt_flags` | bit0 active |
| 32 | 1 | `debt_debtor_id` | 债务人 |
| 33 | 1 | `debt_creditor_id` | 0 表示系统 |
| 34 | 1 | `debt_asset_index` | 无关联资产时 `0xFF` |
| 35 | 1 | `debt_payment_event` | 支付成功后产生的 EventKind |
| 36 | 1 | `debt_continuation` | 0 None、1 FinishLanding、2 ReleaseHoldAndMove |
| 37 | 1 | `debt_die_a` | 恢复流程使用的骰子 |
| 38 | 1 | `debt_die_b` | 恢复流程使用的骰子 |
| 39 | 1 | reserved | 固定 0 |
| 40 | 4 | `debt_amount` | int32 |
| 44 | 1 | `auction_flags` | bit0 active，bit1 Opening 屏障 |
| 45 | 1 | `auction_asset_index` | 无拍卖时 `0xFF` |
| 46 | 1 | `auction_landing_player_id` | 触发拍卖的玩家 |
| 47 | 1 | `auction_current_bidder_id` | 当前需要决定的竞拍者；Opening 时为 0 |
| 48 | 1 | `auction_highest_bidder_id` | 当前领先者 |
| 49 | 1 | `auction_passed_mask` | 已退出玩家位图 |
| 50 | 1 | `auction_ready_mask` | 已进入本轮拍卖介绍页的参与者位图 |
| 51 | 1 | `auction_required_ready_mask` | 本轮必须 Ready 的参与者位图 |
| 52 | 4 | `auction_current_bid` | int32；最低下一价为 0 时 10，否则当前价+10 |
| 56 | 4 | `auction_generation` | uint32；每次新拍卖使用新的非 0 代次 |
| 60 | `9×N` | players | 全部玩家动态状态 |
| 后续 | `3×A` | assets | 全部资产动态状态 |

每个完整玩家记录为：`player_id:u8, position:u8, cash:i32, flags:u8, failed_hold_rolls:u8, doubles_streak:u8`。每个资产记录按资产索引顺序排列：`owner_id:u8, building_level:u8, flags:u8`，资产 flags bit0 为抵押。

玩家屏幕必须使用 `board_size + board_id_hash` 选择本地共享的 `BoardCatalog` 静态地图，再将全资产数组覆盖到动态投影；哈希不匹配时禁止继续游戏并显示内容版本不兼容。

### 5.3 `RosterSnapshot`：玩家显示名

payload schema 1，长度为 `6 + 18 × player_count`：`schema:u8, state_version:u32, player_count:u8`，随后每名玩家为 `player_id:u8 + display_name[17]`。名称最多 16 个 UTF-8 字节并以 NUL 结束。每次状态同步事务都会发送，配对改名和新对局不会遗漏。

### 5.4 阶段

| 值 | 名称 |
| ---: | --- |
| 0 | Lobby |
| 1 | AwaitRoll |
| 2 | AwaitMoveConfirm |
| 3 | AwaitPurchase |
| 4 | AwaitAuction |
| 5 | AwaitDebt |
| 6 | TurnEnd |
| 7 | GameOver |

## 6. 动作

### 6.1 `ActionRequest` payload（12 字节）

| 偏移 | 长度 | 字段 |
| ---: | ---: | --- |
| 0 | 1 | schema 1 |
| 1 | 1 | action code |
| 2 | 1 | `player_id`，必须等于配对席位 |
| 3 | 1 | `asset_index`，不适用时为 `0xFF` |
| 4 | 4 | `argument`，int32 |
| 8 | 4 | `expected_state_version` |

`expected_state_version` 推荐始终填写最近快照版本；填 0 表示测试模式下不做版本栅栏。

`AuctionReady` 是例外：服务端不要求 `expected_state_version` 精确等于当前版本，因为其他屏幕的 Ready 也会推进版本。该动作由帧头 `room_id`、来源 peer 的已绑定席位、`player_id`、`asset_index` 与 `auction_generation` 共同约束。

| code | 动作 | 动作掩码位 | 参数 |
| ---: | --- | ---: | --- |
| 1 | Roll | 0 | 无 |
| 2 | ConfirmPosition | 1 | `argument=实际棋子位置` |
| 3 | Buy | 2 | 无 |
| 4 | Decline | 3 | 无 |
| 5 | EndTurn | 4 | 无 |
| 6 | PayHoldFee | 10 | 无 |
| 7 | Mortgage | 5 | `asset_index` |
| 8 | Unmortgage | 6 | `asset_index` |
| 9 | Build | 7 | `asset_index` |
| 10 | SellBuilding | 8 | `asset_index` |
| 11 | PayDebt | 11 | 无；现金足够时确认付款 |
| 12 | DeclareBankruptcy | 12 | 无；仅无力筹足时开放 |
| 13 | AuctionBid | 13 | `argument=出价金额` |
| 14 | AuctionPass | 14 | 无；退出本次拍卖 |
| 15 | AuctionReady | 15 | `asset_index=拍品`，`argument` 的 32 位原始值为 `auction_generation` |

屏幕只能展示并发送 `available_actions` 已开放的动作。位置确认必须使用 `pending_target` 对应的实物检测结果，不能仅因动画结束自动确认。`active_player_id` 表示正常回合归属，`decision_player_id` 表示当前必须操作的玩家；债务和拍卖中两者可能不同。

只允许 `ControllerKind::Bot` 自动调用动作。只要席位是 PlayerConsole，服务端会停在该玩家的债务、拍卖、购买、移动确认和回合结束状态，直到收到该屏幕的合法 ActionRequest；断线不会把该席位自动改成机器人。

### 6.1.1 拍卖 Opening 屏障

- 位图 bit n（从 0 开始）对应 `player_id=n+1`。`required_ready_mask` 在拍卖开始时冻结为本轮全部未破产参与者。
- Bot 和 `ControllerKind::Web` 由服务端自动 Ready；真人屏幕初始不 Ready。新拍卖会分配新的 generation 并清空旧 ready。
- Opening 判定为 `active && ((ready_mask & required_ready_mask) != required_ready_mask)`。Opening 期间 `current_bidder_id=0`，不得出价、退出或推进机器人。
- 真人屏幕进入并成功渲染当前拍卖介绍页后发送 `AuctionReady`。重连或全量 resync 后，若当前拍卖仍处于 Opening、自己的 required 位为 1 且 ready 位为 0，应使用快照中的相同资产与 generation 重发。
- 同一拍卖的重复 Ready 幂等，不增加状态版本，也不重复开启竞价；旧资产或旧 generation 会被拒绝。generation 或资产变化时必须取消旧待发请求。

### 6.2 `ActionResult` payload（12 字节）

| 偏移 | 长度 | 字段 |
| ---: | ---: | --- |
| 0 | 1 | schema 1 |
| 1 | 1 | result code |
| 2 | 1 | seat ID |
| 3 | 1 | reserved 0 |
| 4 | 4 | 执行后的 `state_version` |
| 8 | 4 | 原 ActionRequest 的 sequence |

结果码：0 Ok、1 InvalidPhase、2 InvalidPlayer、3 InvalidArgument、4 NotEnoughCash、5 NotOwner、6 RuleViolation、7 PositionMismatch、8 GameOver。无论结果是否成功，屏幕都应等待或主动请求下一份权威快照，不在本地预测现金和所有权。

服务端会缓存每个 peer 最近一条 `ActionRequest` 的 12 字节结果。若同一 sequence 的同一请求因丢包再次到达，服务端只重发缓存的 `ActionResult`，绝不再次执行游戏动作。屏幕等待 450ms 未收到结果时可以重发原始帧，但必须逐字节相同；建议最多发送 4 次，随后以 Heartbeat flags bit0 请求全量恢复。

### 6.3 `Ack` payload（12 字节）

服务端对每个通过鉴权和 payload 解码的 Heartbeat 都立即返回轻量 Ack：`schema=1:u8, reserved:u8+u16, state_version:u32, latest_event_sequence:u32`。Ack 帧头的 `acknowledgement` 等于 Heartbeat sequence，只确认该 Heartbeat 已收到；若屏幕累计版本落后、事件缺口或 flags bit0 请求恢复，服务端会在 Ack 之后额外排队增量或完整同步事务。状态完全一致时只发 Ack，避免每 2 秒重复发送四段快照。旧屏幕可以忽略 Ack 内容，但收到合法服务端帧时仍应刷新链路存活时间。

发送调度器严格限制同一时刻只有一个 `esp_now_send` 在途，等待发送回调后才调度下一帧。`PairAccept`、`ActionResult` 和 Heartbeat `Ack` 进入高优先队列，Discover 与各类投影进入普通队列；高优先响应最多尝试 3 次、投影最多 2 次、Discover 只尝试 1 次。这样 Heartbeat 与同为 2 秒周期的 Discover 不会在同一轮循环背靠背提交给无线驱动。

收到合法 `PairRequest` 后，服务端在 0/250/500ms 幂等发送三次 `PairAccept`，到 900ms 才把 peer 切换为加密模式并开始投影同步。玩家端处理首个 Accept 后可安全忽略重复帧；该时间窗用于覆盖 UI 忙、接收队列瞬时满或射频抖动。两端 USB HWCDC 日志均使用零发送超时，未打开串口监视器时不得阻塞配对时序。

### 6.4 `GameEvent` 事件批次

payload schema 1，基础长度 6 字节：`schema:u8, event_count:u8, state_version:u32`。随后每条事件固定 16 字节：

| 相对偏移 | 长度 | 字段 |
| ---: | ---: | --- |
| 0 | 4 | `event_sequence` |
| 4 | 1 | `event_kind` |
| 5 | 1 | `actor_id` |
| 6 | 1 | `target_id` |
| 7 | 1 | `asset_index`，无资产时 `0xFF` |
| 8 | 4 | `amount`，int32 |
| 12 | 4 | `detail`，事件专用位字段或数值 |

单帧最多 13 条事件，最大 payload 214 字节、整帧 246 字节。事件包含掷骰、移动请求与完成、经过起点、购买、租金、费用、卡牌、进入/离开限制区、抵押、赎回、建设、拍卖、债务、交易、破产、回合切换和游戏结束。屏幕按 `event_sequence` 去重，只用事件驱动动画与提示；现金、产权和阶段始终以 `AuthoritySnapshot` 为准。

## 7. 序号、去重与恢复

- 每次配对后从 sequence 1 开始单调递增；0 保留，不要在会话中回绕。
- 服务端记录每个屏幕最后接收序号。最近一次 ActionRequest 的重复帧命中幂等缓存时只重发原 ActionResult；其他重复或倒退帧触发 `FlagResync` 快照。
- `ActionResult.acknowledgement` 等于请求帧 sequence；payload 末尾也回显该 sequence。
- 状态、名单和事件帧的 `acknowledgement` 携带服务端已接收的屏幕累计 sequence，供后续客户端诊断或升级使用。
- 房间或设备 ID 不匹配的帧直接丢弃。
- 屏幕收到更高 `state_version` 时覆盖本地投影；收到旧版本时忽略。
- 新版 Heartbeat payload 固定 12 字节：`schema=1:u8, flags:u8, reserved:u16, applied_state_version:u32, applied_event_sequence:u32`。flags bit0 请求完整重同步。
- `applied_state_version` 与当前权威版本不等时，服务端发送 `FlagResync` 的个人、权威、名单和事件恢复事务。`applied_event_sequence` 小于最新事件且状态版本一致时，从最近 32 条事件历史中增量补发；超出历史窗口时同样设置 `FlagResync` 并以完整权威快照恢复最终状态。
- 为兼容早期测试客户端，0 字节 Heartbeat 仍会刷新状态，但不能确认事件；新玩家屏幕必须使用 12 字节格式。
- 9 秒（至少 3 个完整心跳窗口和无线抖动余量）无服务端合法帧时屏幕显示链路降级并立即通过 Heartbeat 请求恢复；15 秒失联时清空会话层状态并重新扫描 Discover。服务端也在 15 秒标记离线，但继续保留席位至失联满 60 秒。

## 8. 玩家控制屏幕实现清单

玩家屏幕会话应按以下顺序实现：

1. 直接复用 `GridopolyProtocol` 的帧和 payload codec，禁止创建第二套字段定义。
2. 新建固定容量 RX 队列；ESP-NOW 回调中不修改 UI 或游戏状态。
3. 实现 1～13 信道扫描、Discover 校验和同信道锁定。
4. 从本地忽略的 secret 配置读取相同测试 PSK，按本文件完成两阶段配对。
5. 持久化 server MAC 只能作为快速重连提示，服务端重启后仍以 Discover 为准。
6. 以 `seat_id` 过滤自身操作，以 `decision_player_id`、`available_actions` 驱动按钮和页面。
7. 复用协议库解码 `StateSnapshot`、`AuthoritySnapshot`、`RosterSnapshot` 和 `GameEventBatch`；禁止复制另一套 wire struct。
8. 对完整权威状态建立单向 reducer；事件只驱动动画和提示，不在屏幕本地运行规则。
9. 动作请求使用递增 sequence 和最近 `state_version`；超时只重发完全相同的字节。
10. 每 2 秒发送带累计应用版本的新版 Heartbeat；发现事件序号缺口时设置 flags bit0。9 秒（至少 3 个完整心跳窗口和无线抖动余量）无合法服务端帧显示重连态，15 秒进入重新扫描。
11. 实现人工筹款页：展示债权人和金额，允许出售建筑、抵押、付款与破产；交易入口待交易草案协议接入后开放。
12. 实现人工拍卖页：展示资产、当前价、最低出价和最高出价者，允许出价或退出。
13. 至少测试一实屏 + 三机器人、两实屏接管机器人席位、网页操作向全部屏幕同步、重复动作去重、事件丢帧恢复、错误位置、服务端重启和路由器信道变化。

## 9. 安全边界

- TEST_PSK、PMK、LMK、Wi-Fi SSID 和密码不得进入 Git、日志、网页或错误消息。
- Discover、PairRequest、PairAccept 可被旁听，只用于本地开发发现；业务帧必须加密。
- 当前 HTTP 网站无认证，不得暴露到公网。
- 正式产品必须把测试 PSK 配对替换为设备身份、一次性授权和密钥轮换；协议 v1 的 TEST_PSK 不能作为量产安全结论。
