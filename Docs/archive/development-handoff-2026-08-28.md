> 历史冻结记录，已由[当前开发交接](../CURRENT-DEVELOPMENT-HANDOFF.md)替代；下文状态只适用于原记录时间。

# Gridopoly 当前开发接手基线

> **2026-09-07 恢复开发：用户已要求三个分会话继续未完成任务。** 当前分工见[恢复开发与协调记录](../firmware/development-coordination-2026-09-07.md)。下文暂停状态及测试、部署结果为 2026-08-28 历史记录。

> **2026-09-07 设备端口更新：玩家屏为 COM7，格子模块为 COM6。** 见[当前设备连接记录](../firmware/current-device-connections.md)。下文 COM4/COM12 为历史记录；本次仅更新端口，不改变原开发状态。

> 更新时间：2026-08-28（Asia/Shanghai）
> 状态：**已按用户要求暂停开发**。不要把本文中的“已写入源码”误认为“已编译、已烧录或已实机通过”。
> 优先范围：玩家旋转屏幕模块（COM4）、格子模块（COM12）以及两者之间的树莓派权威服务时序。

## 1. 本次暂停的目标

待完成的问题是：玩家掷骰时，格子模块不能在圆屏骰子动画尚未结束时提前改变 LED。

冻结后的正确时序为：

1. 服务端确定骰子结果与目标格，但保持所有格子模块的 `movementCue=none`。
2. 玩家圆屏完整播放骰子动画和结果保持阶段。
3. 圆屏进入 `MoveGuide`，且第一帧真正提交到屏幕后，发送 `ActionCode::MovementCueReady (17)`。
4. 服务端收到并接受 Action 17 后，才向原格下发 `departure`，向目标格下发 `destination`。
5. RFID 自动到达也必须等同一闸门打开；手动 `I'M THERE` / `ConfirmPosition (2)` 始终保留且不受闸门阻塞。

这不是固定延时方案。闸门绑定真实的 UI 首帧呈现，普通重同步不能重播 LED 动画。

## 2. 树莓派服务端：已完成并已部署

### 2.1 已实现

- 共享协议已有 `ActionCode::MovementCueReady = 17`。
- `AuthorityService` 已实现按 `room + player + origin + target` 管理的移动提示闸门。
- 真人 `RealConsole` 的新移动默认 `ready=false`；Web/Bot 不依赖圆屏动画，默认可就绪。
- Action 17 校验认证席位、`AwaitMoveConfirm`、目标格、精确且非零的 `expectedStateVersion`。
- Action 17 成功不增加游戏 `stateVersion`，重复请求通过原 UDP 请求缓存幂等回放。
- 闸门状态追加持久化；同一待确认移动在服务重启后可恢复。
- 新房间、新目标、离开等待到达阶段或确认成功会清理旧闸门。
- RFID 自动到达在闸门未就绪时拒绝执行；手动 `ConfirmPosition` 不受影响。
- `/api/sync` 增加只读诊断字段 `movementCueGate`。

主要文件：

- `Firmware/libraries/GridopolyProtocol/src/gridopoly/protocol/Protocol.h`
- `Server/RaspberryPi/src/AuthorityService.h`
- `Server/RaspberryPi/src/AuthorityService.cpp`
- `Server/RaspberryPi/src/TileDebugAssignments.h`
- `Server/RaspberryPi/src/TileDebugAssignments.cpp`
- `Server/RaspberryPi/src/HttpServer.cpp`
- `tests/host/authority_persistence_tests.cpp`
- `tests/host/tile_debug_assignment_tests.cpp`
- `tests/host/udp_server_integration_tests.cpp`
- `tests/host/http_asset_integration_tests.cpp`

### 2.2 已通过验证

- Windows 原生构建与 CTest：12/12 PASS。
- Raspberry Pi ARM64 严格原生测试：PASS。
- UDP 集成重复 50/50 PASS。
- HTTP 集成重复 50/50 PASS。
- 候选 ARM64 二进制：
  - 路径：`/home/kicofy/gridopoly-movement-cue.4RMjtB/build-pi-native/gridopoly_server`
  - SHA-256：`346dfc92d630f07c434aa2ed56fc5f652ca147f9e5725f02396718374787da36`
  - 大小：475800 bytes

