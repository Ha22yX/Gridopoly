# 格子模块临时分配调试契约

更新日期：2026-08-28

## 1. 用途与边界

该接口供树莓派测试网页和物理格子模块临时指定“某个格子模块现在显示哪一格”。格子模块通过
Heartbeat 注册、续租并取得冻结 DTO；网页通过棋盘格右键菜单对在线模块做手动覆盖。格子的
所有者始终是当前权威 `GameState` 的只读投影，不能由调试网页指定。该接口是服务器进程内的
调试绑定，不是游戏规则、资产过户或正式格子总线：

- 不修改 `GameState`、资产 owner、玩家现金、room、`stateVersion`、`controlVersion` 或
  `identityRevision`。
- 不写 `state.bin`、`identity.bin`、`authority.meta` 或 `device-seats.bin`。
- 服务重启后注册表和临时分配均为空；不写任何存档。
- Heartbeat 默认每 2 秒一次，在线租约为 15 秒；过期模块释放其格子。
- 首次注册、清除后的下一次 Heartbeat，都会在锁内原子认领当前地图 `mapIndex` 最小的空闲格。
- 建立新 room 或切换地图时，未过期在线模块按稳定注册顺序，从最低 `mapIndex` 起原子重排。
- `moduleId`、`deviceId` 和被占用格子分别唯一；一个模块同时最多占一个格子。
- 数组排序和注册顺序只用于确定性调试，绝不表示物理安装顺序；原始调试协议没有 `ORDER` 枚举；下文“ORDER 物理邻接扩展”定义新增能力。

HTTP 管理接口没有登录，只允许在可信局域网中使用。

## 2. ID 与所有者投影

`moduleId`、`deviceId` 是 1–32 字符的调试标识，允许字符为
`[A-Za-z0-9_.:-]`。两者必须同时提供并形成唯一绑定；格子固件通常以稳定模块 ID 作为
`moduleId`、以 MAC 地址作为 `deviceId`。

assignment 请求没有 `ownerPlayerId` 或任何其他 owner 输入。服务端根据 assignment 绑定的
`tileId`，在当前权威 `GameState` 中派生三个只读字段：

- 非资产格，或资产当前未拥有时：`owner_player=0`、`owner_display_name=""`、
  `owner_color=0`。
- 资产由有效席位拥有时：`owner_player` 等于 `AssetState::ownerId`，
  `owner_display_name` 使用对应权威玩家名（名字为空时才回退稳定席位名 `P1..P6`），
  `owner_color` 使用下表正式席位色。
- 自动/手动只描述模块与格子的绑定来源，不参与 owner 派生；浏览器不能提交或覆盖 owner、
  名字或颜色。

正式席位色由服务器投影：

| 玩家 | RGB888 | RGB565 |
|---|---:|---:|
| P1 | `0x58A7EB` | `0x5D3D` |
| P2 | `0xEF7168` | `0xEB8D` |
| P3 | `0x52DCB7` | `0x56F6` |
| P4 | `0xF2C453` | `0xF62A` |
| P5 | `0xC28AE8` | `0xC45D` |
| P6 | `0xEA8A55` | `0xEC4A` |

## 3. API

### 3.1 Heartbeat、注册和自动认领

```http
POST /api/tile-modules/heartbeat?moduleId=<id>&deviceId=<id>
Content-Type: application/json

{"tagReaderState":"stable","tagRevision":17,"tags":["8EFA259D"],"overflow":false}
```

- 格子固件每 2 秒发送一次；服务端返回的 `leaseMs` 固定为 15000。
- 首次成功注册会原子认领当前地图最低空闲 `mapIndex`；没有空闲格时返回
  `assigned=false`、`source="none"`，模块仍保持在线。
- 后续 Heartbeat 会续租、刷新权威 owner 投影并返回当前冻结 DTO；绑定和 owner 投影均未变化
  的单纯续租不会推进 `serverRevision`。
