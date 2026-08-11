# Gridopoly 主控与玩家圆屏协议

状态：历史设计基线；其中 WebSocket/JSON 传输已由认证 Wi‑Fi/UDP 二进制协议取代

更新日期：2026-08-05

> 当前正式实现以 [Wi‑Fi/UDP 玩家屏协议](wifi-udp-player-protocol.md) 和
> [Raspberry Pi 5 权威服务端](raspberry-pi-server.md) 为准。本文保留用于追溯早期消息语义，
> 不得据此实现新的传输层、地址、热点名称或会话鉴权。

## 1. 范围

本文定义树莓派主控与 2 至 6 块 ESP32-S3 玩家圆屏之间的正式通信边界。单格模块的
RS485/ORDER 协议继续由现有硬件文档定义，不使用本文的 WebSocket 通道。

## 2. 网络拓扑

- 树莓派创建 WPA2 专用热点，SSID 格式为 `Gridopoly-XXXX`。
- 正式游戏不依赖现场路由器或互联网。
- 主控固定网关地址建议为 `192.168.50.1`。
- 玩家终端通过 `ws://192.168.50.1:8765/ws/player` 建立 WebSocket。
- 调试电脑可加入同一热点，但不能自动获得玩家会话权限。
- USB 串口只用于烧录、日志和诊断，不承载正式多人状态。

热点口令由设备初始化流程写入圆屏安全配置。生产版本不得使用所有设备共享的公开
默认口令。

## 3. 协议版本

版本格式为 `major.minor`：

- `major` 不同表示不兼容，终端停止入局并提示升级。
- `major` 相同、`minor` 不同可以通过能力协商降级。
- 未识别的可选字段应忽略。
- 未识别的必需消息类型必须返回 `UNSUPPORTED_MESSAGE`。

首个协议基线为 `1.0`。

## 4. 消息信封

```json
{
  "protocol": "1.0",
  "type": "PAY_NOW",
  "message_id": "01J...",
  "request_id": "01J...",
  "room_id": "room-20260802-01",
  "device_id": "esp32-a1b2c3d4",
  "player_id": "P1",
  "state_version": 184,
  "server_time_ms": 1785629001000,
  "deadline_ms": 1785629011000,
  "payload": {}
}
```

| 字段 | 方向 | 说明 |
| --- | --- | --- |
| `protocol` | 双向 | 协议版本 |
| `type` | 双向 | 消息类型 |
| `message_id` | 双向 | 当前消息唯一 ID |
| `request_id` | 响应必需 | 对应客户端请求，支持幂等重试 |
| `room_id` | 入局后双向 | 房间标识 |
| `device_id` | 客户端上行 | 设备永久标识，不等同玩家身份 |
| `player_id` | 绑定后双向 | 当前会话玩家槽位 |
| `state_version` | 入局后双向 | 客户端所见或服务端生成的状态版本 |
| `server_time_ms` | 服务端下行 | UTC 或主控单调映射时间戳 |
| `deadline_ms` | 定时消息下行 | 绝对截止时间，没有则省略 |
| `payload` | 双向 | 消息专用数据 |

游戏金额使用整数美元，不传浮点数。角度、进度和比例需要小数时使用整数定点值。

## 5. 连接与认领

### 5.1 握手

客户端连接后发送：

```json
{
  "protocol": "1.0",
  "type": "CLIENT_HELLO",
  "message_id": "hello-1",
  "device_id": "esp32-a1b2c3d4",
  "payload": {
    "firmware": "player-console-demo-0.1.0",
    "display": [480, 480],
    "capabilities": ["rotary", "press", "touch", "demo-lab"],
    "last_session_token": null
  }
}
```

主控返回 `SERVER_HELLO`，包括协议协商结果、房间状态、服务器时间和是否可以恢复
上一次绑定。

### 5.2 交互式座位认领

1. 未绑定圆屏显示“按下加入”。
2. 玩家短按后发送 `JOIN_REQUEST`。
3. 主控大厅显示设备临时颜色、设备短 ID 和请求时间。
4. 主控操作者选择空闲玩家槽位并发送 `SEAT_ASSIGNED`。
5. 圆屏显示角色、颜色和玩家编号，并发送 `SEAT_ACK`。
6. 主控下发会话令牌和完整状态快照。