### 2.3 当前生产部署状态

- 候选已部署到 `/usr/local/bin/gridopoly_server`。
- `gridopoly.service` 已恢复为 `active`。
- 部署后健康样本：`roomId=993580098`、`version=70`、`peers=1`、UDP `authFailures/replayDrops/txErrors=0`。
- `/api/sync` 当时显示：
  - `movementCueGate.active=false`
  - `movementCueGate.ready=false`
  - `player=0`
  - `origin=255`
  - `target=255`
- 回滚备份：
  - `/home/kicofy/gridopoly_server.pre-movement-cue-20260828`
  - `/home/kicofy/gridopoly-state.pre-movement-cue-20260828.tar.gz`

注意：第一次直接覆盖二进制时目标文件意外成为 0 bytes，systemd 报 `203/EXEC`。随后已用 `.new` 文件、校验 SHA-256、再原子 `mv` 的方式修复；当前服务健康。后续部署必须沿用“临时文件 -> 校验 -> 原子替换”，不要直接覆盖正在使用的目标文件。

## 3. 玩家旋转屏幕模块（COM4）：部分源码已落盘，未完成

### 3.1 已实际落盘

- `Firmware/PlayerConsole/transport_types.h`
  - 新增 `TransportCommandKind::MovementCueReadyRequest`。
- `Firmware/PlayerConsole/app_types.h`
  - 新增：
    - `movementCueRoomId`
    - `movementCueStateVersion`
    - `movementCueTarget`
    - `movementCueFramePresented`
    - `movementCueAcknowledged`
    - `movementCueReadyToSend`
  - `pendingRequestIds` 从 24 扩为 25。
- `Firmware/PlayerConsole/espnow_player_transport.h`
  - 新增 `pendingMovementCue_`。
  - 新增 `begin/resend/tick/rejectMovementCueReady` 方法声明。
- 共享 `Protocol.h` 已有 Action 17 枚举。

### 3.2 尚未实现

- `app_state.cpp` 尚未建立 `room + stateVersion + target` 生命周期键。
- 尚未把“骰子演出结束 + MoveGuide 首帧呈现”接入 gate。
- `appNotifyFramePresented` 尚未排队 Action 17。
- Wi-Fi/UDP 尚未实现独立 movement-cue pending、同 requestId 重试、ActionResult 结束、重连恢复和清理。
- ESP-NOW 仅有头文件声明，尚无实现。
- Demo transport 尚未增加该命令分支。
- 尚未添加以下关键测试：
  - 首帧前不发送；
  - 首帧后只启动一次；
  - ActionResult 丢失后幂等重试；
  - 同 key resync 不重启；
  - 新 room/target/phase 清理；
  - 手动 `ConfirmPosition` 不被独立 pending 阻塞。

### 3.3 编译与设备状态

- 这批部分改动**没有执行编译**，当前不能宣称可编译。
- Action 17 正式固件**没有烧入 COM4**。
- COM4 保持暂停前的正式生产固件；它不会发送 Action 17。
- 因树莓派已部署新闸门，当前 COM4 发起真人移动时，格子 LED 会保持 `none`，直到未来刷入完成版 Action 17。手动到达仍可继续游戏。

### 3.4 同一工作树内其它未验证改动

以下 Avatar/手写性能改动已落盘，但尚未完整构建、SelfTest 或实机验收。接手者不得把它们当成稳定基线，也不要盲目覆盖：

- `PlayerConsole.ino`：RGB bounce buffer 20 行增至 40 行；主循环每轮最多应用一个旋钮 step。
- `hardware_input.cpp` 与新文件 `rotary_input_filter.h`：35ms 反向毛刺过滤、同向合并后单步有序输出。
- `app_state.cpp`：Avatar `delta=0` 直接忽略。
- `remote_avatar_cache.cpp/.h`、`remote_tile_cache_policy.h`：双预览缓冲、保留旧完整帧、约 45ms recipe settle/deferred compose、Setup 缓存预算约 2304 KiB、释放路径变更。
- `ui_renderer.cpp`：AvatarSetup 保留式组件树，避免每次切换整页重建；加载时保留上一张完整预览。
- `ui_handwriting.cpp`：16ms 批量笔迹 flush、低优先级 FreeRTOS 识别 worker、持久 workspace/mutex/capture-generation。
- `logic_tests.cpp`：已加入部分旋钮滤波/单步队列测试源码，但未运行。

