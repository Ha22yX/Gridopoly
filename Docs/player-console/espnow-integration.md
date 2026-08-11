# 玩家控制屏幕 ESP-NOW 接入与全量同步

本文是玩家控制屏幕开发会话 `019fbb98-c4f6-7e20-9ccc-5f6dc5b00413` 的实施入口。逐字节线协议以 [测试服务端与玩家屏幕 ESP-NOW 协议 v1](../firmware/test-game-server-espnow-protocol.md) 和 `Firmware/libraries/GridopolyProtocol` 为唯一权威定义。

## 1. 必须复用的代码

玩家屏幕 Arduino 工程直接引用：

- `Firmware/libraries/GridopolyProtocol/src/gridopoly/protocol/Protocol.h`
- `Firmware/libraries/GridopolyProtocol/src/gridopoly/protocol/Protocol.cpp`
- `Firmware/libraries/GridopolyCore/src/gridopoly/core/BoardCatalog.*`，用于静态地图内容

禁止在屏幕工程重新声明消息号、字段偏移、CRC 或地图资产索引。真实 TEST_PSK 只放在屏幕本地且被 Git 忽略的 secret 文件中。

## 2. 接收状态模型

屏幕至少保存以下投影：

```text
ConnectionState
  server_mac, channel, room_id, seat_id
  last_rx_ms, next_tx_sequence

AuthoritativeProjection
  personal: StateSnapshot
  authority: AuthoritySnapshot
  roster: RosterSnapshot
  applied_state_version
  applied_event_sequence

PresentationQueue
  去重后的 GameEventRecord
```

ESP-NOW 接收回调只复制原始帧到固定容量队列。解码、状态替换、事件排序和 LVGL 更新全部在 UI/应用任务中进行。

## 3. 同步事务处理顺序

服务端一次更新会依次发送以下消息，多个屏幕之间采用轮询调度：

1. `StateSnapshot`：自己的现金、位置、动作按钮和当前格摘要。
2. `AuthoritySnapshot`：全部玩家、全部资产和全部待处理流程。
3. `RosterSnapshot`：玩家编号与显示名。
4. 一个或多个 `GameEvent`：本版本新增或需要补发的事件。

屏幕不要求四帧同时到达。每类快照先在临时对象中完整解码和校验，再按 `state_version` 原子替换对应投影：

- 版本小于已应用版本：忽略。
- 版本等于已应用版本：允许补齐名单、个人投影或事件，不重复播放已应用事件。
- 版本大于已应用版本：以 `AuthoritySnapshot` 覆盖动态状态。
- `board_id_hash` 不匹配本地 `BoardCatalog`：进入“不兼容内容”页，禁止发送游戏动作。
- 收到 `FlagResync`：清除未提交动画和临时选择，再使用快照恢复。

事件严格按 `event_sequence` 连续入队。重复事件忽略；出现缺口时不猜测中间过程，在下一次 Heartbeat 设置 resync bit。全量同步中的 `AuthoritySnapshot.lastEventSequence` 只登记目标，不得提前覆盖累计确认；首条保留历史可建立一次基线，之后必须逐号连续。其他玩家的 PRIVATE CardDrawn 使用全零 transport tombstone 占住序号，客户端明确忽略其表现。

## 4. Heartbeat 与累计确认

每 2 秒发送一个加密 Heartbeat，payload 使用 `encodeHeartbeat()`：

```cpp
gridopoly::protocol::Heartbeat heartbeat{};
heartbeat.flags = eventGapDetected ? 1 : 0;
heartbeat.appliedStateVersion = projection.appliedStateVersion;
heartbeat.appliedEventSequence = projection.appliedEventSequence;
```

只有事件已经由业务 reducer 从 transport 队列取出并安全应用后，才能推进 `appliedEventSequence`；仅成功解码或入队不能确认。服务端根据该累计序号重发缺失事件；若缺失范围已经超过最近 32 条历史，则使用 `FlagResync` 完整恢复最终状态。客户端事件队列必须容纳完整同步事务，并在读取 UDP 数据报前至少预留一个 13-event batch，避免半批消费。

服务端对每个通过鉴权和解码的 Heartbeat 都立即返回 12 字节 Ack；若累计版本落后、事件缺口或 resync bit 置位，再额外排队状态同步事务。Ack 只确认 Heartbeat 已收到，不代表同步事务已经完成。任何合法服务端帧（包括 Ack）都应刷新 `last_rx_ms`。连续 9 秒（至少覆盖 3 个完整心跳窗口和无线抖动余量）无服务端帧才进入“正在恢复”状态并发带 resync bit 的 Heartbeat；15 秒仍未恢复则清空会话并重新扫描。服务端会为同一 MAC 保留原席位 60 秒，所以正常短时断连不会换玩家编号。

## 5. 动作幂等与重发

