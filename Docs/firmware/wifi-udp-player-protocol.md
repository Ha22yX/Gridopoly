# Gridopoly 玩家屏 Wi‑Fi/UDP 协议

状态：A 级当前规范
协议：Gridopoly Frame v1 + UDP Envelope v1
服务端固定地址：`10.42.0.1:4242`
最后更新：2026-08-07

## 1. 目标与边界

树莓派权威服务端和玩家圆屏通过本地 Wi‑Fi 传输。应用层继续使用
`Firmware/libraries/GridopolyProtocol` 中唯一的一套 Frame、快照、动作和事件 codec；
UDP 只替代 ESP‑NOW 的链路层，不改变游戏规则或 UI reducer。

- 一个 UDP datagram 必须只包含一个完整 Gridopoly Frame。
- 最大 inner frame 为 250 B；UDP envelope 为 48 B；最大 datagram 为 298 B。
- 常规状态仍由 `StateSnapshot + AuthoritySnapshot + RosterSnapshot + GameEvent` 组成。
- 动作与查询分别维护独立 pending；玩家详情不加入常规广播。
- UDP 丢包、重复、乱序不允许造成动作重复执行或状态倒退。

## 2. UDP Envelope v1

所有多字节整数均为 little-endian。

| 偏移 | 大小 | 字段 |
| ---: | ---: | --- |
| 0 | 4 | magic `0x31555047`（`GPU1`） |
| 4 | 1 | envelope version = 1 |
| 5 | 1 | flags：bit0 PairingKey，bit1 Broadcast |
| 6 | 2 | header size = 48 |
| 8 | 4 | sessionId；发现/PairRequest 为 0 |
| 12 | 4 | senderDeviceId |
| 16 | 8 | packetSequence |
| 24 | 2 | inner frame length |
| 26 | 2 | reserved = 0 |
| 28 | 4 | reserved = 0 |
| 32 | 16 | HMAC-SHA256 截断 tag |
| 48 | N | 原有 Gridopoly Frame |

tag 覆盖 envelope bytes `0..31` 与完整 inner frame；比较必须使用常量时间比较。
会话包使用 64 位 replay window，允许最多 63 个 packet 的乱序；重复或窗口外旧包直接丢弃。

## 3. 密钥派生

真实 PSK 只允许存在于圆屏本地忽略文件和树莓派 `/etc/gridopoly/server.env`，禁止进入
Git、日志、网页或错误消息。

```text
pairKey = HMAC-SHA256(UTF8(PSK), "GRIDOPOLY-UDP-PAIR-V1")

sessionKey = HMAC-SHA256(
  pairKey,
  "GRIDOPOLY-UDP-SESSION-V1" ||
  LE32(serverDeviceId) || LE32(clientDeviceId) || LE32(deviceNonce) ||
  LE32(roomId) || LE32(sessionId)
)
```

发现、PairRequest、PairAccept 使用 `pairKey`；配对完成后的所有包使用 `sessionKey`。

## 4. 发现与配对

1. 圆屏连接 SSID `gridopoly`，从 DHCP 获得 `10.42.0.0/24` 地址。
2. 服务端每秒向 `10.42.0.255:4242` 发送 PairingKey+Broadcast Discover。
3. 圆屏也可不等待广播，直接向 `10.42.0.1:4242` 发送 PairRequest。
4. PairRequest inner header 的 `deviceId` 必须等于 envelope `senderDeviceId`，roomId 必须来自当前 Discover。
5. 服务端按持久化 `deviceId -> seatId` 恢复席位；新设备分配尚未保留的席位。
6. PairAccept 使用 pairKey，envelope 与 payload 同时携带非零 sessionId。
7. 圆屏派生 sessionKey，随后发送 Heartbeat；服务端开始全量同步。

### 4.1 UDP Discover payload（固定 16 B）

UDP Discover 与历史 ESP‑NOW Discover 共用 `MessageType::Discover=0x01`，但 payload schema 不同。
树莓派 UDP 服务端只发送 schema `2`；UDP 客户端在迁移期可以同时接受 schema `1/2`，但不得假定
schema `1` 才是合法 Discover。

