# 完整棋盘闭环、自动席位与 Tag 配对契约

初版2026-09-26；全在线闭环与16/24/32/40格接口及现役状态更新于2026-10-15。Pi仍部署 `0fed0ce` 对应 `ea6e1f51…`，格子V0.34。当前16实体闭环、0～15自动映射及四角0/4/8/12已有实证，USB热点20台同时关联/注册与Pi软件重启恢复已验；重启后持续观察按实际开启的16格＋3屏执行。见[服务端构建与部署](server-all-online-ring-progress-2026-10-15.md)、[USB实机记录](usb-ap-deployment-2026-10-15.md)。

24/32/40实体环、32格＋4屏＝36台、锚点改派/实体断环重接、四真人完整配对与实体整局仍待验。软件模拟、20台网络关联和16 reader stable分别是各自范围的证据；不能互相替代。冻结固件范围见[16格试玩报告](trial16-release-2026-10-15.md)。

## ORDER 闭环与角色

模块 heartbeat 顶层新增 `moduleRole`（`long`、`corner`、`unknown`）和诊断用 `firmwareVersion`。角色来自显式固件 profile；GPIO13、INA226、MAC 不用于猜测机械形状。物理 `order.version=1` 波形与其八个字段保持原定义。

服务端自动编号必须同时满足：所有当前在线格子模块属于同一个 ORDER 完整环、模块数等于当前游戏地图格数、每个当前 boot 唯一、上下游一对一且物理证据未过期。在线按模块心跳的 15 秒租约定义；已过期的历史设备不计入，在线但没有 ORDER 的旧模块、额外孤岛、第二个环或未知板型不能被悄悄排除。物理闭环与是否允许分配分开记录：已闭环但地图不匹配时仍显示实际环大小，并暂停该环映射。

| 当前地图格数 | long / corner | 两角之间的 long | 角落 mapIndex |
| --- | --- | --- | --- |
| 16 | 12 / 4 | 3 | 0 / 4 / 8 / 12 |
| 24 | 20 / 4 | 5 | 0 / 6 / 12 / 18 |
| 32 | 28 / 4 | 7 | 0 / 8 / 16 / 24 |
| 40 | 36 / 4 | 9 | 0 / 10 / 20 / 30 |

还须读取实际 `BoardDefinition.tiles`：Start/Hold/Rest/GoToHold 各一个且位于等距四角，START 为索引 0；数量相同不代表布局正确。根是稳定 moduleId 字典序最小的 corner，沿 ORDER_OUT 排列 mapIndex 0..N-1。随机 bootId 与上线先后不决定根。无需先人工指定格子，最后一条有效闭环证据到达后即可自动分配；地图切换、模块入网/过期与重连时重新计算。未知板型、错误角落数量/间距或不匹配的地图结构均不自动映射。

环的手动锚点只能一致旋转同一相位，不能破坏 corner 位置或让多个锚点相互冲突。旧开放链仍支持分段手动锚点。曾验证为环的模块断环后撤回推导分配，不能静默降级成开放链自动编号。开放链中的旧锚点若在闭环后不一致，保留其意图供纠正，同时撤回该环全部显示分配。

`/api/tile-debug/assignments` 中 module 增加 `moduleRole`、`firmwareVersion`、`closedRing`；order.chains 中增加 `closedRing`。旧 orderStatus/conflict/anchor 字段继续提供诊断。

分配条件不满足时，环内模块的 `orderConflict` 区分 `ring_online_set_mismatch`（存在环外在线模块）、`ring_size_mismatch`（当前地图数量不匹配）、`ring_board_layout_mismatch`（地图实际角落布局不兼容）。`ring_or_tile_conflict` 保留表示手动相位或占位冲突。前三者不会将真实 `closedRing` 改为 false；网页按物理链的模块列表显示环大小。在线全集不符时撤回派生分配，显式手动意图保留；数量/地图布局或环内锚点冲突时暂停整环显示分配，锚点意图仍可修正。

## 新房间与重启边界

新room/换图清旧assignment、manual锚点及旧Tag扫描显示；仍新鲜的物理ORDER证据保留，并按新地图和全部在线集合重新校验。匹配环立即自动映射；尺寸/角色/在线全集不符仍撤销，不用延时或保留旧图掩盖无效分配。开放链等待新锚点。服务器进程重启则重新注册并获取物理证据，不能将最终映射恢复解释为manual锚点已持久化。

2026-10-15同尺寸16格重开局：150组观察保持全部在线与逐模块映射，独立30秒230次真实heartbeat全部正确，用户确认正常；首次部分屏等待格子的原因仍未知，本轮未改源码或部署。HTTP分别读取 `stateCopy()`／`roomId()` 的上下文窗口另列待复现风险，不能据此认定本次原因或宣称已有修复。既有顺序换图回归不覆盖确定性的并发交错，详见[重开局观察](newgame16-assignment-observation-2026-10-15.md)。

## 发现与开局