- 模块当前未分配时，Heartbeat 会再次尝试最低空闲格，因此 DELETE 清除后无需额外注册。
- 相同 `moduleId` 不能换绑另一个 `deviceId`，相同 `deviceId` 也不能换绑另一个
  `moduleId`；冲突返回 409。
- JSON body 可省略以兼容旧固件。V0.27 起每次 Heartbeat 发送完整 RFID 状态：
  `tagReaderState` 只允许 `scanning|stable|fault`；`tagRevision` 是模块本地 `uint32` 诊断版本；
  `tags` 是 0..6 个 8 位十六进制 HITAG S UID，服务端统一转大写并去重；`overflow` 表示稳定
  集合超过本帧容量。模块重启后 `tagRevision` 可以从低值重新开始，服务器始终把当前 body 当作
  完整替换，不用它做跨启动单调性判断。

有分配时的成功响应：

```json
{
  "ok": true,
  "assigned": true,
  "source": "auto",
  "leaseMs": 15000,
  "serverRevision": 3,
  "moduleId": "tile-01",
  "deviceId": "dc:b4:d9:02:d1:dc",
  "movementCue": {"mode":"none","playerId":0,"revision":77},
  "assignment": {
    "moduleId": "tile-01",
    "deviceId": "dc:b4:d9:02:d1:dc",
    "tile_id": "CORNER-START",
    "mapIndex": 0,
    "displayName": "Start",
    "kind": "START",
    "accent": 16777045,
    "artworkKey": "corner-start",
    "purchase_price": 0,
    "owner_player": 0,
    "owner_display_name": "",
    "owner_color": 0,
    "source": "auto",
    "revision": 3,
    "updatedAtMs": 1787540000001
  }
}
```

`source` 只允许 `auto`、`manual`、`none`，只表示绑定来源。无论自动还是手动绑定，owner 字段
都按当前权威 `GameState` 派生；自动认领到已购买资产时也必须显示其真实 owner。

`movementCue` 每个 Heartbeat 都存在：`mode` 只允许 `none|departure|destination`，
`playerId` 是当前移动玩家，`revision` 是产生该提示的权威 `stateVersion`。`departure` 要求橙色
呼吸，`destination` 要求绿色双闪；同一模块意外同时命中起点和终点时目的地优先。非
`AwaitMoveConfirm`、没有有效 pending move、模块没有分配，或玩家屏尚未完成骰子展示时返回
`none/0`。Roll 只创建待移动目标，不会立即点灯。

### 3.2 列出目录、在线模块和分配

```http
GET /api/tile-debug/assignments
```

成功返回 `200 application/json`、`Cache-Control: no-store`：

```json
{
  "ok": true,
  "roomId": 993580094,
  "boardId": "grid-city-32-v1",
  "boardSize": 32,
  "revision": 3,
  "serverRevision": 3,
  "updatedAtMs": 1787540000001,
  "modules": [
    {
      "moduleId":"tile-01",
      "deviceId":"dc:b4:d9:02:d1:dc",
      "online":true,
      "assigned":true,
      "source":"auto",
      "lastSeenMs":1787540000001,
      "leaseMs":15000,
      "leaseRemainingMs":14850,
      "tagReaderState":"stable",
      "tagRevision":17,
      "tagOverflow":false
    }
  ],
  "tiles": [],
  "players": [],
  "assignments": []
}
```

`tiles` 是当前地图的完整可选目录；`players` 是 P1–P6 的只读服务端席位名和正式颜色目录，
不作为右键输入；`modules` 给出注册、在线、租约和来源状态；`assignments` 是当前临时绑定及
权威 owner 投影。读取会先按当前时间回收过期租约，并刷新所有 assignment 的 owner 投影；
只有发生过期回收或 owner 投影实际变化时才推进 revision。网页右键菜单只列
`online=true` 的模块。

### 3.3 右键手动设置或替换

```http
POST /api/tile-debug/assignment?moduleId=<id>&deviceId=<id>&tileId=<tile>
```