| 偏移 | 大小 | 类型 | 语义 |
| ---: | ---: | --- | --- |
| 0 | 1 | u8 | schema；树莓派 UDP 固定为 `2` |
| 1 | 1 | u8 | UDP flags/reserved；当前固定为 `0` |
| 2 | 1 | u8 | inner `FrameHeader` 协议版本；当前为 `kVersion` |
| 3 | 1 | u8 | 服务端支持的最大玩家席位数；当前为 `6` |
| 4 | 4 | LE32 | serverDeviceId；必须等于 inner header 与 envelope senderDeviceId |
| 8 | 4 | LE32 | roomId；PairRequest 必须回送当前值 |
| 12 | 4 | LE32 | 当前 stateVersion；只用于发现阶段提示，配对后以权威快照为准 |

UDP Discover 的 envelope 必须同时设置 `UdpFlagPairingKey | UdpFlagBroadcast`，`sessionId=0`，并用
`pairKey` 验证 HMAC。客户端应先验证 envelope、inner frame、三处 serverDeviceId 和 roomId，再更新
发现目标。ESP‑NOW schema `1` 的 byte 1 是无线信道、byte 3 是 peer capacity；这两个字段不得套用到
UDP schema `2`。

PairAccept payload schema v2 为 17 B：

| 偏移 | 大小 | 字段 |
| ---: | ---: | --- |
| 0 | 1 | schema = 2 |
| 1 | 1 | accepted |
| 2 | 1 | seatId |
| 3 | 1 | wifiChannel；UDP 固定 0 |
| 4 | 1 | reserved |
| 5 | 4 | serverDeviceId |
| 9 | 4 | stateVersion |
| 13 | 4 | sessionId |

decoder 继续接受 ESP‑NOW v1 的 13 B PairAccept，并令 sessionId=0；encoder 只发 v2。

通过 session HMAC 和 replay 校验后，来源 IP/UDP 端口允许变化，服务端更新 endpoint，席位不变。
树莓派或 AP 重启会使 room/session 失效；圆屏回到发现状态，以 deviceId 自动恢复原席位。

## 5. 心跳、同步与重发

- 圆屏每约 2 s 发送 Heartbeat，携带已应用 stateVersion 与 eventSequence。
- 同版且无缺口时，服务端返回 Ack。
- 版本不同、event gap、FlagResync 或 projectionIncomplete 时，服务端发送全量同步。
- 推荐圆屏 UI degraded 门限 9 s，session reset/re-pair 门限 15 s。
- Pair 后首次投影与任何全量重同步都保持同一顺序：Identity → 私有 pending CardDrawn replay（如有）
  → State → Authority → Roster → Trade resync → events。Identity 不替代游戏投影；即使处于
  AvatarSetup/Countdown，服务端仍发送完整序列，便于客户端先进入正确的开局顶层页面，同时保留统一的
  Active 切换基线。
- AvatarSetup/Countdown 阶段，Heartbeat 的 `appliedStateVersion` 表示客户端已经完整应用的
  `IdentitySnapshot.stateVersion`；`projectionIncomplete` 只表示 Identity 投影未完成。服务端在该阶段
  不要求 State/Authority/Roster/Trade 或 event cursor 完整，也不使用 `appliedEventSequence` 判定同步。
  Identity 同版且完整时直接 Ack；版本落后或 incomplete 时重新发送上述全量序列。进入 Active 后恢复
  State/Auth/Roster、Trade 与事件游标的常规完整性规则。
- 任何投影只允许单调应用；同一 stateVersion 的 State/Auth/Roster 必须全部完成后才清除 incomplete。
- Authority 的 `lastEventSequence` 只是本轮同步目标，不能直接写入 Heartbeat 的累计确认。
  只有事件已经被客户端业务 reducer 取出并应用后，`appliedEventSequence` 才能推进。
- 全量历史最多保留 32 条，可能跨 3 个或更多 `GameEvent`/`PlayerCardEvent` 数据报。
  客户端必须在解析新数据报前预留一个完整 13-event batch 的队列空间，不允许半批入队。

