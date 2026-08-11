# Gridopoly 双向交易按需协议 v1

状态：冻结（2026-08-05）
单一实现来源：`Firmware/libraries/GridopolyProtocol`、`GridopolyCore`、
`Server/RaspberryPi/src/UdpPlayerServer.cpp`

## 1. 边界

- 交易使用独立 `TradeRequest=0x27` / `TradeResponse=0x28`，不占用
  `ActionRequest`，也不进入 State/Authority/Roster 常规负载。
- 一个 UDP datagram 仍只承载一个 Gridopoly Frame；请求最大 60 B，响应最大 68 B，
  均远低于 218 B payload 上限。
- 请求来源玩家由已认证 UDP session 的 `seatId` 决定。payload 不允许自报 actor。
- v1 每名玩家最多参与一笔未关闭交易；最多三对互不重叠的玩家并发交易。
- 本次仅支持普通交易。`AwaitDebt` 强制筹资交易留给后续版本。

## 2. MessageType 与操作号

| 名称 | 编号 |
| --- | ---: |
| `TradeRequest` | `0x27` |
| `TradeResponse` | `0x28` |

`TradeOperation`：

| 名称 | 编号 | 语义 |
| --- | ---: | --- |
| `Query` | 1 | 查询本人当前参与的未关闭交易 |
| `Create` | 2 | 创建报价；创建者自动确认 revision 1 |
| `Update` | 3 | 修改或反向报价；revision +1，仅编辑者确认 |
| `Confirm` | 4 | 确认指定 tradeId/revision；双方确认后原子结算 |
| `Reject` | 5 | 非当前编辑者拒绝报价 |
| `Cancel` | 6 | 当前编辑者撤销报价 |

不新增 `ActionCode`。交易拥有自己的较大 payload、pending、重试和幂等缓存，不能降级成
12 B `ActionRequest`。

设计记录中的 16 位 `TRADE_CREATE=0x1007`、`TRADE_UPDATE=0x1008`、
`TRADE_REJECT=0x1009`、`TRADE_CONFIRM=0x100A` 保留为完整主控领域命令；轻量圆屏链路不直接
传输这些大命令，而由上表 operation 做一一投影。生成领域事件 `TRADE_CREATED=0x0230`、
`TRADE_UPDATED=0x0231`、`TRADE_CLOSED=0x0232` 的语义保持不变；圆屏只消费固定小 payload
和紧凑 GameEvent 投影。

## 3. TradeRequest schema 1

所有多字节整数为 little-endian；固定头 32 B，随后两个资产索引数组。

| 偏移 | 大小 | 类型 | 字段 |
| ---: | ---: | --- | --- |
| 0 | 1 | u8 | schema = 1 |
| 1 | 1 | u8 | operation |
| 2 | 1 | u8 | targetPlayerId；Query 可为 0 |
| 3 | 1 | u8 | selfAssetCount，0..28 |
| 4 | 1 | u8 | counterpartyAssetCount，0..28 |
| 5 | 1 | u8 | reserved = 0 |
| 6 | 2 | u16 | expectedRevision；Create/Query 为 0 |
| 8 | 4 | u32 | requestId，非零 |
| 12 | 4 | u32 | expectedStateVersion；Query 可为 0，所有 mutation 必须非零且精确匹配 |
| 16 | 4 | u32 | tradeId；Create/Query 可为 0 |
| 20 | 4 | i32 | selfGivesCash，非负 |
| 24 | 4 | i32 | counterpartyGivesCash，非负 |
| 28 | 4 | bytes | reserved = 0 |
| 32 | N | u8[] | selfAssets |
| 32+N | M | u8[] | counterpartyAssets |

`N+M <= 28`。每个资产索引必须小于 28，同一请求的两个列表内及列表之间不得重复。
Confirm/Reject/Cancel/Query 的现金和资产列表必须全为零。Update 的字段始终为发送者视角，
服务端负责转换成稳定的原始 proposer/counterparty 方向。