`GET /api/player-devices` 返回 `{maxPlayers:4,devices:[{deviceId,displayName,online,seatId,capabilities,eligible}]}`，eligible 是布尔值。在线边界是最近 15 秒收到有效已认证流量；旧协议设备可显示，但不计入自动开局。

新能力位 `0x02` 表示 Tag 配对，原 Wi-Fi UDP 位为 `0x01`。新屏的 deviceId 为六字节完整 MAC 的 FNV-1a32（ESP.getEfuseMac 低字节在先，hash=0 映射为1）；PairRequest.displayName 为 `GP-` 加同序六字节十二位十六进制。服务端复算并拒绝身份冲突。设备 boot nonce 不作为稳定身份。新握手的 nonce/packetSequence 重放不能刷新 online。

`POST /api/new?size=32&autoPlayers=1` 从当前合格在线屏冻结 1..4 个真实席位，按稳定 deviceId 排序，机器人补到总共4席。size 默认32，也接受 BoardCatalog 支持的16、24、40；其它尺寸拒绝且不改变房间。零合格屏或多于4屏返回 HTTP409，message 分别为 `no_eligible_players`、`too_many_players`，不改变房间。自动人数与 Tag 配对不要求实体闭环，单块未分配 reader 或开放链在四种地图上同样可用。完整环的角色/角落规则约束地图分配，不是 reader 配对的前提。显式 humans/bots 调试接口保留旧行为。

冻结的 full identity 与 seat 持久化；重连/设备重启不抢占其它座位，晚加入的设备不能挤占机器人。旧截断 deviceId 不自动偷迁已占座位；新固件即使在旧局被拒绝席位，仍能通过发现接口计入下一自动新局。

`devices` 的实时 eligible/online 与 `sync.frozenDevices` 的本局席位是两份不同视图。即使后来有四屏在线，先前冻结的三真人加一机器人仍保持原样；第四屏可显示 `seatId=0` 等待下一次用户新局。四屏在线不等于四人已完成 Tag、身份准备或实际游戏验收。

## 配对流程

配对在 IdentityRoomPhase::AvatarSetup 期间进行。未绑定真实玩家的 IdentitySeatStage=7（TagPairing），不得提交头像/姓名；绑定成功后立即允许独立编辑，不阻塞下一人配对。等待全部身份 ready 且配对完成后，使用原有五秒倒计时进入游戏。

服务端一次指定一名未绑定玩家，随机选可信可用 reader；先优先实际空库存，再尽量避开上一块 reader。所有设备均占用时进入 clear_required。新 session 后必须先见该 session 的新鲜完整空扫描，再见另一次新单 UID 扫描。预存在位、重复缓存、旧 room/session、不可信扫描不能绑定。

绑定落盘后展示约1秒 success_remove，然后为下一人选择 reader；上一 reader 继续独立提示取回，直到实际匹配的新空扫描。绑定者此时可继续头像/姓名。单 reader 复用时必须清空。已绑定 UID 不转移，multiple_tags/already_bound 必须重新清空。读卡器离线/重启可自动新 session 恢复，仍需新空扫描；cancelled/timed_out 只有显式 Retry 可恢复。

每次 session 等待60秒。Retry/Cancel 仅当前 active player 可操作，必须匹配 roomId/sessionId/revision。请求 ID 与完整 payload 幂等；同 ID 不同内容冲突。新的房间清空请求缓存，服务器重启生成新 session fence。Query 可带 sessionId=0。

## UDP 新消息（schema1，小端）

MessageType TagPairRequest=0x2B、TagPairSnapshot=0x2C；Protocol 版本及原身份消息长度不变。

TagPairRequest 固定20字节：

| 偏移 | 内容 |
|---:|---|
| 0 | schema=1 u8 |
| 1 | operation u8：Query1、Retry2、Cancel3 |
| 2 | playerId u8 |
| 3 | reserved=0 |
| 4 | requestId u32 |
| 8 | roomId u32 |
| 12 | sessionId u32 |
| 16 | expectedRevision u32 |

TagPairSnapshot 固定88字节：

| 偏移 | 内容 |
|---:|---|
| 0..3 | schema=1、state、result、selfPlayerId，各u8 |
| 4/8/12/16 | requestId、roomId、revision、sessionId，各u32 |
| 20/24 | expiresInMs、tagUid，各u32 |
| 28..31 | activePlayerId、requiredHumanMask、boundMask、flags，各u8 |
| 32..63 | moduleId 固定32字节，零填充 |
| 64..79 | playerName 固定16字节，零填充 |
| 80 | readerTagRevision u32 |
| 84 | readerMapIndex u8，255表示未知 |
| 85..87 | reserved=0 |

State：Disabled0、WaitingReader1、ClearRequired2、AwaitingTag3、SuccessRemove4、AlreadyBound5、MultipleTags6、ReaderOffline7、TimedOut8、Cancelled9、Complete10。

Result：Ok0、InvalidRequest1、Unauthorized2、RoomStale3、SessionStale4、RevisionStale5、NotAllowed6、RequestIdConflict7、PersistFailed8。