- `moduleId` 与 `deviceId` 必须与一个当前在线注册项精确匹配；浏览器不能借此注册新模块。
- `tileId` 必须精确存在于当前 `BoardDefinition::tiles[]`。
- 浏览器不得上传 owner、名称、类型、颜色、素材键或价格；`ownerPlayerId`、`owner_player`、
  `owner_display_name`、`owner_color` 等追加 query 没有输入语义，服务器必须忽略且不得让它们
  进入状态或响应投影。
- 同一在线模块被移到新格时，服务端在同一把锁内释放原格并占用新格。
- 目标格被另一个在线模块占用时返回 409，不能静默抢占。
- 成功后 assignment 的 `source` 为 `manual`；首次设置或实际变化时全局 revision 加一。
- 每次处理 POST 前先刷新权威 owner 投影；完全相同的重复绑定不再额外增加 revision。
- 成功响应是与 GET 相同的完整最新快照。

### 3.4 清除

```http
DELETE /api/tile-debug/assignment?moduleId=<id>
```

存在时删除 assignment、revision 加一并返回完整最新快照；不存在返回 `404`。模块注册和在线
租约仍保留，但暂时为 `assigned=false`、`source="none"`；它的下一次 Heartbeat 会原子认领
最低空闲格。清除不改变任何游戏状态。

### 3.5 全局 Tag 检测与玩家绑定

```http
GET /api/tile-tags
```

返回进程内全局去重目录、当前权威绑定和两个互相独立的 revision：

```json
{
  "ok": true,
  "roomId": 993580094,
  "tagRevision": 12,
  "bindingRevision": 4,
  "updatedAtMs": 1787540000001,
  "tags": [{
    "uid": "8EFA259D",
    "currentlySeen": true,
    "lastSeenMs": 1787540000001,
    "boundPlayerId": 1,
    "sightings": [{
      "moduleId":"tile-01","deviceId":"28:84:85:ba:9f:e8",
      "tileId":"B3","mapIndex":9,"currentlySeen":true,
      "lastSeenMs":1787540000001
    }]
  }],
  "bindings": [{"playerId":1,"displayName":"Kicofy","uid":"8EFA259D"}]
}
```

- `tagRevision` 只描述瞬时 reader/Tag 目录；Tag 离开后保留最近 sighting 最多 60 秒，模块租约
  过期则立即移除该模块的全部 sighting。该目录不持久化。
- `bindingRevision` 与 `bindings[]` 是权威 metadata；服务重启恢复，同一 room 普通重同步不清，
  建立新 room 时清空。
- `currentlySeen=true` 仅来自当前在线模块的 `stable` 完整集合。`scanning`、`fault` 或历史记录
  只能用于诊断，不能触发自动到达。

玩家卡片右键通过以下 revision-gated API 修改绑定：

```http
POST /api/player-tag-binding?playerId=1&uid=8EFA259D&expectedRevision=4
DELETE /api/player-tag-binding?playerId=1&expectedRevision=5
```

POST 只接受当前 `currentlySeen=true` 的 UID。`expectedRevision` 必须为非零且精确等于当前
`bindingRevision`，否则返回 409。一个玩家最多一个 UID，一个 UID 最多一个玩家；把已有 UID
分给另一玩家时，在同一权威临界区内先解除旧玩家再绑定新玩家，不存在双重绑定窗口。完全相同
的重复 POST 幂等，不推进版本。实际绑定/解除会同时推进 `bindingRevision` 与 `stateVersion`，
写入 `authority.meta`，并在 `/api/sync` 的玩家对象中投影 `tagUid`。网页条件同步必须附带
`tagBindings=<bindingRevision>`。

### 3.6 RFID 自动到达

当且仅当以下条件同时成立，目标格模块的一次 Heartbeat 才执行自动到达：

1. 权威阶段是 `AwaitMoveConfirm`，`pendingMove.active=true`；
2. 对应 PlayerConsole 已在骰子动画、结果保持及首个 MoveGuide 帧完成后发送
   `ActionCode::MovementCueReady(17)`，且服务端持久化闸门为 ready；