## 4. TradeResponse schema 1

响应固定头 40 B，随后两个资产索引数组。字段始终为接收者视角。

| 偏移 | 大小 | 类型 | 字段 |
| ---: | ---: | --- | --- |
| 0 | 1 | u8 | schema = 1 |
| 1 | 1 | u8 | operation；主动恢复投影为 Query |
| 2 | 1 | u8 | result |
| 3 | 1 | u8 | status |
| 4 | 1 | u8 | flags |
| 5 | 1 | u8 | selfPlayerId |
| 6 | 1 | u8 | counterpartyId |
| 7 | 1 | u8 | selfAssetCount |
| 8 | 1 | u8 | counterpartyAssetCount |
| 9 | 1 | u8 | reserved = 0 |
| 10 | 2 | u16 | revision |
| 12 | 4 | u32 | requestId；主动恢复/通知为 0 |
| 16 | 4 | u32 | authoritative stateVersion |
| 20 | 4 | u32 | tradeId |
| 24 | 4 | u32 | expiresInMs；客户端只用于显示，服务端裁决截止时间 |
| 28 | 4 | i32 | selfGivesCash |
| 32 | 4 | i32 | counterpartyGivesCash |
| 36 | 1 | u8 | confirmedMask；bit(playerId-1) |
| 37 | 1 | u8 | originatorId（最初 Create 的玩家） |
| 38 | 2 | bytes | reserved = 0 |
| 40 | N | u8[] | selfAssets |
| 40+N | M | u8[] | counterpartyAssets |

flags：bit0 self confirmed；bit1 counterparty confirmed；bit2 self originated；
bit3 self last edited；bit4 resync/主动通知；bit5 terminal；bit6 请求 stateVersion 已过期。

status：0 None；1 Offered；2 Countered；3 Settled；4 Rejected；5 Cancelled；
6 Expired；7 Invalidated。

result：0 Ok；1 NoActiveTrade；2 InvalidRequest；3 Unauthorized；4 StateVersionStale；
5 RevisionStale；6 ParticipantBusy；7 RuleViolation；8 NotEnoughCash；
9 AssetUnavailable；10 Expired；11 RequestIdConflict。

紧凑 `GameEvent.kind`：`TradeSettled=21`（既有）、`TradeCreated=28`、
`TradeUpdated=29`、`TradeClosed=30`。完整报价只存在于 TradeResponse，紧凑事件不承载资产数组。

## 5. 状态机

```text
None
  --Create--> Offered(revision=1, creator confirmed)
Offered/Countered
  --Update--> Countered(revision+1, editor only confirmed)
  --Confirm(first side)--> same revision
  --Confirm(both)--> Settled + atomic cash/assets swap
  --Reject(receiver)--> Rejected
  --Cancel(editor)--> Cancelled
  --120s--> Expired
  --ownership/cash/rule changed before final confirm--> Invalidated
```

普通交易只允许在 `AwaitRoll` 或 `TurnEnd` 变更/结算，避免与移动、购买、拍卖、卡片和债务
事务交叉。Query 始终允许。双方不得相同且均不得破产。

最终 Confirm 必须重新验证：双方现金、所有权、资产未抵押、资产 buildingLevel=0；若为地产，
该颜色组所有地产也必须没有建筑。资产和现金先在只读候选上完整校验，全部合法后才一次性提交，
不存在半笔交易。

机器人参与者使用确定性响应策略，并与真人走同一 revision/确认/原子结算路径：对自身经济价值
不亏损的报价直接 Confirm；不利报价最多自动 Update 反价一次，使收到价值至少覆盖付出价值；若
对方再次提交不利 revision、对方现金无法满足反价或报价已经失效，则 Reject。机器人反价次数随
交易持久化，服务重启后不会重新开始无限反价。机器人动作引起 stateVersion 变化后，客户端通过
下一次 Heartbeat 触发的按需 TradeResponse resync 获取结果，常规三类快照仍不增加交易负载。