ActionRequest 保持一个动作在途。重发必须使用相同 inner frame sequence 与相同请求内容，但使用新的
UDP packetSequence。服务端按 inner sequence 回放缓存 ActionResult，不再次执行动作。

PlayerDetailRequest 使用独立 requestId 缓存，700 ms 可重发，建议最多 4 次、4 s 失败；同 requestId
内容发生变化视为碰撞并拒绝。该查询不阻塞 Heartbeat 或 ActionRequest。

## 6. 卡片两阶段投影

领域事件必须复用生成契约：`CARD_DRAWN=0x0240` 与
`CARD_EFFECT_APPLIED=0x0241`。玩家链路使用固定 32 B
`MessageType::PlayerCardEvent=0x26`，避免让圆屏解析完整 TLV。

| 偏移 | 大小 | 字段 |
| ---: | ---: | --- |
| 0 | 1 | schema=1 |
| 1 | 1 | stage：1 Drawn，2 EffectApplied |
| 2 | 2 | canonical domain event type |
| 4 | 4 | stateVersion |
| 8 | 4 | eventSequence |
| 12 | 1 | playerId |
| 13 | 1 | deckId：1 Chance，2 Community Chest |
| 14 | 1 | cardIndex 0..7 |
| 15 | 1 | flags：bit0 keepable，bit1 replay |
| 16 | 2 | cardInstanceId |
| 18 | 2 | cardCatalogId |
| 20 | 2 | effectId |
| 22 | 4 | signed amount；付款为负 |
| 26 | 1 | targetPlayerId |
| 27 | 1 | targetPosition |
| 28 | 1 | outcome；Drawn=0，Applied=1成功/2部分/3破产/4取消 |
| 29 | 3 | reserved=0 |

`ActionCode::CardContinue=16`，argument 放 cardInstanceId。权威顺序：

```text
付款卡：CardDrawn → CardContinue → DebtOpened → DebtPaid → CardEffectApplied
奖励卡：CardDrawn → CardContinue → 现金变更 → CardEffectApplied
移动卡：CardDrawn → CardContinue → 移动/状态变更 → CardEffectApplied → 后续落点
```

CardDrawn 是 PRIVATE，完整 `PlayerCardEvent/Drawn` 只发目标 seat；CardEffectApplied 是 PUBLIC。
为了让所有 seat 的全局 `eventSequence` 连续，旁观 seat 会在普通 `GameEvent` batch 中收到一条
transport sequence tombstone：`sequence` 保留，`kind=27`，`actorId=0`、`targetId=0`、
`assetIndex=0xFF`、`amount=0`、`detail=0`。该墓碑不是 canonical CardDrawn 揭示，不包含任何
PRIVATE 字段，不进入活动列表，也不得触发抽卡 UI。CardDrawn 本身不产生财务副作用。

## 7. AuthoritySnapshot v3 pending-card

v3 保持 v2 bytes `0..59` 不变，新增 bytes `60..79`，动态玩家/资产数组从 80 开始。

| 偏移 | 大小 | 字段 |
| ---: | ---: | --- |
| 60 | 1 | flags |
| 61 | 1 | playerId |
| 62 | 1 | deckId |
| 63 | 1 | cardIndex |
| 64 | 2 | cardInstanceId |
| 66 | 2 | cardCatalogId |
| 68 | 2 | effectId |
| 70 | 4 | displayAmount |
| 74 | 1 | targetPlayerId |
| 75 | 1 | targetPosition |
| 76 | 4 | drawEventSequence |

flags：bit0 active，bit1 revealed，bit2 continueAccepted，bit3 awaitingSettlement。
`0x03` 表示已揭示等待 Continue，`0x0F` 表示 Continue 已接受、等待结算。满配 6 人/28 资产时
payload=218 B，连 FrameHeader 后正好 250 B。decoder 接受 v1/v2/v3；encoder 只发 v3。

## 8. 稳定卡片 ID

`effectId == cardCatalogId`。数值和 ID 冻结，展示文案可由内容目录更新但不得改变 ID。

| deck | catalog | index | 当前效果 |
| --- | ---: | ---: | --- |
| Chance | 1..8 | 0..7 | +100、-50、前往起点、前往拘留区、后退3格、-75、+150、-100 |
| Community Chest | 9..16 | 0..7 | +50、-25、前往起点、前往拘留区、后退3格、-50、+100、+75 |