Flags：Enabled1、SelfBound2、CanEditIdentity4、Resync8、Replay16。SuccessRemove 绑定落盘即带 SelfBound+CanEditIdentity。位图 bit0 对应玩家1。

新认证 UDP 会话的 full resync 先发 TagPairSnapshot 再发 IdentitySnapshot，心跳补发配对快照。配对 revision 独立于游戏 stateVersion。服务器同 room 重启可重建 revision；客户端仅在新认证 UDP session 的本地 epoch-reset 边界允许首次可信 resync 重建，不能因为普通包自带 Resync 就接受旧 revision。

## 模块扫描与回包

新 heartbeat 同时上报 `tagScanSeq:u32`、`tagAgeMs:u32`、`tagFresh:bool`、`pairingRoomId:u32`、`pairingSessionId:u32`。五项全部出现或全部省略；旧省略客户端不选作新配对 reader。

`tags` 沿用八位十六进制字符串数组，例如 `["0000006F"]`；数字型 UID、同一数组内重复 UID、重复键、嵌套字段冒充顶层字段及非法数字后缀均拒绝。

序号只在实际完整可信稳定扫描后增加，age 从该次扫描完成起算，fresh 严格 age<5000ms。缺失消抖、冲突未完成或不稳定扫描可保留显示库存，但必须 fresh=false。重复 scanSeq 不续期。配对 echo 是扫描开始时已接受的 room/session，不能把新 session 盖到旧库存上。tagRevision 仍仅表示内容变化。

模块回包新增 `pairing:{active,roomId,sessionId,revision,state,playerId,playerName,leaseMs}`。state 使用上述英文 snake_case。每块模块仅消费自己的回包：上一玩家 success_remove 可与另一 reader 正在 awaiting_tag 并存。提示优先于 movementCue；网络故障或本地 lease 到期撤提示。

配对用 RFID 不得触发棋盘自动到达。新扫描携非零 pairing echo、fresh=false 或未取回限制未清除时不用于到达。自动局的手动 `/api/player-tag-binding` set/clear 返回 NotAllowed，避免绕过配对门槛；手动位置确认保留。

## Web 与持久化

`GET /api/tag-pairing`、sync.tagPairing：`{enabled,roomId,revision,sessionId,state,result,requestId,replay,activePlayerId,playerName,moduleId,readerMapIndex,readerTagRevision,expiresInMs,requiredHumanMask,boundMask,identityEditableMask,tagUid,canRetry,canCancel}`。身份可编辑位图等于已绑定真实玩家位图。

`POST /api/tag-pairing?operation=retry|cancel&playerId=&requestId=&roomId=&sessionId=&expectedRevision=` 使用相同权威逻辑。成功200、非法400、越权403、过期/冲突409。管理网页代表当前玩家发出明确操作。

sync.frozenDevices 为固定席位数组，元素与 devices 同字段；同步条件 query `pairing` 对应配对 revision。配对语义变化也更新 identityRevision。sync 另增加 `winnerPlayerId` 与 `bankInventory:{buildings,landmarks}`（银行剩余件数）。

元数据保留原61字节前缀，追加 ONB1（冻结模式及四个full identity）与 PND1（未取回掩码与reader来源），总295字节。原12字节及旧扩展文件仍可读取。绑定 UID、来源 reader、未取回限制同次落盘；失败均回滚。服务器重启保留已确认绑定与身份，重新生成扫描 session；未取回的到达限制独立于 reader 在线/显示 cue，只有匹配的新空扫描成功落盘才解除。

同一 reader 被下一位玩家复用时，当前配对提示优先显示，上一位的取回记录仍保留。通过当前 room/session/boot/扫描序号检查的新空场可解除该 reader 的旧限制；写盘失败保持 ClearRequired，并要求另一轮新空扫描。取消或改用其它 reader 不会丢失旧取回提示；未取回掩码未变化时不写元数据。

## 资产与机器人

SellBuilding action10 的 argument=0 表示原单级出售，argument=1 表示明确整组清算，其它值拒绝。整组清算需存活的本人拥有整组地产、所选Property有建筑，且处于可管理阶段或本人债务阶段；收益为 sum(level*(buildingCost/2))，地标level5计五份。确认界面必须说明整个颜色组的建筑均被出售。

32图总库存为24普通建筑、10地标。地标占一件地标、不占普通建筑；升级归还4普通建筑，单级地标降级需要银行至少4普通建筑。库存、现金、均衡建设与事件由Core权威执行；失败不扣钱/改级。破产回收楼件并正确推进下一存活玩家或宣告winner。

机器人在可操作回合保留 max(startAward,2*holdReleaseFee) 现金后优先赎回、再合法建设，每个资产动作占一个 maxActions；无资格时继续掷骰/结束回合。没有增加强制终局规则或自动谈判策略。roundNumber 仍为既有uint8，超过255轮会回绕，本轮未改变存档布局。