3. 当前模块 assignment 的 `mapIndex` 精确等于 `pendingMove.target`；
4. reader 为 `stable` 且 `overflow=false`；
5. `tags[]` 含当前移动玩家绑定的 UID；
6. 请求处理时的 `stateVersion` 仍与该 pending move 相同。

服务器不新增 Tag/RFID 专用玩家消息；仅复用普通 ActionRequest 的展示回执 code 17。自动到达仍在
权威锁内复用
`ActionCode::ConfirmPosition(2)` / `GameEngine::confirmPosition`。成功只推进一次 `stateVersion`，
清除 `pendingMove`、把玩家位置设为目标格，并进入既有 Purchase/Debt/Auction/Card/TurnEnd 等
后续阶段；同一 Heartbeat 响应立即变为更新版本的 `movementCue.mode="none"`。重复帧、旧
state、错误玩家、错误 UID、错误格、overflow 或非稳定 reader 都不会重复执行。

`MovementCueReady` 自身不确认位置、不推进版本；重复回执幂等。新 room 或新待移动事务默认重新
关闭闸门。服务重启会恢复同一 pending move 的 ready 状态，避免动画完成后灯光无故熄灭。

圆屏和网页的手动“确认格子到位 / I'M THERE”入口必须保留并继续走同一个 ConfirmPosition
事务。没有部署目标格模块或当前只有一块模块时，不阻止手动确认。

### 3.7 错误

| HTTP | 条件 |
|---:|---|
| 400 | 缺少 module/device/tile ID，或 ID 字符非法 |
| 404 | 当前地图没有该 `tileId`，或要清除的 module/assignment 不存在 |
| 405 | `/api/tile-debug/*` 或 `/api/tile-modules/*` 使用不支持的方法 |
| 409 | 当前没有有效地图、module/device 绑定冲突、手动目标已占用、模块不在线、容量已满、Tag 未在当前稳定检测目录或 binding revision 过期 |

错误体为 `{"ok":false,"error":"..."}`；服务内部错误还会带稳定数值 `code`。

## 4. 服务端派生目录

每个 `tiles[]` 项由当前 `BoardCatalog` 和
`Firmware/PlayerConsole/grid_city_visual_catalog.cpp` 联合派生：

```json
{
  "tileId": "A1",
  "mapIndex": 1,
  "displayName": "Rivet Row",
  "kind": "PROPERTY",
  "accentRgb": 13203538,
  "artworkKey": "a1-rivet-row",
  "purchasePrice": 60
}
```

- `mapIndex` 是地图逻辑索引，不是物理安装顺序。
- `purchasePrice` 读取 `AssetDefinition::economy.price`；非资产格为 0。
- `kind` 只允许：`START`、`PROPERTY`、`TRANSIT`、`UTILITY`、`CHANCE`、
  `COMMUNITY_CHEST`、`FEE`、`HOLD`、`REST`、`GO_TO_HOLD`。
- `displayName`、`accentRgb` 和 `artworkKey` 来自正式视觉目录。

## 5. 预留格子固件 DTO

`assignments[]` 的字段名与格子固件冻结字段保持一致：

```json
{
  "moduleId": "tile-01",
  "deviceId": "dc:b4:d9:02:d1:dc",
  "tile_id": "B3",
  "mapIndex": 9,
  "displayName": "Beacon Boulevard",
  "kind": "PROPERTY",
  "accent": 6538984,
  "artworkKey": "b3-beacon-boulevard",
  "purchase_price": 220,
  "owner_player": 1,
  "owner_display_name": "P1",
  "owner_color": 5810155,
  "source": "manual",
  "revision": 2,
  "updatedAtMs": 1787540000001
}
```

颜色是 `0xRRGGBB` 的 24 位无符号整数。owner 三元组不是 assignment 的可写属性；每次
Heartbeat、GET 或 POST 前都从当前权威 `GameState` 刷新。当前 DTO 不包含建筑、抵押、
活动效果或物理顺序；以后增加正式下发传输层时必须复用这些语义，不能让浏览器成为视觉或
归属数据来源。