## 9. 开局身份控制面

身份流程使用独立低频消息，不扩大 State、Authority 或 Roster：

| MessageType | 值 | 方向 |
| --- | ---: | --- |
| IdentityRequest | `0x29` | 圆屏 → 权威服务端 |
| IdentitySnapshot | `0x2A` | 权威服务端 → 圆屏 |

### 9.1 IdentityRequest（schema 1，固定 44 B）

| 偏移 | 大小 | 字段 |
| ---: | ---: | --- |
| 0 | 1 | schema=`1` |
| 1 | 1 | operation：Query=`1`、ConfirmAvatar=`2`、ConfirmName=`3` |
| 2 | 1 | playerId，mutation 必须等于已认证 seat |
| 3 | 1 | flags/reserved=`0` |
| 4 | 4 | requestId，非零 |
| 8 | 4 | expectedStateVersion |
| 12 | 2 | expectedSeatRevision |
| 14 | 2 | avatarCatalogVersion；V1=`1` |
| 16 | 1 | hairPresetId，`1..10` |
| 17 | 1 | hairColorId，`1..20` |
| 18 | 1 | facePresetId，`1..10` |
| 19 | 1 | skinToneId，`1..8` |
| 20 | 1 | outfitPresetId，`1..10` |
| 21 | 1 | nameLength，`0..16` UTF-8 bytes |
| 22 | 17 | name 与零填充 |
| 39 | 5 | reserved=`0` |

Query 的版本、配方和姓名字段必须为零；mutation 要求精确且非零的 stateVersion 与
seatRevision。ConfirmAvatar 只携带完整配方；ConfirmName 只携带姓名。

### 9.2 IdentitySnapshot（schema 1，固定 182 B）

| 偏移 | 大小 | 字段 |
| ---: | ---: | --- |
| 0 | 1 | schema=`1` |
| 1 | 1 | roomPhase：AvatarSetup=`1`、Countdown=`2`、Active=`3` |
| 2 | 1 | selfStage：AvatarSetup=`1`、Generating=`2`、Name=`3`、Ready=`4`、Countdown=`5`、Active=`6` |
| 3 | 1 | IdentityResultCode（见下表） |
| 4 | 4 | requestId；主动全量投影为 `0` |
| 8 | 4 | stateVersion |
| 12 | 4 | identityRevision |
| 16 | 8 | serverEpochMs |
| 24 | 8 | countdownDeadlineEpochMs；非倒计时为 `0` |
| 32 | 2 | avatarCatalogVersion=`1` |
| 34 | 1 | playerCount，`2..6` |
| 35 | 1 | selfPlayerId |
| 36 | 1 | requiredHumanMask |
| 37 | 1 | avatarFinalMask |
| 38 | 1 | nameFinalMask |
| 39 | 1 | readyMask |
| 40 | 1 | onlineMask |
| 41 | 1 | operationEcho；主动全量投影为 `0` |
| 42 | 1 | flags：bit0 Replay、bit1 Resync |
| 43 | 1 | reserved=`0` |
| 44 | 138 | 6 个固定 23 B seat record |

每个 seat record 位于 `44 + index*23`：playerId@0、flags@1、seatColorId@2、reserved@3、
seatRevision@4、avatarRevision@6、avatarContentHash64@8、avatarCatalogVersion@16，五个配方
ID 依次位于 18..22。未完成合成的真人配方不得出现在公共 Snapshot；Generating 只公开状态位。

同一 requestId 与逐字节相同的 44 B 请求必须幂等回放；同 ID 不同内容返回
`RequestIdConflict=10`。结果码冻结如下：

| 值 | 名称 |
| ---: | --- |
| 0 | Ok |
| 1 | InvalidRequest |
| 2 | Unauthorized |
| 3 | StateVersionStale |
| 4 | SeatRevisionStale |
| 5 | CatalogMismatch |
| 6 | InvalidRecipe |
| 7 | InvalidName |
| 8 | DuplicateName |
| 9 | NotAllowed |
| 10 | RequestIdConflict |
| 11 | AvatarGenerationFailed |