## 4. 格子模块（COM12）：协议/LED 已完成，当前设备未上线

### 4.1 固件能力

- V0.27 已支持现有 heartbeat JSON 与顶层 `movementCue`：
  - `none`
  - `departure`：橙色整圈呼吸
  - `destination`：绿色整圈双闪
- 只有 cue/revision 变化才重启动画。
- 不需要因为 Action 17 修改或重刷 TileModule；Action 17 只发生在圆屏和权威服务之间。

### 4.2 当前硬件/在线状态

- 用户声称重新供电后，只读检查仍只枚举到 COM4，COM12 完全未出现。
- 生产 `GET /api/tile-debug/assignments` 在服务重启后返回 `modules=[]`、`assignments=[]`。
- 因此当前不是 HTTP 持久连接卡住，而是格子模块尚未完成 USB 枚举/上电/联网。
- 最短恢复动作：
  1. 检查屏幕和 LED 是否亮；
  2. 单击实体 RST，等待约 15 秒；
  3. 若 COM12 仍不出现，重新插紧 USB 数据线/供电线。

### 4.3 RFID 已知硬件阻塞

- 最近实机状态为 `tagReaderState=fault`。
- 证据：约 4.945V / 125.8mA，HTRC110 启动自检后首次扫描即 FAULT，未进入 UID 波形采集。
- 该问题位于 HTRC110 数字链路、配置开场或线圈/J3 硬件，不是服务端 Tag 映射或 movement cue 问题。
- 在修复硬件前，不能完成 RFID 自动到达实机验收；手动 `I'M THERE` 仍是有效兜底。

## 5. 下一位 agent 的最短续开发顺序

1. **先只读确认工作树**，不要回退或覆盖上述未验证 PlayerConsole 改动。
2. 完成 PlayerConsole Action 17：
   - AppState 生命周期键；
   - `MoveGuide` 首帧 gate；
   - Wi-Fi/UDP 独立 pending；
   - ESP-NOW 编译兼容；
   - Demo 分支与关键回归。
3. 先跑最小逻辑测试和正式 Wi-Fi/UDP 编译；若失败，区分 Action 17 问题与 Avatar/手写未验证改动。
4. 通过后再跑完整 SelfTest；不要跳过正式构建。
5. 服务器已经支持 Action 17，不需要再次改协议或重复部署，除非源码有新的必要修复。
6. 烧 COM4 前确认树莓派 `/health` 正常、room 仍为目标房间。
7. COM4 刷入完成版后，只读确认同 room/seat、Heartbeat/Ack、resync 和错误计数。
8. 恢复 COM12 上电/枚举并确认 `/api/tile-debug/assignments` 再次出现在线模块。
9. 实机时序验收：
   - Roll 后、圆屏动画期间：`movementCueGate.ready=false`，模块 cue 必须为 `none`；
   - MoveGuide 首帧后：Action 17 成功，`ready=true`；
   - 下一次约 2 秒 heartbeat 内，原格/目标格 LED 才进入 departure/destination；
   - 重复 Action 17 不重启 LED；
   - 手动 `I'M THERE` 全程可用。
10. RFID 实机自动到达验收单独等待 HTRC110/线圈硬件恢复，不要把它与 LED 闸门软件验收混为一项。

## 6. 禁止误判

- 不要宣称“完整修复已上线”：当前只有服务端上线，COM4 尚未实现/烧录 Action 17。
- 不要用固定延时替代 MoveGuide 首帧回执。
- 不要把 Action 17 放进普通动作 pending；它不推进 `stateVersion`，会导致通道等待并阻塞手动确认。
- 不要修改 TileModule wire 或新增 PlayerConsole Tag/RFID 消息。
- 不要因为 RFID fault 回滚服务器 movement cue；两者是独立问题。
- 不要覆盖用户和其它任务在同一脏工作树中的改动。