## 6. Revision、租约与换图语义

- 顶层 `revision` 与 `serverRevision` 是同一个当前进程内全局注册/分配版本；保留
  `revision` 是为了兼容已经存在的调试页面。
- 每个 assignment 的 `revision` 是该项最后一次实际变化时的全局版本。
- `updatedAtMs` 是服务器 Unix epoch 毫秒。
- 服务重启后 revision 从 0 开始。
- 首次注册、自动/手动分配、清除、租约过期和 room/地图重排会推进一次全局版本。
- 每次 Heartbeat、GET 或 POST 前刷新所有 assignment 的权威 owner 投影。一次刷新中只要
  任一受影响 assignment 的 owner id、显示名或正式颜色实际变化，全局 revision 只推进一次；
  所有变化项取得相同的新 `revision` 和 `updatedAtMs`，绑定格子与 `source` 保持不变。
- 正常 2 秒 Heartbeat 续租、权威 owner 投影未变化的重复手动设置和纯读取不推进版本。
- 租约 15 秒；过期模块释放格子，之后再次 Heartbeat 视为新注册。
- 新 room/换地图不会保留旧 tileId。未过期在线模块按稳定注册顺序，在单一临界区中从当前
  地图最低 `mapIndex` 开始重排；没有空闲格的模块保持在线未分配。
- revision 不进入 `/api/sync`，也不触发圆屏投影或游戏持久化。
- 上一条只指临时 module assignment revision；玩家 `bindingRevision` 是另一条权威版本线，
  会进入 `/api/sync` 并写 `authority.meta`。

## 7. 网页右键交互

- 在棋盘任意格右键（键盘可用 Context Menu 或 `Shift+F10`）打开“分配格子模块”菜单。
- 菜单锁定当前格子，不提供第二个 tile 下拉框；显示格号、名称、类型、素材键和价格。
- 模块下拉只显示 `online=true` 的注册项，并显示 device、当前格、来源和剩余租约。
- 菜单只要求选择一个在线模块；不提供“临时所有者”或任何 owner 控件，也不提交
  `ownerPlayerId`。当前权威 owner 可只读显示。
- 玩家卡片右键提供“分配 Tag / 更换 Tag / 解除 Tag”。弹窗只列全局目录中
  `currentlySeen=true` 的 UID，并显示当前看到它的 module/tile；从其他玩家转移时明确提示原子转移。
- 应用成功即关闭菜单并刷新状态，失败保留菜单和模块选择以便重试。
- “清除”只清除该模块当前 assignment；下一次 Heartbeat 会恢复自动认领。
- 页面外点击、Escape、窗口尺寸或 room/地图变化会关闭菜单。强制投骰的棋盘点选模式优先，
  右键菜单不得取消或打断该流程。

## 8. 最小验收

1. 两个模块并发首次 Heartbeat 只会分别占用 `mapIndex=0`、`mapIndex=1`，不会重复占格。
2. 重复 Heartbeat 续租不增加 `serverRevision`；15 秒过期会释放格子并增加版本。
3. 手动右键把在线模块移到另一空闲格时，原格立即空闲；另一个在线模块占用目标时返回 409。
4. DELETE 后模块仍在线未分配；下一 Heartbeat 自动认领当前最低空闲格。
5. 新 room/换地图后，未过期在线模块按稳定注册顺序原子重排；不沿用旧 tileId。
6. 16/24/32/40 地图目录中的每个格子都有名称、kind、accent 和 artworkKey。
7. 16 格地图的 B3 派生为 `Beacon Boulevard`、`b3-beacon-boulevard`、价格 220。权威状态
   未购买时，自动或手动绑定都返回 owner 0；权威状态改为由 P1 购买后，无需重新 POST，下一次
   Heartbeat/GET 即自动返回 P1 的权威名字和正式颜色，并按第 6 节只推进一次调试 revision。