完整重同步先发 IdentitySnapshot，再按第 5 节顺序发送其余游戏投影。服务端倒计时使用
`serverEpochMs` 与持久化 deadline，所有圆屏显示同一 5 秒时间线。ConfirmName 成功会在同一权威事务中
写入 `PlayerState.name`、推进 `stateVersion/identityRevision/seatRevision`，随后立即触发所有已连接屏幕的
Identity + Roster 全量投影；未确认姓名的真人席位在 Roster 中保持空名，UI 使用 P1/P2 等席位占位，不得
提前公开草稿姓名。幂等回放保持原权威结果、版本和 deadline 不变，但重新采样 `serverEpochMs`，因此客户端
用 `deadline-serverEpochMs` 换算本地单调时钟截止点时不会因请求重试而重新开始 5 秒。

### 9.3 头像 HTTP 资产

玩家编辑阶段的预览不进入固件，也不进入 UDP Snapshot。正式圆屏按结构 preset 分别请求中性组件：

```text
GET /assets/avatar-components/v1/hair/h<hair>.gavc
GET /assets/avatar-components/v1/face/f<face>.gavc
GET /assets/avatar-components/v1/outfit/o<outfit>.gavc
```

- Hair/Face/Outfit 仅在对应 preset 变化时请求；HairColor/SkinTone 由圆屏按冻结定点算法即时着色，
  不产生 HTTP 请求。
- GAVC 使用 220×300 坐标系、cropped straight-alpha RGBA8888 RLE；精确 32 字节头、颜色表、
  source-over、golden vectors 与缓存上界见 [头像组件流协议](avatar-component-protocol.md)。
- 合法 ID 为 1..10 且禁止前导零；成功响应为 `application/octet-stream`、一年 immutable 与强 ETag。
- 客户端采用“最后一次 kind+preset+recipe generation 胜出”；迟到响应可入缓存，但不能覆盖当前配方。

以下完整配方接口保留兼容旧客户端，不再是正式圆屏编辑主路径：

```text
GET /assets/avatar-previews/v1/h<hair>-c<hairColor>-f<face>-s<skin>-o<outfit>.rgb565
```

- 固定 `220×300`，RGB565 little-endian，无文件头，`Content-Length: 132000`。
- 透明区域由服务端预合成到 `#061017` 深色背景。
- 仅 V1 合法 ID 范围可访问；非法数字、前导零、额外后缀、路径穿越或畸形文件返回 404。
- 成功响应为 `application/octet-stream`、`public, max-age=31536000, immutable`，并携带强 ETag。
- 服务端以规范 recipe key 在磁盘缓存；旧客户端异步请求时必须采用“最后一次 recipe key 胜出”，旧响应不能
  覆盖更新后的配方。

ConfirmAvatar 后由服务端异步生成公共最终头像，只有原子发布完成后才设置 AvatarFinal：

```text
GET /assets/avatars/<roomId>/p<playerId>-a<avatarRevision>-<hash16>.rgb565
GET /assets/avatars/<roomId>/p<playerId>-a<avatarRevision>-<hash16>.png
```

- `.rgb565` 固定 `128×128`、RGB565 little-endian、无文件头，`Content-Length: 32768`。
- `.png` 固定 `128×128` 圆形 RGBA，供网页使用。
- 两种格式共享 URL 中的 16 位小写十六进制内容哈希、强 ETag 与一年 immutable 缓存；畸形长度、非法
  room/player/revision/hash 或尚未发布的文件均返回 404。
- 圆屏缓存键为完整 immutable URL（等价于 roomId + playerId + avatarRevision + hash16）；新 room 必须
  清除旧身份引用，但磁盘/PSRAM 内容可按 immutable key 安全复用。

## 10. 安全与拒绝条件

以下任一情况必须静默丢弃或返回协议错误，不能执行动作：HMAC 错误、reserved 非零、未知 session、
senderDeviceId 不匹配、room 不匹配、replay、seat/player 不匹配、旧 stateVersion、未知动作、
同 requestId 内容碰撞。日志只允许输出计数、room、sessionId、deviceId 标签和版本，不输出密钥。