## 6. UI 行为映射

- “只领取”发送 Confirm。
- “领取并回赠”进入编辑，提交完整发送者视角 Update；Update 后发送者已确认，等待另一方 Confirm。
- 首页 Trade：target 可编辑。
- Players 详情 Trade：target 锁定。
- AssetDetail Trade：target 可编辑且 selfAssets 预选当前资产。
- Asset Detail 根据权威资产状态动态组织 Mortgage/Redeem、Build、Sell Building、Trade：
  Mortgage 在已抵押时变为 Redeem；Build 仅完整拥有颜色组时显示；Sell Building 仅当前地产
  有建筑时显示；Trade 仅资产未抵押时显示。显示条件不等于最终合法性，Trade 仍要求该颜色组
  无建筑，Build 仍由 Core 的完整颜色组、无抵押、均匀建设和现金规则最终裁决。
- Asset Detail 使用固定 2×2 语义槽位，不因隐藏项而重排：slot0 左上 Mortgage/Redeem，slot1
  右上 Build，slot2 左下 Sell Building，slot3 右下 Trade。隐藏槽保持为空，旋钮焦点自动跳过；
  其他操作始终留在原槽位。
- 正式交易后端仅为 Raspberry Pi `WifiUdpPlayerTransport`。ESP-NOW 客户端保留相同 codec 以便
  后续适配，但当前 ESP32 `TestGameServer` 不处理 0x27/0x28；回退构建必须禁用交易入口并明确
  提示需要 Wi-Fi 服务端，不能静默发送。

## 7. 幂等、重试与恢复

- 客户端一次只允许一个 TradeRequest pending；建议 700 ms 重发，最多 4 次，4 s 失败。
- 重发必须保留相同 requestId 和完全相同 payload，但使用新的 UDP packetSequence/inner sequence。
- 收到 StateVersionStale/RevisionStale 后应先应用最新投影，再用新的 requestId 构造新请求；不得
  在旧 requestId 下修改 expectedStateVersion、revision 或报价内容。
- 服务端 session 缓存最近 TradeRequest 原始字节和 TradeResponse。相同 requestId+逐字节相同
  payload 直接回放；相同
  requestId+不同 payload 返回 RequestIdConflict，绝不执行。
- tradeId 防止操作另一笔交易；expectedRevision 防止确认旧报价；expectedStateVersion 防止在
  已变化的资产/现金快照上提交 mutation。
- 已确认方对同一 active revision 再次 Confirm 为业务幂等：返回当前报价且不推进 stateVersion；
  不会因为客户端换了 requestId 就重复结算或制造无意义版本。
- Create/Update/Confirm/Reject/Cancel 成功响应直接回发送者，并向在线对手发送 requestId=0、
  FlagResync 的接收者视角通知。
- 配对或全量 resync 时，服务端在 Roster 后、GameEvent 前发送本人当前未关闭交易；没有活动
  交易也必须发送 `Query + requestId=0 + FlagResync + NoActiveTrade`，显式清除客户端可能残留的
  旧报价。断线端也可主动 Query。交易数据不加入常规快照。
- 终态通过即时 TradeResponse 和 `TradeSettled/TradeClosed` GameEvent 恢复；`TradeClosed`
  的 `detail=tradeId`、`amount=TradeWorkflowStatus`。经济结果同时由
  后续 State/Authority 的现金和资产所有权验证。

## 8. 持久化与诊断

树莓派状态存储 schema 为 3，持久化最多三笔交易和 nextTradeId；schema 2 游戏存档可原位迁移，
已有房间和游戏进度不会因此重建。`/health.udp.trade` 提供请求、响应、回放、错误、最近
requestId/tradeId/revision/operation/result/responseBytes 计数，不记录报价隐私或 PSK。