8. 设置、列出、续租、过期、清除和重排前后 room、游戏 version、资产 owner 和现金完全不变。
9. 浏览器追加伪造的 `ownerPlayerId`、owner、名称、颜色、价格或 artwork query 不会进入响应，
   也不会修改权威状态。
10. 原始legacy模式不定义或推断物理 `ORDER`；新增能力见下文物理邻接扩展。
11. 两个模块同时报告同 UID 时全局目录只出现一个 Tag，并保留两条 sighting；租约过期或
    60 秒历史过期后不再可选。
12. 同 UID 从 P1 原子改绑 P2 时 P1 立即清空；重复/过期 revision 不产生第二次状态变化；
    服务重启保持绑定，新 room 清空绑定。
13. AwaitMoveConfirm 时原格返回 departure、目标格返回 destination；stable 且非 overflow 的
    目标 Tag 只执行一次 ConfirmPosition，重复 Heartbeat 不再推进 version；手动确认始终可用。


## ORDER 物理邻接扩展（2026-09-10）

本节扩展此前调试注册协议，保留前文legacy客户端兼容性；不能把原注册顺序解释成物理线序。
ORDER线信标与HTTP请求字段由[tile-order-protocol.md](tile-order-protocol.md)定义，格子固件在
原Tag心跳body添加order对象，服务器只根据新鲜的物理bootId/上游bootId/序号证据建有向边。
不存在将完整拓扑列表直接写入服务器的测试后门。原POST/DELETE assignment路径沿用。

支持ORDER的模块停止使用注册顺序auto占空位。无手动锚点显示unanchored；已验证链内最近
上游manual为分段锚点，首锚前使用负偏移回推，mapIndex按当前board.tileCount环绕。新manual
重启后段基准；派生source为order，auto和order绝不充当锚点。manual优先于order，order优先
于legacy auto；legacy临时占格被实际ORDER占用后，在下一次legacy心跳选剩余空位。其它manual
占同格时新manual仍409拒绝；两个派生结果互撞、或派生撞manual时所有争用派生停用并显示conflict，
不能跳空位破坏相对间隔。若保留的手动意图暂不能恢复，该锚点所在段暂停，不越过它套用前锚。

ORDER手动意图以moduleId/deviceId/tileId暂存内存，失联后停止生效但保留；同device回来先检查
manual占格冲突再恢复。DELETE可清离线意图，换room/board或服务器重启清除，不新增落盘。
legacy模块保持原15秒租约删除契约。物理证据独立15秒TTL；相同seq重报、valid=false、过期与
离线都不会续旧边。退休boot/每上游序号及本模块txSeq栅栏拒绝迟到旧报告；过期历史不参加活动
nonce唯一性或分叉判定。历史保护各最多64项，到达界限拒绝新身份而非无界增长。

GET /api/tile-debug/assignments新增顶层order：epoch、status、leaseRemainingMs、chains数组
（chainId/moduleIds）。epoch是本服务器运行期拓扑/boot/状态变化的递增整数，非固件时间戳或
永久链编号。modules增加orderCapable、orderStatus（legacy/ready/unanchored/stale/conflict）、
orderChainId、orderIndex（0起，未知-1）、orderEpoch、orderConflict、orderUpstreamModuleId和
orderAnchorTileId（无意图为空）。离线但保留manual意图的模块仍有online=false/assigned=false行，
供用户撤销。assignment新增orderAnchorModuleId、orderOffset有符号整数及orderEpoch；source
仍可none/auto/manual，新增order。ready拓扑不表示已分配；链序仅描述当前验证连通分量的方向。

旧客户端忽略新增字段即可。新固件必须在assigned=false时撤销旧assignment与提示，不能保留
已经过期的ORDER派生格。此时可继续上报Tag inventory，但未分配的模块不触发目标格自动到达。
新增目标由服务器native/HTTP隔离回归检验，实际信标与双板联调须另记录设备版本及现场证据。