屏幕同时只允许一个待确认动作。第一次发送前缓存完整编码帧；450ms 未收到匹配 `ActionResult` 时，最多重发到总计 4 次，sequence、room、payload 和 CRC 必须与第一次完全相同。重试期间暂停普通 Heartbeat，防止后发 sequence 越过待重发动作；达到重试上限后发送带 resync bit 的 Heartbeat。6.5 秒仍无结果才解除本地等待，并以权威快照决定最终状态。

服务端以“peer + ActionRequest sequence”为幂等键缓存最近一次结果：首次请求执行一次，重复请求只重发相同结果。ActionResult 的帧头 acknowledgement 与 payload 尾部都必须等于待确认动作 sequence；其他结果不能解除按钮锁。

## 6. UI 数据来源

| UI 内容 | 唯一来源 |
| --- | --- |
| 当前是否轮到自己、可用按钮 | `StateSnapshot.availableActions` |
| 所有玩家位置、现金、在线、限制区、破产 | `AuthoritySnapshot.players` |
| 全部产权、建筑等级、抵押 | `AuthoritySnapshot.assets` |
| 骰子点数和移动动画 | `AuthoritySnapshot.pendingMove` 与 DiceRolled/MoveRequested 事件 |
| 购买、租金、费用和卡牌提示 | `GameEvent` |
| 债务筹款页 | `AuthoritySnapshot` debt 字段 |
| 拍卖页 | `AuthoritySnapshot` auction 字段 |
| 玩家姓名 | `RosterSnapshot` |
| 玩家详情的资产与最近 10 条财务流水 | 按需 `PlayerDetailRequest/Response`，不订阅 |
| 静态格子名、价格和租金表 | 经 hash 校验的本地 `BoardCatalog` |

不要根据事件在本地计算新现金、产权或游戏阶段。事件用于说明“发生了什么”，AuthoritySnapshot 决定“现在是什么状态”。

### 拍卖 Opening/Ready

`AuthoritySnapshot` schema 2 在拍卖区增加 `auctionReadyMask:u8`、`auctionRequiredReadyMask:u8` 和 `auctionGeneration:u32`；`auctionFlags` bit1 表示 Opening。bit n 对应 `playerId=n+1`。

真人屏幕进入并成功渲染当前拍卖介绍页后，发送 12 字节 `ActionRequest`：`action=15 (AuctionReady)`、`playerId=自身席位`、`assetIndex=authority.auctionAssetIndex`、`argument` 的 32 位原始值为 `authority.auctionGeneration`、`expectedStateVersion=当前版本`。服务端以 room、来源席位、资产和 generation 校验，不要求 Ready 的 expected version 精确相等。

重复 Ready 可安全重发。重连或全量 resync 后，如果拍卖仍 active+Opening、自己的 required 位为 1 且 ready 位为 0，应为当前 asset+generation 重发；如果 asset 或 generation 改变，应取消旧待发 Ready。同一 room+generation+asset 的 1.8 秒介绍只播放一次且不能被重同步截断；之后保持在同一 Live 页面，Opening 未解除时显示 Ready 进度并禁用 Bid/Pass，不得回到介绍页。普通同房间重连不重播，只有新房间或新 generation 才重新介绍。本地也不得推测机器人出价。

### 玩家详情按需请求

进入玩家详情页时按[玩家详情按需查询协议](../firmware/player-detail-query-protocol.md)生成新的非零 request ID 并请求一次；超时重发同一请求，手动刷新才生成新 ID。响应只应用到仍打开、target 与 request ID 都匹配的页面，离开页面后丢弃迟到结果。禁止把详情加入 Heartbeat、常规快照 reducer 或定时轮询。

## 7. 全量同步验收

连接至少一块真实屏幕后，在网页和机器人侧分别触发以下变化；每项必须在所有连接屏幕出现相同最终状态，事件动画不得重复：

| 场景 | 应验证的同步内容 |
| --- | --- |
| 新建 16/24/32/40 格对局 | 地图大小、hash、名单、全部初始资产 |
| 掷骰与移动确认 | 两颗骰子、起点/终点、经过起点、全部棋子位置 |
| 购买或放弃 | 购买提示、资产 owner、现金、拍卖启动 |
| 租金、费用、卡牌 | actor/target、金额、现金、位置或限制区状态 |
| 抵押、赎回、建设、出售建筑 | 指定资产状态与现金 |
| 拍卖出价、退出、成交 | 当前竞拍者、领先者、passed mask、价格、最终 owner |
| 债务、筹款、付款、破产 | 债权人、金额、资产清算和玩家状态 |
| 交易 | 双方现金与资产 owner，以及 TradeSettled 事件 |
| 回合结束和游戏结束 | active/decision/winner、回合号 |
| 第二块屏幕加入或离线 | 所有屏幕的 connected flags 和名单 |
| 人为丢弃一个事件帧 | Heartbeat 后补发；超过历史窗口则完整 resync |

通过条件：所有屏幕最终 `state_version`、`last_event_sequence`、玩家数组和资产数组逐字段一致；任何屏幕不得独立运行游戏规则。