会话令牌用于断线重连，不允许圆屏自行选择其他玩家 ID。新建房间、主控解除绑定或
令牌失效后重新认领。

## 6. 客户端命令

圆屏在本地将旋钮和触摸转换成语义命令。原始 GPIO、触摸坐标和焦点移动不通过正式
协议上传，诊断模式除外。

| 命令 | 用途 |
| --- | --- |
| `JOIN_REQUEST` | 请求认领座位 |
| `ROLL_REQUEST` | 请求主控生成骰子 |
| `PURCHASE_ACCEPT` | 购买无主资产 |
| `PURCHASE_DECLINE` | 放弃购买并进入拍卖 |
| `CLAIM_RENT` | 地主确认收租 |
| `PAY_NOW` | 立即完成已经成立的债务 |
| `AUCTION_READY` | 已渲染当前 generation 的拍卖介绍页 |
| `AUCTION_BID` | 提交竞价 |
| `AUCTION_PASS` | 退出当前拍卖 |
| `TRADE_CREATE` | 创建交易草案 |
| `TRADE_UPDATE` | 修改交易草案 |
| `TRADE_REJECT` | 拒绝草案 |
| `TRADE_CONFIRM` | 确认锁定版本 |
| `MORTGAGE_ASSET` | 抵押资产 |
| `UNMORTGAGE_ASSET` | 赎回资产 |
| `BUILD_LEVEL` | 建设一级 |
| `SELL_BUILDING` | 出售一级建筑或地标 |
| `HOLD_DECISION` | 选择限制区离开方式 |
| `DECLARE_BANKRUPTCY` | 在筹款流程中声明破产 |
| `RESYNC_REQUEST` | 请求完整快照 |
| `PING` | 心跳与时间同步 |

所有改变权威状态的命令必须包含唯一 `request_id` 和客户端当前 `state_version`。

`AUCTION_READY` 还必须携带拍品 ID 和服务端生成的 `auction_generation`。同一 generation 的重复 Ready 幂等；旧代次不得影响新拍卖。服务端在全部必需真人终端 Ready 前保持 `AUCTION_OPENING`，不接受竞价/退出，也不推进机器人。重连全量快照若仍显示自身 Ready 位缺失，终端可重发同一代次的 Ready。

## 7. 服务端消息

| 消息 | 用途 |
| --- | --- |
| `SERVER_HELLO` | 协议与服务器能力 |
| `SEAT_ASSIGNED` | 玩家槽位和会话令牌 |
| `STATE_SNAPSHOT` | 完整权威状态 |
| `STATE_PATCH` | 版本连续的增量状态 |
| `TURN_STATUS` | 当前玩家、顺序和回合阶段 |
| `ROLL_RESULT` | 已提交的骰子结果 |
| `MOVE_RESULT` | 路径、经过起点和最终位置 |
| `PURCHASE_OFFER` | 自愿购买窗口 |
| `RENT_OPPORTUNITY` | 地主 20 秒收租机会 |
| `PAYMENT_REQUIRED` | 付款方 10 秒授权窗口 |
| `DEBT_RESOLUTION_REQUIRED` | 现金不足与筹款状态 |
| `TRADE_OFFER` | 新交易或版本更新 |
| `AUCTION_STATE` | 当前价格、领先者、可出价状态 |
| `CARD_REVEALED` | 卡片 ID、参数和结算结果 |
| `ASSET_UPDATED` | 购买、抵押、建设或转移结果 |
| `PLAYER_UPDATED` | 公开玩家状态变化 |
| `COMMAND_ACCEPTED` | 命令已提交 |
| `COMMAND_REJECTED` | 标准错误码与最新版本 |
| `ROOM_RECOVERING` | 主控正在恢复 |
| `GAME_OVER` | 胜者和最终排名 |
| `PONG` | 时间同步响应 |

## 8. 快照与增量

### 8.1 完整快照

`STATE_SNAPSHOT` 至少包括：

- 房间、地图、节奏和协议版本
- 当前回合、阶段和顺序
- 本玩家完整私有状态
- 其他玩家公开状态
- 资产所有权、抵押和建筑状态
- 与本玩家相关的交易和债务
- 当前有效截止时间
- 可执行动作列表

### 8.2 增量

- `STATE_PATCH.from_version` 必须等于客户端当前版本。
- 应用成功后版本变为 `to_version`。
- 版本不连续时客户端丢弃该增量并发送 `RESYNC_REQUEST`。
- 客户端不自行合并相互冲突的增量。

## 9. 倒计时同步

- 主控发送绝对 `deadline_ms`，不发送每秒递减消息。
- 圆屏通过最近一次 `PONG` 估算服务器时间并本地绘制进度。
- 每 5 秒心跳一次；活动倒计时时可缩短到 2 秒。
- 时间偏差超过 150ms 时平滑校正显示，不倒退已经显示的进度。
- 主控按自己的时间判断是否超时，圆屏显示不是裁决依据。

## 10. 幂等与状态冲突

- 主控保存近期已处理 `request_id` 及其结果。
- 相同请求重发返回同一结果，不重新掷骰或重复扣款。
- 客户端状态版本过期时返回 `STALE_STATE` 和最新版本。
- 交易草案另有 `trade_version`，修改后双方确认清零。
- 自动付款和手动付款共享同一债务事务 ID。

## 11. 错误码

| 错误码 | 含义 | 客户端处理 |
| --- | --- | --- |
| `UNAUTHORIZED` | 会话无效 | 返回认领或重连页 |
| `WRONG_PLAYER` | 玩家身份不匹配 | 停止操作并请求快照 |
| `STALE_STATE` | 状态版本过期 | 应用新快照 |
| `DEADLINE_EXPIRED` | 操作窗口已结束 | 关闭对应弹窗 |
| `ACTION_NOT_ALLOWED` | 当前阶段不允许 | 显示原因并刷新可用动作 |
| `INSUFFICIENT_CASH` | 现金不足 | 进入筹款状态 |
| `ASSET_CHANGED` | 资产已变化 | 刷新资产或交易草案 |
| `DUPLICATE_REQUEST` | 重复请求 | 使用原始结果 |
| `UNSUPPORTED_MESSAGE` | 消息类型不支持 | 记录并安全忽略 |
| `PROTOCOL_MISMATCH` | 主版本不兼容 | 显示升级页 |
| `SERVER_RECOVERING` | 主控暂不可裁决 | 冻结危险操作 |

## 12. 重连

1. WebSocket 断开后，圆屏立即遮暗页面并禁止危险操作。
2. 使用退避间隔重连，建议 0.5、1、2、3、5 秒，之后保持 5 秒。
3. 发送上一次会话令牌和最后状态版本。
4. 主控验证令牌并始终发送完整快照。
5. 圆屏清除本地确认弹窗、未提交草案编辑和过期焦点。
6. 快照应用完成后恢复正常页面。

客户端不得离线排队购买、付款、交易、抵押或掷骰命令。

## 13. 信息权限

### 本玩家可见

- 完整现金、资产和卡片
- 可执行操作和债务
- 参与中的交易草案

### 其他玩家公开

- 玩家名、颜色和头像
- 余额、位置和是否在限制区
- 公开资产、抵押和建筑状态
- 是否破产

### 私有

- 其他玩家未公开卡片细节
- 与其他两名玩家之间的交易草案
- 设备会话令牌和热点凭据

## 14. Demo 传输适配器

圆屏代码应依赖统一的状态源接口：

- `WebSocketTransport` 接收正式主控消息。
- `DemoTransport` 按脚本产生同结构消息。
- UI 不得根据传输类型改变页面逻辑。
- Demo 消息使用独立房间 ID 和显眼的 `demo=true` 标记。

## 15. 日志

USB 日志至少记录：

- 连接、断开、重连和认领
- 消息类型、ID、状态版本和处理耗时
- 命令拒绝错误码
- 时间同步偏差
- 页面 ID、焦点 ID和输入分类
- 堆、PSRAM 和帧率异常

日志不得输出热点口令、完整会话令牌或其他玩家私有数据。
