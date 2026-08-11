# Gridopoly ESP32-S3 测试游戏服务器设计规格

状态：总体语义设计已完成独立复核，待用户批准；字节合同按批准后 P0-Contract 闭包

日期：2026-08-02

目标硬件：独立 ESP32-S3 开发板，当前串口 COM5

## 1. 目的

本规格定义一套可在 ESP32-S3 上运行的临时权威游戏服务器，用于在真实树莓派主控
完成前验证 Gridopoly 的完整规则、玩家圆屏、ESP-NOW 通信、模拟棋盘和多玩家流程。

“临时”只描述承载平台。游戏规则核心、地图数据、状态版本、协议语义和测试向量均按
正式可复用组件设计，未来可以直接编译进树莓派主控，或由树莓派通过 ESP32-S3
无线网关驱动多个玩家圆屏；后一模式的 Pi↔网关协议需另立规格，不属于本轮实现。

测试配置允许一名真实圆屏玩家和一至五名网页或机器人玩家，也支持后续一至六台真实
圆屏混合接入。

## 2. 已冻结的产品决定

- 固件使用 Arduino-ESP32 作为平台外壳。
- 权威规则核心和语义协议使用平台无关 C++17 子集。
- 网页、机器人、真实圆屏和未来棋盘模块只能提交命令，不能直接修改权威状态。
- 支持完整的 16、24、28、32、40 格版本化地图包。
- 40 格正常模式以经典美版地产经营规则、格型顺序和经济数字为校准母版。
- Gridopoly 的名称、角色、卡牌文案、插画、颜色系统和页面布局保持原创。
- 不使用自由中庭奖池、跳过拍卖、玩家贷款或免租承诺等房规。
- ESP32-S3 以 STA 连接家庭 2.4GHz Wi-Fi，不创建 SoftAP。
- 玩家圆屏不需要家庭 Wi-Fi 密码，通过 ESP-NOW 与测试服务器通信。
- 家庭 Wi-Fi 与 ESP-NOW 共用同一射频和同一实际信道。
- Wi-Fi 凭据和 commissioning secret 仅存在于 Git 忽略文件或 NVS。
- 当前安全配置为 TEST_PSK_V1；游戏状态一律使用加密单播。
- 断电恢复是首版范围，不接受“仅内存运行”的简化实现。
- 五种尺寸的数据包都完整，但发布状态独立版本化；未通过模拟门禁的 revision 为 candidate，
  不能在 standard 模式使用或宣称已完成平衡。

## 3. 范围与非目标

### 3.1 本规格包含

- 完整地产经营规则核心。
- 五种地图的逐格顺序、产权经济、特殊资产和库存。
- 二至六玩家、真实设备和模拟玩家混合座位。
- ESP-NOW 多 peer、配对、可靠传输、分片、恢复和隐私投影。
- 内置网页测试台、确定性机器人、场景和故障注入。
- LittleFS 写前日志、双槽快照和恢复。
- COM5 构建、烧录、自检和硬件验收。
- 玩家圆屏对接文档、消息映射和黄金字节向量。

### 3.2 本规格不包含

- 修改嘉立创 EDA 原理图或 PCB。
- 在当前阶段实现真实智能格子硬件驱动；由 BoardPositionAdapter 模拟。
- 在只有一台圆屏时声称完成六台真实射频设备的实物验收。
- 生产级 ECDH、Secure Boot、Flash Encryption 和 NVS Encryption；这些属于正式硬件
  安全规格，不以含糊的“未来支持”混入 TEST_PSK_V1。
- 复制 Hasbro 商标、棋盘视觉、产权卡版式、角色或受保护文案。

## 4. 成功标准

交付完成时必须同时满足：

1. COM5 固件可连接本地配置的家庭 Wi-Fi 并提供测试网站。
2. Wi-Fi 凭据、配对密钥和编译产物均未被 Git 跟踪。
3. 五种地图均可加载并通过结构、引用和经济完整性校验。
4. 一名真实圆屏能完成发现、加密配对、认领、快照、掷骰、移动、购买、收租、
   付款、筹款、断线和恢复流程。
5. 网页可控制其余模拟玩家，也可启用确定性机器人自动推进完整游戏。
6. 重复、乱序、丢失和延迟的 ESP-NOW 帧不会导致重复掷骰、重复扣款或状态分叉。
7. 测试服务器重启后能恢复到一致、可判定的暂停状态。
8. 六个逻辑玩家会话的队列、公私视图、版本推进和公平调度测试通过。
9. 核心可在无 Arduino 头文件的主机 CMake 目标中编译和测试。
10. 两小时自动场景老化期间无重启、持续堆下降或无界队列增长。

## 5. 总体架构

~~~text
真实玩家圆屏 ─ ESP-NOW ─┐
网页模拟玩家 ─ HTTP ─────┤
确定性机器人 ─ 内部命令 ─┼─> CommandBus ─> GameEngine ─> DomainEvent
模拟棋盘模块 ─ 内部事件 ─┘                         │
                                                  ├─> EventJournal / Snapshot
                                                  ├─> ConsoleView × 1..6
                                                  └─> AdminProjection
~~~

GameEngine 是唯一权威写入口。所有输入最终调用：

~~~cpp
DispatchResult GameEngine::dispatch(const Actor& actor, const Command& command);
~~~

每个模块只承担一个职责：

| 模块 | 责任 | 不得承担 |
| --- | --- | --- |
| GridopolyCore | 规则、状态机、地图校验、领域事件 | Arduino、无线、网页、Flash |
| GridopolyProtocol | 语义消息、二进制编解码、分片、黄金向量 | 执行游戏规则 |
| Application | 命令排队、机器人、场景、视图投影 | 在无线回调中改状态 |
| ArduinoPlatform | Wi-Fi、ESP-NOW、HTTP、LittleFS、NVS、时钟和随机源 | 包含规则分支 |
| WebConsole | 管理员测试 UI | 绕过 CommandBus 改余额或资产 |
| Persistence | 写前日志、快照、恢复 | 重新生成随机结果 |

## 6. 目录、生成器和依赖方向

~~~text
Gridopoly/
├─ CMakeLists.txt
├─ cmake/
│  └─ GridopolyOptions.cmake
├─ GameData/
│  ├─ schemas/
│  ├─ maps/
│  ├─ economies/
│  ├─ decks/
│  ├─ bots/
│  ├─ protocol-vectors/
│  └─ manifest.json
├─ tools/
│  └─ game-data/
│     ├─ generate.py
│     ├─ validate.py
│     └─ README.md
├─ Firmware/
│  ├─ libraries/
│  │  ├─ GridopolyCore/
│  │  │  ├─ CMakeLists.txt
│  │  │  ├─ library.properties
│  │  │  ├─ src/
│  │  │  └─ tests/
│  │  └─ GridopolyProtocol/
│  │     ├─ CMakeLists.txt
│  │     ├─ library.properties
│  │     ├─ src/
│  │     └─ tests/
│  ├─ TestGameServer/
│  │  ├─ TestGameServer.ino
│  │  ├─ partitions.csv
│  │  ├─ CMakeLists.txt
│  │  ├─ src/
│  │  │  ├─ app/
│  │  │  ├─ bots/
│  │  │  ├─ persistence/
│  │  │  ├─ platform/arduino/
│  │  │  └─ web/
│  │  ├─ generated/
│  │  ├─ config/
│  │  │  ├─ secrets.example.h
│  │  │  └─ secrets.local.h
│  │  ├─ tests/
│  │  └─ tools/
│  └─ PlayerConsole/
├─ tests/
│  └─ host/
│     ├─ CMakeLists.txt
│     ├─ core_tests.cpp
│     ├─ protocol_tests.cpp
│     └─ simulator_tests.cpp
└─ Docs/
~~~

主机 target 名称冻结为 gridopoly_core、gridopoly_protocol、gridopoly_host_tests 和
gridopoly_simulator。根 CMake 只构建平台无关代码，不包含 Arduino 头文件。Windows 和
Linux 使用同一入口：

~~~powershell
cmake -S . -B build-host -DGRIDOPOLY_BUILD_TESTS=ON
cmake --build build-host
ctest --test-dir build-host --output-on-failure
~~~

内容生成和核验入口：

~~~powershell
python tools/game-data/generate.py --input GameData --output Firmware/TestGameServer/generated
python tools/game-data/validate.py --input GameData --generated Firmware/TestGameServer/generated
~~~

生成结果和 manifest 可以提交；普通 Arduino/CMake 编译读取已提交生成物，不依赖
Python。CI 必须生成到临时目录后逐字节比较，陈旧生成物直接失败。

依赖只能由外向内：

~~~text
Arduino adapters / Web / Bots
            ↓
      Application
            ↓
Protocol semantics + GridopolyCore
~~~

GridopolyCore 不得包含 Arduino.h、MAC 地址、millis()、WiFi、ESP-NOW、WebServer、
Preferences、LittleFS 或 PROGMEM。固定上限通过领域常量表达，但不把 ESP32 存储细节
泄漏进公开接口。

GameData JSON 是长期权威源。开发工具生成只读 C++ 表供嵌入式使用；生成结果可提交，
但测试必须验证生成结果与规范化 JSON 的内容哈希一致。固件普通编译不依赖 Python。
规范化 JSON 使用 UTF-8、LF 换行、对象键按 UTF-8 字节序排序、无无意义空白，且数值只
允许十进制整数；内容哈希为完整 32 字节 SHA-256，不截断。

“规范化”具体使用 Gridopoly Canonical JSON v1：先把所有 key/value string 归一化为 Unicode
NFC，归一化后重复 key 直接拒绝；只接受有效 Unicode scalar value，不接受 lone surrogate。
对象 key 按其 NFC UTF-8 字节字典序排列，数组保持输入顺序。非 ASCII 字符必须原样编码为
UTF-8，禁止 `\uXXXX`；`"` 和 `\` 分别唯一编码为 `\"`、`\\`，U+0008/0009/000A/000C/000D
分别用 `\b/\t/\n/\f/\r`，其他 U+0000–001F 用小写 `\u00xx`，`/` 不转义。整数使用最短
十进制，零只写 `0`；true/false/null 小写；token 之间无空白，文档末尾恰好一个 LF。内容
源、manifest、HTTP 规范化 body hash 和其他写明“规范化 JSON”的位置都引用同一规则。

含 Unicode/引号/反斜杠/控制字符的黄金向量逻辑值为 name=`城市"A\B`、note 为由
`cafe + U+0301 + LF` 输入后得到的 NFC `café + LF`、value=-2；canonical 52 字节 hex 为
`7b226e616d65223a22e59f8ee5b8825c22415c5c42222c226e6f7465223a22636166c3a95c6e222c`
`2276616c7565223a2d327d0a`，SHA-256 为
`5ccf52d2720ec7f78aa71ed7b7490d9664ef85445a9ea5e7458518a4539042a0`。

content manifest 的 preimage 同样属于跨实现协议，不由目录遍历顺序或宿主路径决定。
每个可发布内容包必须生成一个 `manifest.json`，其顶层只含 `manifest_schema`（v1 为整数
1）和 `entries`。每个 entry 只含 `logical_path`、`kind`、`content_id`、`revision`、
`byte_length`、`sha256`；entry 按 logical_path 的 UTF-8 字节序严格递增。logical_path 只允许
小写 ASCII、数字、连字符、下划线、点和 `/`，不得以 `/` 开头、包含空段、`.`、`..` 或
反斜杠。它是生成器定义的逻辑路径，不含仓库根目录或本机绝对路径。

entries 必须完整覆盖本房间实际使用的 ruleset、map、economy、两套 resolved deck、
domain-events schema、game-state schema、protocol semantic schema 和可见内容字符串表；
不得包含未被本包引用的其他地图，也不得包含 manifest 自身。每项 byte_length 和 sha256
分别取该源文件规范化 JSON（含唯一结尾 LF）的实际字节数和完整 SHA-256。manifest 自身也
按相同规则规范化并带唯一结尾 LF；`manifest_sha256`/`content_manifest_hash` 就是这份
canonical `manifest.json` 全部字节的 SHA-256，manifest 内不放自身 hash。任何缺项、重复
logical_path、额外字段、长度/hash 不符或非规范编码都使内容包无效。最小哈希黄金向量是
35 字节 UTF-8 `{"entries":[],"manifest_schema":1}\n`，SHA-256 为
`6b8f934c1de876b59d8a71066a5863d907061d59e50af0838f3f62d90190c175`；该空表只测试
canonicalizer，生产 manifest 必须通过上述完整性校验。

生成器还从 manifest 中抽取会改变权威随机规则的 entries，生成
`rules-randomness-v1.json`：只含 ruleset、当前 map/economy、两套 resolved deck 和直接参与
规则裁决的 schema；明确排除 bot policy、响应延迟、网页/UI 字符串、图标和 transport 配置。
顶层精确为 `{"randomness_schema":1,"entries":[]}`（canonical 排序后 entries 在前）；每个
entry 完整复用 manifest entry 的六个字段 logical_path/kind/content_id/revision/byte_length/
sha256，仍按 logical_path UTF-8 严格递增，不允许额外字段。该文件使用同一 canonical JSON，
完整 SHA-256 为 `rules_randomness_hash`，并作为一个普通 entry 进入完整 content manifest。
空表黄金字节是 37 字节 `{"entries":[],"randomness_schema":1}\n`，SHA-256 为
`7a378f732f883f5170721a3d6388e7f9ac220f6f91ba28bddba775f6bd7a17d3`；生产表不得为空。
房间、快照和回放同时保存两个 hash。只改机器人策略或界面不得改变规则骰子/洗牌；任何
规则 entry 改变则必须改变 rules_randomness_hash。

网页的 HTML、CSS 和 JavaScript 是源文件；v1 生成 raw PROGMEM bytes，不生成 gzip。
源文件与实际 HTTP 响应 bytes 的哈希必须由生成器测试锁定，并与第 17.1 节 URL/ETag 完全一致。

## 7. 权威状态与提交管线

### 7.1 会话状态

~~~text
BOOT → NO_ROOM ─ROOM_CREATED→ LOBBY ─原子 start→ IN_GAME → GAME_OVER
                                └候选状态内 SETUP┘
                                            ↘ RECOVERING overlay ↗
~~~

SETUP 是 `/session/start` 候选副本中的过渡阶段，v1 不把它作为独立 state_version 持久或
向客户端暴露。要么整个开局以一个 COMMIT 进入 IN_GAME，要么 live state 仍为 LOBBY。

AuthorityStateBlob 另有与 session_phase 正交的持久 `room_lifecycle u8`：0 NO_ROOM、1 ACTIVE、
2 RESET_PREPARED、3 CREATING，其他值拒绝恢复。canonical room_id=0 的状态必须是 NO_ROOM；
CREATING 只允许第 19.5 节未激活的新 room base，不能对外投影；ROOM_CREATED 首 COMMIT 原子
把它改为 ACTIVE。普通房间的其他领域命令只允许 ACTIVE。第 19.5 节 ROOM_CLOSED
COMMIT 原子把 ACTIVE 改为 RESET_PREPARED，并使全部游戏/session 写入口失效。它不是新的
session_phase，不能被普通 ROOM_RESUMED 清除；该字段进入 snapshot/state_delta/post_state_hash
和不变量。`ROOM_CLOSED` 事件 schema 必须携带旧 room_id 与 resulting_lifecycle=RESET_PREPARED。

### 7.2 回合状态

~~~text
WAIT_ROLL
AWAIT_MOVE_CONFIRM
RESOLVE_TILE
PURCHASE_OFFER
AUCTION
RENT_CLAIM
PAYMENT
DEBT_RESOLUTION
EXTRA_ROLL
TURN_END
MORTGAGE_TAKEOVER
BUILDING_DEMAND
HOLD_DECISION
~~~

状态不使用组合爆炸的枚举。任意时刻只存在：

- 一个 session_phase。
- 一个 turn_phase。
- 最多一个阻塞回合的 blocking_transaction。
- 一个事务完成后的 continuation。

例如限制区第三次失败后的付款事务携带 MOVE_WITH_SAVED_DICE continuation，付款完成
后使用已经持久化的骰子移动，不重新掷骰。

### 7.3 可恢复的权威提交顺序

一次 dispatch 可以生成多个领域事件，但这些事件必须封装在同一条事务日志记录中，
共享 transaction_id 和一个新的 state_version。

~~~text
身份 / request_id / expected_state_version / 阶段 / deadline 校验
→ 在候选状态副本上生成随机结果并应用全部领域事件
→ 在候选状态上检查全部不变量并确定新 state_version
→ 预先序列化事务记录和幂等终态响应
→ 使用 4KiB 有界流缓冲将超过内存阈值的记录写入 storage-scratch 并验证
→ 向当前 journal 追加完整 COMMIT 记录并 flush
→ 以无分配、不可失败的 swap 将候选状态换入 live state
→ 更新 RAM 幂等缓存
→ 生成各玩家个性化视图
→ 发布命令终态和状态更新
~~~

禁止在候选状态通过不变量检查前写 journal。持久化失败时不得换入候选状态或返回
成功。journal 已 flush、但尚未换入内存时掉电是安全的：恢复器重放该 COMMIT，客户端
在新 session 的完整快照中看到结果。

BindingSlot NVS 与 Authority 虽非同介质，但共享唯一 `BindingRegistry` 串行域。无线回调只
排队 intent；所有 slot 状态写，以及 SEAT_BOUND、SEAT_ACK/RESUME presence、SEAT_UNBOUND、
takeover 和读取 binding identity 的候选提交，都必须在 CommandBus 取得该锁后完成。锁从
验证 exact `(slot_id,generation,state,binding_id,device_id)` 一直持有到 journal flush、所需
NVS promotion 和 live swap；不得在中间让远端 UNPAIR/FORCE 改 fence。第 19.4 节恢复同样在
最终重读六槽后持锁到 generation K meta 激活和 swap。若实现因 I/O 必须临时释放锁，则
flush/激活前必须再次逐字节重验快照，任一变化即废弃未激活候选并从新槽状态重建，绝不
提交 ghost seat。peer add/delete 仍由 RadioActor 执行，但其完成结果在同一序列域交接。

ACTIVE BindingSlot 的 occupant 字段只是 Authority→NVS 的可修复镜像，不是第二份座位
权威源：固定为 `occupant_room_id u64、seat_id u8、player_id u8、assignment_version u32`，
全零表示 PAIRED_UNSEATED。SEAT_BOUND、正常 controller/takeover 解绑、room reset 等仍以
Authority journal COMMIT 为唯一裁决；持 BindingRegistry 锁的顺序必须是 Authority COMMIT
flush → 按已持久候选原子写 NVS occupant mirror 并读回 → live swap/开放入口/发布终态。
mirror 写或读回若失败，已 flush COMMIT 不回滚、不返回成功，立即保持 command gate 关闭并
进入 RECOVERING，由 journal 重放后再 reconcile。

每次启动必须在开放无线/HTTP 与计算 resume_disposition 前逐槽 reconcile。若 ACTIVE slot 的
`binding_id/device_id/slot_generation` 在 Authority 恰好命中一个 REAL_CONSOLE 座位，就从
Authority 无条件重写并读回 occupant mirror；若该 ACTIVE identity 在 Authority 无引用，就
原子清零 mirror 为 PAIRED_UNSEATED；多重引用是不可恢复不变量错误。Authority 引用不存在、
非 ACTIVE 或 identity/generation 不符的 slot 时，仍按 NVS revocation fence 优先，在第 19.4 节
ROOM_RESUMED 候选中 SEAT_UNBOUND。UNPAIR_PENDING、FORCE_REVOKE_PENDING 和 tombstone 的
occupant/release intent 继续 NVS-wins，不受 ACTIVE mirror 规则反转。MAY_RESUME_SEAT 只能在
reconcile 完成后、Authority 与 ACTIVE mirror 逐字段一致时返回；绝不能只读陈旧 NVS occupant。

COMMIT 记录字段顺序冻结为：

~~~text
record_length        u32 LE
record_schema        u16 LE
record_type          u16 LE = 1 COMMIT
storage_generation   u32 LE
first_event_id       u64 LE
event_count          u16 LE
reserved0            u16 LE = 0
previous_version     u32 LE
committed_version    u32 LE
room_id              u64 LE
transaction_id       u32 LE
actor_kind           u8
actor_id             u8
reserved1            u16 LE = 0
sender_epoch         u32 LE
request_id           u32 LE
ingress_boot_id      u32 LE
ingress_sequence     u32 LE
received_at_ms       u32 LE
deadline_instance_id u32 LE，非 trigger 的普通输入为 0
system_trigger_id    u64 LE，仅 SYSTEM、BotScheduler 或 BoardAdapter 到期命令非零
request_payload_hash 32 bytes，规范化命令体的 SHA-256
terminal_format      u8，0 NONE / 1 ESP_EVENT / 2 HTTP_JSON / 3 SYSTEM / 4 SESSION_ASSIGNMENT
reserved2            u8 = 0
terminal_result_len  u16 LE
state_delta_len      u16 LE
event_payload_len    u16 LE
post_state_hash      32 bytes
terminal_result      terminal_result_len bytes
state_delta          state_delta_len bytes
event_payload        event_payload_len bytes
record_crc32         u32 LE
record_length_copy   u32 LE
~~~

record_length 包含首尾长度和 CRC 的整个记录。record_crc32 使用 CRC-32/ISO-HDLC，覆盖
record_schema 至 event_payload 的全部字节。
首尾长度、CRC、schema、版本连续性或 room_id 任一不合法时不得重放。文件尾部不完整
记录可作为断电尾记录丢弃；中部损坏必须进入 RECOVERING，不能跳过。

候选状态、事务缓冲和终态响应必须在写入前准备完毕。序列化器的工作内存上限为
4KiB；完整 state_delta 和 COMMIT 可以流式落入最大 64KiB 的 storage-scratch，读回验证
长度、SHA-256 和 CRC 后再流式追加 journal。scratch 不是权威状态，启动时一律忽略未完
或遗留内容，提交完成后可安全删除。journal flush 成功后不得再进行可能失败的动态
分配、规则校验或字符串生成。

deadline、机器人和恢复继续动作也只能经 dispatch。SYSTEM actor 的 request_id 为 0，
但必须携带由 room_id + transaction_id + trigger_kind + deadline_instance_id 唯一确定的
system_trigger_id；GameState 和 journal 保存当前 pending source/instance，重启或重复 tick
不得再次结算同一 source。preimage 固定为 ASCII `trigger-v1`、room_id u64 LE、
transaction_id u32 LE、trigger_kind u16 LE、deadline_instance_id u32 LE；取 SHA-256 前 8
字节小端；若该 64 位值为零，则依次尝试 digest[8..15]、[16..23]、[24..31] 的小端值，
取第一项非零者，四项全零则进入 INTERNAL_LIMIT/RECOVERING。零值保留。非 deadline
SYSTEM 工作也必须为其 pending source 分配同一字段。
例外之一是 BotScheduler 到期后直接入队的 actor_kind=BOT：它保留 BOT/player_id 审计身份，
request_id 固定等于非零 decision_index，且必须携带 trigger_kind=10 的非零
system_trigger_id。另一个例外是延迟模拟观察的 actor_kind=BOARD_ADAPTER：request_id 固定为
非零 observation_id，使用 trigger_kind=15 和该 pending observation 的 instance。REAL_CONSOLE、
WEB、ADMIN 的字段必须为 0。两种到期动作都只产生一条规则 COMMIT，不先提交 SYSTEM
“消费 pending”中间版本；候选结果必须同时关闭/替换其 pending source。

`trigger_kind u16` 的 v1 注册表固定为：1 PURCHASE_DEADLINE、2 AUCTION_DEADLINE、
3 RENT_DEADLINE、4 PAYMENT_DEADLINE、5 TRADE_DEADLINE、6 MOVE_DEADLINE、
7 MORTGAGE_TAKEOVER_DEADLINE、8 BUILDING_DEMAND_DEADLINE、9 HOLD_DECISION_DEADLINE、
10 BOT_PENDING、11 CARD_CONTINUATION、12 BANKRUPTCY_CONTINUATION、13 PRESENCE_OFFLINE、
14 RESERVED_SESSION_RECOVERY（v1 禁止创建）、15 BOARD_OBSERVATION、
16 RESERVED_SCENARIO（v1 禁止创建）、
17 BOT_PLANNER_CONTINUATION。
1–9 与快照 DEADLINES 的 kind 一一对应；10–13、15 和 17 的 source 也必须持久保存自己的
deadline_instance_id。14/16 只锁定数值、不接受 ingress/source；其他未注册值和 0 拒绝，新增语义必须升级同一
`GameData/schemas/system-triggers-v1.json` 注册表及 content hash。

所有 adapter 完整认证、解析并成功预留固定入口槽时，必须在 CommandBus 的同一临界区采样
服务端单调 `received_at_ms u32`，并分配本次启动内从 1 开始、不回绕的
`ingress_sequence u32`；`ingress_boot_id` 是本次启动的非零 CSPRNG u32。队列严格按
`(received_at_ms, ingress_sequence)` 串行。所有业务窗口都满足小于 2^31 ms，时间比较统一用
有符号模差；玩家输入仅在 `received_at_ms` 严格早于当前 deadline 时有效，等于 deadline
已经过期，客户端时间永不参与。计时器把 timeout 项的逻辑 `received_at_ms` 固定为 deadline，
并且只有同一 source 不存在更早的已入队合法输入时才可 dispatch；因此 deadline 前已入队但
排在其他工作后的命令仍先于 timeout 裁决。COMMIT 保存上述四个入口字段，相关领域事件也
保存 boundary outcome；重放只采用已提交结果，不重新比较时钟。入口序号达到
0xFFFFFFFF 时停止接收新写请求并安全重启，不能回绕。

任何 live 绝对 u32 时点若以 0 表示“无/终态”，创建时统一使用
`nonzero_abs(now,delta)`：先算 `d=u32(now+delta)`，若 d=0 则改为 1，并把 source 的实际
effective duration/original_window/saved_remaining 同步按这个至多多 1ms 的时点保存；否则用 d。
deadline_ms、due_ms、manual_available_at_ms、movement_deadline_ms 及其他采用 0 sentinel 的
expires/absolute 字段都引用同一函数，包括 delta=0 的 immediate source。因此活动时点永远非零，
跨约 49.7 天回绕也不会被误投影成终态；调度和 signed-delta 比较使用调整后的实际值。P7/host
黄金向量必须覆盖 now 接近 0xFFFFFFFF、raw d=0、调整为1并正确晚至多1ms裁决。

同一 tick 发现多个已到期 source 时，不允许由容器遍历或 RTOS 唤醒顺序决定
ingress_sequence。调度器先收集全部 ready source，再用 u32 有符号模差比较 due_ms（所有窗口
均小于 2^31 ms），并按
`(due_ms, trigger_kind, transaction_id, owner_sort_id, deadline_instance_id, request_sort_id)`
升序逐个分配 ingress_sequence；owner_sort_id 为该 source 的 seat/player_id，无主体取 0，
request_sort_id 对 BOT 为 decision_index、BOARD_ADAPTER 为 observation_id、其他为 0。完全相同
tuple 是持久状态不变量失败。外部输入恰等业务 deadline 仍因严格 `<` 判定为晚到，不能借较小
ingress_sequence 抢在 timeout 前产生有效副作用。此全局顺序同时用于 ESP32、主机模拟与未来
树莓派适配器。

所有工作流共用 AuthorityStateBlob 中持久的 `next_transaction_id u32`。新房间从 1 开始，
分配当前值后严格加一；交易、拍卖、付款、债务、移动、卡牌、建筑竞争和接管均
不得另设命名空间或从时间/MAC 派生。0 和 0xFFFFFFFF 保留；next 达 0xFFFFFFFF 时拒绝建立
新工作流、进入 REVISION_EXHAUSTED overlay 并只允许诊断/export/reset，绝不回绕。事务关闭也
不复用 ID；ROOM_CLOSED fence 是唯一 transaction_id=0 的 ADMIN lifecycle COMMIT，不属于工作流。

更严格地说，每条 Authority COMMIT 都必须有唯一可解释的 transaction_id：延续已有 workflow/
持久 source 的提交沿用该非零 ID；其他一次性 mutation 即使命令 envelope.transaction_id=0、
且提交内立即完成，也要在候选中从 next_transaction_id 分配 T。MORTGAGE_ASSET、
UNMORTGAGE_ASSET、即时 BUILD_LEVEL/SELL_BUILDING、controller/seat/presence 变更、ROOM_CREATED、
session start、ROOM_RESUMED 等都遵循此规则；若命令同时建立新 workflow，T 就是其 workflow ID。
业务/校验拒绝、flush 失败且未提交的候选不消费计数。HTTP/ESP-NOW 成功终态的 transaction_id
必须返回这个 T，COMMIT 内全部领域事件也用 T；不能以 envelope 的 0 填 journal。
唯一例外仍是 reset 的 ROOM_CLOSED fence，它固定为 0、只含该生命周期事件且不消费 counter。

所有计时/pending source 另共用持久 `next_deadline_instance_id u32`，同样从 1 单调分配，0 和
0xFFFFFFFF 保留且不回绕。每次新建窗口或明确重置窗口都分配新 instance；同一窗口重投
保持原 instance；取消窗口不消费下一号。SYSTEM dispatch 必须校验 instance 仍是当前
AuthorityState 项，成功提交时在同一原子状态中关闭它或替换成新 instance。因此旧 trigger
在下一条串行消息到达时会先因 instance 不匹配被拒绝。

成功 SYSTEM 候选必须原子关闭来源 instance 或把 pending source 推进到一个新分配的
instance；result blob 中绝不继续保留旧 instance。若 flush 前失败，live state 仍引用旧
instance，重投合法；若 flush 后掉电，重放后的状态已经关闭/替换它，旧 trigger 会被拒绝。
因此 v1 不持久化另一个 consumed-trigger 集合，也不在 swap 后修改 AuthorityState；精确一次
性完全由“单一串行入口 + 不复用的 instance + journal 原子提交”给出，28 项接管不会积累
已消费 ID。活动 deadline、最多六个 bot pending、八个 board observation 和 continuation 都是
各自 section 中的固定容量记录，预运行超限按已冻结的 OPERATION_CAPACITY/INTERNAL_LIMIT
边界拒绝，不能无界增长或丢弃活动项。

本局规则 RNG 与机器人 RNG 严格分流。规则 RNG 使用第 18 节同一 PCG-XSH-RR
64/32 算法和无偏闭区间映射，其唯一种子字节为 17 字节 ASCII
`GRIDOPOLY-RULE-V1` || room_seed u64 LE || rules_randomness_hash[32]。digest[0..7]小端为
initstate，digest[8..15]小端为 initseq，初始化步骤与第 18 节完全相同。每次提交只在
候选状态上推进 RNG；失败事务不消耗权威随机数。快照保存 state、increment 和
draw_index，journal 保存已产生的骰子/洗牌结果，恢复不重新生成。
`room_seed u64` 是 ROOM_CONFIG_AND_CONTENT 的 required critical 字段：session/new 的 16 位 hex
数值按 u64 保存（0 也是合法种子），并进入 snapshot、state_delta、post_state_hash 和 replay；
NO_ROOM 时固定为 0。它不进入玩家 ConsoleView/ESP-NOW 投影。恢复后创建新 BOT controller 或
执行 BOT_TAKEOVER 必须从这个持久值派生第 18 节 per-seat PCG，不能使用开机随机数或 HTTP RAM。

开局消耗顺序冻结为：先对 CE 牌堆从 i=15 到 1 取 `j=uniform(0,i)` 交换，再以同样
方式洗 CF；然后每名玩家按 seat_id 升序各取两次 `uniform(1,6)`，点数最高者先手。
最高点并列时仅并列者按 seat_id 升序重掷，初始轮加最多四轮重掷，总计最多五轮。第五轮
仍并列时，对最高并列 seat_id 升序数组做一次规则 RNG Fisher–Yates（i=count-1..1，
`j=uniform(0,i)`），排列首位为先手；最终排列和每个 j 进入 ROOM_SETUP_STARTED 的公开
FieldTLV，不另增事件。此后按 seat_id 环形顺序行动。每名玩家每轮的两颗开局骰子各形成
ROOM_SETUP_STARTED 中一条公开 `(round u8, seat_id u8, die_a u8, die_b u8)` RECORD_LIST
记录，而不是各自 DICE_ROLLED event；六人五轮最多 30 项。start COMMIT 始终只有
ROOM_SETUP_STARTED 与 GAME_STARTED 两事件，生成器必须断言 event_payload≤4KiB。正常回合中两颗骰子也按第一颗、第二颗
的顺序各消耗一次 `uniform(1,6)`。

规则 RNG 黄金向量：room_seed=`0123456789ABCDEF`、rules_randomness_hash 为 32 个零时，
digest=`60f75ccb9770a8508355d9e676d1c6d5bc181390ffdca28bbf56e265032020b1`，initstate=
`50A87097CB5CF760`，initseq=`D5C6D176E6D95583`，increment=`AB8DA2EDCDB2AB07`，前五个
原始输出为 `E3F208EB A4720B37 33DD8B3D 3FF5188F E7A6335C`，前两个骰子值为 2、6。

actor_kind：0 SYSTEM、1 REAL_CONSOLE、2 WEB、3 BOT、4 BOARD_ADAPTER、5 ADMIN；actor_id
对玩家来源为 player_id，其他来源为 0。terminal_result 最大 512 字节，state_delta 最大
48KiB，event_payload 最大 4KiB；完整 COMMIT 超过 56KiB 时先轮换 snapshot 后重试，仍
超限则拒绝事务并进入 RECOVERING，不能截断。

ESP_EVENT terminal_result 编码为 message_type u16、body_length u16、已冻结事件 body；不
保存会话信封。HTTP_JSON 是第 17 节规范化 UTF-8 result/error 对象，不含 token；SYSTEM
格式长度必须为 0。SESSION_ASSIGNMENT 只用于 SEAT_ASSIGNED：terminal_result 是第 13.5 节
的完整 body，但 16 字节 seat_token 位必须全为零。发送前根据已提交座位状态重新派生
该 binding 的 token 并填入；因此 journal、snapshot、replay export 和 RAM 幂等槽都不保存
明文 token。长度必须与 terminal_format 一致。

state_delta 是恢复的权威载荷，不通过重新执行规则恢复：

~~~text
delta_schema       u16 LE = 1
base_blob_length   u32 LE
result_blob_length u32 LE
operation_count    u16 LE，0–512
重复 operation_count 次：
  base_offset      u32 LE
  remove_length    u16 LE
  insert_length    u16 LE
  insert_bytes     insert_length bytes
~~~

operation 按 base_offset 升序、互不重叠，offset 全部指向提交前 AuthorityStateBlob。恢复器
复制未改变区间并应用替换，所得长度和 SHA-256 必须等于 result_blob_length 和
post_state_hash。任何越界、重叠、尾随字节或哈希不符都使 lineage 进入 RECOVERING。

event_payload 只用于审计、网页事件和回放，不参与恢复裁决：

~~~text
batch_schema u16 LE = 1
event_count  u16 LE，必须等于 COMMIT.event_count
重复 event_count 次：
  event_record_length u16 LE
  event_type          u16 LE
  event_schema        u16 LE = 1
  event_flags         u16 LE，bit 0 PUBLIC / bit 1 PRIVATE / bit 2 CRITICAL
  event_id            u64 LE
  field_payload_len   u16 LE
  field_payload       FieldTLV bytes
  event_crc32         u32 LE
  length_copy         u16 LE
~~~

event_record_length 包含自身首尾长度；event_crc32 覆盖 event_type 至 field_payload。事件
按 event_id 连续升序。FieldTLV 固定为 field_id u16、wire_type u8、flags u8、length u32、
value；field_id 升序且不重复。wire_type：1 U8、2 U16、3 U32、4 U64、5 I32、6 BYTES、
7 UTF8、8 RECORD_LIST。RECORD_LIST 为 count u16，随后每项 item_length u32 + item bytes。
FieldTLV flags bit 0 为 CRITICAL、bit 1 为 PRIVATE、bit 2–7 必须为零。未知 critical
field/event 拒绝，未知非 critical 可跳过。

event_type v1 主注册表：0x0001 ROOM_CREATED、0x0002 ROOM_SETUP_STARTED、0x0003
GAME_STARTED、0x0004 ROOM_RESUMED、0x0005 ROOM_CLOSED、0x0010 SEAT_BOUND、0x0011
SEAT_UNBOUND、0x0012 PLAYER_CONNECTION_CHANGED、0x0013 CONTROLLER_CHANGED、
0x0100 TURN_STARTED、0x0101 DICE_ROLLED、0x0102 MOVE_STARTED、0x0103
POSITION_CONFIRMED、0x0104 MOVE_COMPLETED、0x0105 MOVE_MANUAL_FALLBACK_OPENED、
0x0200 PURCHASE_OFFERED、0x0201 ASSET_PURCHASED、0x0202 AUCTION_OPENED、0x0203
AUCTION_BID_PLACED、0x0204 AUCTION_CLOSED、0x0205 AUCTION_PARTICIPANT_PASSED、
0x0210 RENT_CLAIMED、0x0211 PAYMENT_CREATED、0x0212 PAYMENT_COMPLETED、
0x0213 DEBT_STARTED、0x0214 DEBT_RESOLVED、0x0215 RENT_WAIVED、0x0220 ASSET_MORTGAGED、0x0221
ASSET_UNMORTGAGED、0x0222 BUILDING_CHANGED、0x0223 BUILDING_DEMAND_CHANGED、0x0230 TRADE_CREATED、0x0231
TRADE_UPDATED、0x0232 TRADE_CLOSED、0x0240 CARD_DRAWN、0x0241 CARD_EFFECT_APPLIED、
0x0250 PLAYER_HELD、0x0251 PLAYER_RELEASED、0x0260 PLAYER_BANKRUPT、0x0261
GAME_FINISHED、0x0270 CARD_MULTI_PAYMENT_PROGRESS、0x0280 BOARD_OBSERVATION_RECORDED、
0x0290 BOT_DECISION。

四个补充事件的 required FieldTLV 语义冻结如下，字段顺序/ID/type 由同一 schema 生成器锁定：

- MOVE_MANUAL_FALLBACK_OPENED：player_id u8、movement_transaction_id u32、target_position u8、
  workflow_revision u32、reason u8=1 DEADLINE_EXPIRED；它是 +60s source 关闭并进入
  WAIT_MANUAL_CONFIRM 的必有事件。
- AUCTION_PARTICIPANT_PASSED：auction_transaction_id u32、auction_kind u8、lot_id u16、
  player_id u8、auction_version u16（接受 PASS 后版本）、accepted_order u16、passed_mask u8。
- RENT_WAIVED：rent_transaction_id u32、landlord_player_id u8、payer_player_id u8、asset_id u16、
  quoted_amount i32、workflow_revision u32、reason u8=1 DEADLINE_EXPIRED。
- BUILDING_DEMAND_CHANGED：demand_transaction_id u32、workflow_revision u32、stage u8、
  actor_player_id u8（SYSTEM 为 0）、registered_mask u8、target_asset_id u16、target_level u8、
  list_cost i32、outcome u8。stage：1 OPENED、2 REGISTERED、3 REPLACED、4 DEADLINE_DIRECT_SETTLED、
  5 DEADLINE_AUCTION_OPENED、6 CLOSED_NO_STOCK；outcome：0 PENDING、1 TARGET_ACCEPTED、
  2 DIRECT_SETTLED、3 AUCTION_OPENED、4 NO_STOCK。SYSTEM deadline 的 target/level/cost 为 0。

adapter 投影也唯一冻结：MOVE_MANUAL_FALLBACK_OPENED 向该玩家发送 request_id=0、
observed_position=POSITION_UNKNOWN、reason=RFID_TIMEOUT、manual_available_at_ms=本 COMMIT
received_at_ms 的 RFID_POSITION_REJECTED 并发布 STATE_PATCH；AUCTION_PARTICIPANT_PASSED
投影 AUCTION_STATE；RENT_WAIVED 投影后继 TURN_STATUS/STATE_PATCH；BUILDING_DEMAND_CHANGED
投影 BUILDING_DEMAND_STATE，直接结算时再按 asset_id 升序投影 ASSET_UPDATED。domain event 与
wire event 不是两次权威提交。

字段 ID/type/required 组合的唯一权威文件是 GameData/schemas/domain-events-v1.json，进入
manifest hash；同 event_type 改字段含义必须提高 event_schema。
每条 COMMIT 的 event_count 必须为 1–32，不允许零事件提交。连接事件至少保存
player_id、online、assignment_version 和 presence_generation，不含 session key/token；控制器
事件保存 player_id、旧/新 controller_type、control_epoch、原因和 bot policy 引用。

事件 ID 关系冻结如下：新 room 的 next_event_id=1；含 k 个事件的候选令
`first_event_id=next_event_id`，event_payload 第 i 项（i=0..k-1）的 event_id 必须等于
first_event_id+i，成功候选再令 next_event_id+=k。0 保留；next_event_id 可达到
0xFFFFFFFFFFFFFFFF 作为 EXHAUSTED 哨兵但不得作为实际 event_id。普通/恢复候选必须保证成功
后的 next_event_id≤0xFFFFFFFFFFFFFFFE，从而把最后一个可分配 event_id
0xFFFFFFFFFFFFFFFE 专门留给单事件 ROOM_CLOSED；会侵占该保留量的候选不提交，只进入
REVISION_EXHAUSTED 诊断/reset gate。ROOM_CLOSED 恰好分配一项且允许把 next_event_id 推到
0xFFFFFFFFFFFFFFFF，任何其他加法回绕或使用 sentinel 都拒绝。snapshot.last_event_id 恒等于 next_event_id-1
（空新 room 为 0）；配套空 journal 的 base_event_id 必须等于 snapshot.last_event_id，
base_state_version 等于 snapshot.state_version。journal 首个 COMMIT.first_event_id 必须为
base_event_id+1，后续严格等于前一 COMMIT 的 `first_event_id+event_count`。恢复器逐条验证，
不能只检查 state_version。

### 7.4 核心不变量

- 事务提交后的现金不得为负。
- 一个资产最多一个所有者。
- 抵押资产不能收租或建设。
- 同组任意两块地产的建筑等级差不超过一。
- 建筑和地标总量不得超过地图库存。
- 同一时间最多一个阻塞回合的强制事务。
- 破产玩家不在后续回合顺序中。
- state_version 只在成功权威提交时单调增加。
- room_id=0 当且仅当 room_lifecycle=NO_ROOM；CREATING 不得成为 active meta 的可投影末端，
  RESET_PREPARED 不得接受 ROOM_RESUMED 或普通领域命令。
- state_version=0 只允许 NO_ROOM 或未激活 CREATING base；0xFFFFFFFF 只允许
  RESET_PREPARED，且其最后一条 COMMIT 必须是唯一 ROOM_CLOSED。
- 相同幂等键不能产生不同骰子或重复资产转移。
- 未确认目标位置前不能提前结算落点。
- 地图、经济、卡组和存档内容哈希必须匹配。

## 8. 完整规则和缺口裁决

### 8.1 规范优先级与内容身份

本规格获批后，设计期规则按以下顺序裁决：

1. 本规格明确冻结的规则缺口、地图版本和金额解析。
2. Docs/game/game-rules.md 的通用玩法，以及 game-state-machine.md 的事务和状态转换。
3. Docs/game/map-economy-spec.md 的通用地图校验；其中 grid-city-28-v1 数据只适用于 v1。
4. Docs/game/game-content-catalog.md 的卡牌 ID、效果方向和正常模式母版金额。
5. 协议、玩家屏和视觉文档；它们不能改变权威规则或经济结果。

高优先级条款只在明确声明覆盖时替代低优先级条款。同一优先级无法得出唯一结果时，
构建和内容校验必须失败，固件不得猜测或静默选取。

运行期权威来源不是仓库中的“最新文档”，而是房间冻结的内容清单：

~~~text
ruleset_id
map_id
map_revision
economy_id
deck_ids[]
content_hash
ResolvedEconomy
~~~

房间、快照和回放都保存完整清单。恢复时缺包、未知版本或哈希不符则进入
RECOVERING/UNSUPPORTED_CONTENT，不得用较新内容替代。

玩家屏可渲染内容另生成 `GameData/generated/console-catalog-v1.json`。它列出本固件可建房的
全部地图格子/资产、卡牌公开文案、颜色、图标和语义 schema 的 `{content_id, sha256}`，按
content_id 升序，使用第 6 节 canonical JSON；完整文件 SHA-256 为 `catalog_root_hash`。
每次构建把该 32 字节根同时生成到服务端和玩家屏常量，任一内容改变都产生新根。房间的
`content_manifest_hash` 标识本局完整规则与策略，catalog root 标识玩家屏渲染能力；两者
不可互换。玩家屏必须在任何座位 COMMIT 前通过第 13.5 节声明自己支持服务端所选 root，
否则返回 UNSUPPORTED_CONTENT。实现阶段的生成器必须把当前实际 root 写入交接文档和黄金
向量；设计文档不伪造一个尚未由 GameData 生成的常量。

28 格兼容矩阵：

| 项目 | grid-city-28-v1 | grid-city-28-v2 |
| --- | --- | --- |
| 定位 | 旧存档和旧回放恢复包 | 新游戏候选包；过平衡门禁后正式启用 |
| 经济 ID | grid-city-28-economy-v1 | grid-city-28-economy-v2 |
| 正常初始现金 | $1,500 | $1,200 |
| 起点奖励 | $200 | $140 |
| 限制区费用 | $50 | $40 |
| 城市服务费 | $120 | $160 |
| 地产价格合计 | $3,170 | $3,670 |
| 交通价格 | $200 | $160 |
| 基础设施价格 | $150 | $120 |
| 可购资产总值 | $4,270 | $4,550 |
| 建筑/地标库存 | 32 / 12 | 20 / 8 |
| 正常卡牌比例 | 1.0 | 0.8 |
| 新建房间 | 除兼容测试外不显示 | 使用 v2 |
| 恢复 | 仅完全匹配的 v1 包和冻结金额 | 仅完全匹配的 v2 包和冻结金额 |

地图包由 map_id、map_revision、economy_id 和 content_hash 联合标识。禁止混用 v1 地图与
v2 经济、用 v2 哈希恢复 v1、根据相同资产 ID 推断价格，或重算旧存档的
ResolvedEconomy。测试服务器不执行隐式迁移；未来迁移必须使用独立、版本化、可回滚的
离线工具。

16-v1、24-v1、28-v2、32-v1 和 40-v1 的正常经济均以本规格第 10 节为准；节奏只按
第 10.6 节解析。28-v1 继续使用现有 v1 内容包中冻结的正常经济和旧节奏表。

### 8.2 规则缺口决定

除上述显式覆盖外，现有 Docs/game/game-rules.md 的经典玩法继续有效：

| 项目 | 决定 |
| --- | --- |
| 购买窗口 | 15 秒；拒绝或超时立即拍卖 |
| 拍卖初始时长 | 30 秒 |
| 拍卖防抢秒 | 剩余少于 5 秒收到有效出价时恢复为 5 秒 |
| 起拍和最小加价 | 所有地图统一 $10 / $1 |
| 拍卖资格 | 所有未破产玩家，包括拒绝购买者 |
| 拍卖退出/结算 | 当前 leader 的报价有约束力，不能 PASS；非 leader PASS 后只剩 leader 则立即成交；deadline 有 leader 则成交、无 leader 则 NO_BIDS |
| 无人出价 | 资产继续归系统 |
| 收租 | 地主 20 秒，持续按住 1.2 秒；超时以 RENT_WAIVED(reason=DEADLINE_EXPIRED) 永久放弃本次租金并推进后继 |
| 付款 | 债务成立后 10 秒主动窗口；超时自动付款 |
| 交易期限 | CREATE/每次有效 UPDATE 后 60 秒；UPDATE 重置为新 deadline instance，CONFIRM 不重置；到期 EXPIRED |
| 实体移动期限 | guidance 起 +12 秒开放手动确认，+60 秒进入无 deadline 的 WAIT_MANUAL_CONFIRM；错误观察不重置任一时点 |
| 限制区决定 | 回合进入 HOLD_DECISION 后 20 秒；超时等同 TRY_DOUBLES，消费同一固定规则 RNG 流并遵守第三次失败付款规则 |
| 特殊设施掷骰 | 独立 rent_roll，不影响双骰和回合计数 |
| 地标成本 | 等于该地产一次建设成本，并交回四级建筑 |
| 抵押资产接管 | 按第 8.3 节逐项选择：立即支付抵押本金+10% 赎回，或先支付 10% 后保持抵押 |
| 建筑库存竞争 | 按第 8.4 节定义 5 秒需求窗口；供不应求时逐个拍卖建筑，不能复用无主资产拍卖的 lot 语义 |
| 通行许可卡交易 | 转移给对方；使用或归还系统时才回牌库 |
| 债务中交易 | 仅允许债务人发起直接筹资交易 |
| 交易并发 | v1 每名玩家最多参与一个未关闭交易；关闭后才可创建或接收下一笔 |
| 多人收付款顺序 | 从行动者后一个座位开始顺时针逐项原子结算 |
| 付款者中途破产 | 停止其余付款 |
| 收款过程中他人破产 | 完成该玩家破产后继续下一玩家 |
| 最终排名 | 胜者第一；其余按破产顺序倒序排列 |
| 六人角色 | 新增紫罗兰色“衡星”，身份为城市规划师，无技能和数值加成 |

机器人和网页玩家也必须遵守上述窗口、债务和事务规则。

`mode=demo && player_count=1` 是唯一终局例外：建房/start 时不执行“只剩一名未破产玩家”胜利
检查，只要该玩家尚未破产就继续用于协议/UI 验证。唯一玩家一旦向系统或任一规则义务破产，
同一 COMMIT 完成资产/现金清算、写 PLAYER_BANKRUPT 与 GAME_FINISHED，并以
GAME_OVER reason=SOLE_DEMO_PLAYER_BANKRUPT 结束；winner_player_id=0、ranking_count=1、
ranking_players=[该 player_id]，表示没有赢家。正常 2–6 人的 LAST_SOLVENT_PLAYER 检查和排名
完全不变；所有写着“只剩一名即结束”的条款都只适用于初始 player_count≥2。

### 8.3 抵押资产接管

玩家因交易或另一玩家向其破产而接收抵押资产时，所有权转移与建立阻塞的
`MORTGAGE_TAKEOVER` continuation 必须位于同一条 ownership-transfer COMMIT/state_delta；
不存在“先提交资产、下一 COMMIT 再建接管”的中间态。它冻结所有待处理项，并按接收者座位号、再按
asset_id 升序逐项处理，断线和重启不得重排。系统接收破产资产时不建立接管付息流程，
而是按破产规则清除所有者与抵押状态，使资产重新归系统；若仍有至少两名未破产玩家，
同一破产 continuation 立即冻结这些资产并按 asset_id 升序逐项打开 UNOWNED_ASSET 拍卖，
每项成交或 NO_BIDS 后才推进下一项。只剩一名未破产玩家时直接 GAME_OVER，不做无意义
拍卖。该资产列表、游标和当前 auction ledger 全部持久化，重启不重排或跳过。
系统作为债权人时，清除所有权/抵押与写入 SYSTEM_BANKRUPTCY_AUCTIONS 列表、游标和原
successor 也必须在同一破产 COMMIT；只有无线提示允许在 swap 后异步发送。

若向玩家破产转移后只剩这一名未破产玩家，先完成资产/现金转移并直接 GAME_OVER，不建立
接管利息义务，避免唯一赢家因系统费用再次破产。否则当前项向接收者发送
`MORTGAGE_TAKEOVER_REQUIRED`，选择窗口 20 秒：

- `REDEEM_NOW` 的金额是抵押本金加向上取整的 10% 利息；成功付款后立即清除抵押。
- `KEEP_MORTGAGED` 立即形成仅含向上取整 10% 利息的玩家到系统义务，资产保持抵押；以后
  普通赎回仍按抵押值 110% 计算。
- `REDEEM_NOW` 是可选升级；现金不足时拒绝该命令并保留选择窗口，不为可选升级强制举债。
- `KEEP_MORTGAGED` 和超时默认动作是强制义务；现金不足进入普通债务筹资，可能破产。
- 每项付款完成后推进冻结游标；全部完成后关闭 continuation，恢复原交易、破产或回合
  后继。两个交易参与者都收到抵押资产时仍按上述全局顺序串行处理。
- 若 KEEP_MORTGAGED 的系统债务使当前接收者破产，按当前 owner/recipient 对原 takeover
  的未处理尾部处理。若该破产 COMMIT 后只剩一名未破产玩家，同一 COMMIT 直接 GAME_OVER，
  丢弃系统拍卖、其他接收者 tail 和原 successor，唯一赢家不再承担 takeover 系统利息。
  只有仍有至少两名未破产玩家时才稳定分区：属于该破产者的项与其其他资产合并去重、清除抵押并按 asset_id
  升序进入 SYSTEM_BANKRUPTCY_AUCTIONS；属于其他仍未破产接收者的项保持原冻结相对顺序，
  不得误拍或跳过。AuthorityState 把它改写为一个扁平 composite continuation，阶段依次为
  `AUCTION_BANKRUPT_RECIPIENT_ASSETS → RESUME_OTHER_RECIPIENT_TAIL → ORIGINAL_SUCCESSOR`。
  系统拍卖结束后继续其他接收者 tail，全部完成后才恢复 takeover 开始前的原始后继；不再
  返回已破产接收者，也不建立嵌套 continuation。若分区后某段为空则直接进入下一段。

资产转移提交、冻结列表、当前游标、每项决定、付款/债务 transaction_id 和原 continuation
都进入 AuthorityStateBlob；`TRADE_OFFER(SETTLED)` 或 `BANKRUPTCY_RESOLVED` 仍是原命令
终态，接管提示是 request_id=0 的后续强制事件。

### 8.4 建筑库存竞争与拍卖

建筑等级 1–4 消耗 `BUILDING` 库存，等级 5 消耗 `LANDMARK` 库存。普通合法建设仍按
解析后的建设成本即时成交；只有同时满足“库存大于零”“至少两名玩家当前各有一个合法、
现金足够的同类单级建设候选”“库存少于这些潜在买家数”时，首个 `BUILD_LEVEL` 才打开
5 秒 `BUILDING_DEMAND` 阻塞事务，而不立即扣款。这里的“同时申请”被数字化为该冻结窗口，
避免按无线包先后把官方库存竞争规则变成抢包竞赛。

打开窗口的首个 `BUILD_LEVEL` 不是只负责创建窗口：同一 COMMIT 必须把它携带的目标作为
发起玩家的第一项登记，因此 OPEN 状态 `registered_mask` 至少包含发起者，且其个性化
`self_target_*` 非零。窗口创建成功就是该命令的唯一业务效果；不得要求发起者再发第二次
请求，也不得因先创建后登记留下可观察的空窗口。该 COMMIT 必须写
BUILDING_DEMAND_CHANGED(stage=OPENED)，后续首次登记/替换分别写 REGISTERED/REPLACED；不能用
尚未发生的 BUILDING_CHANGED 冒充。

潜在买家集合在窗口建立时冻结。集合内玩家以 `BUILD_LEVEL` 携带该 transaction_id 登记
一个单级、合法、按牌价承诺购买的目标；同一玩家的新请求可在截止前替换目标但不能撤销
登记。事务阻塞其他会改变现金、资产或库存的动作，因此截止时登记仍可确定性复核：

- 登记数不大于库存时，按座位号顺序在一个原子提交中以各地产牌价完成全部建设。
- 登记数大于库存时，库存单位按 lot_id 逐个拍卖；每个 lot 起拍 $10、最小加价 $1、初始
  30 秒并使用 5 秒防抢秒。当前未满足登记者都是该 lot 的初始竞价者。
- 建筑拍卖的有效出价必须同时给出竞价者当前仍合法的 target_asset_id；领先记录冻结该
  目标。赢家向系统支付成交价并在同一提交中把一个库存单位放到该目标，随后从待满足
  登记者移除。新 lot 重置领先者与退出 mask；上一 lot 退出者可参加下一 lot。
- 无人出价时该单位留在系统，关闭本次竞争；库存耗尽或登记者全部满足时也关闭。未满足
  玩家以后必须重新明确发起建设，服务器不保留等待队列。
- 库存为零时不打开空拍卖，命令直接以 `BUILDING_SUPPLY_EXHAUSTED` 拒绝；库存释放后由
  玩家用新 request_id 重试。

需求 deadline COMMIT 必须先写 BUILDING_DEMAND_CHANGED 的 DEADLINE_DIRECT_SETTLED 或
DEADLINE_AUCTION_OPENED，再按 asset_id 顺序追加 BUILDING_CHANGED 或 AUCTION_OPENED；任何关闭
分支至少有前一事件，不能产生 event_count=0。

每个玩家每个需求窗口最多登记一个单位。未来若允许一次请求多个单位，必须提高协议版本，
不能在 v1 中隐式循环。地标拍卖结算时同时把该地产原四级建筑归还 BUILDING 库存。

无主资产与建筑库存 lot 复用同一 AuctionEngine，但 AuthorityState 必须保存
auction_kind、lot_id、version、accepted_order u16 以及最多六项的 bidder ledger。每名玩家
只保留其当前最高有效 bid、对应 building target（无主资产为 0）和 accepted_order；新有效
出价必须高于全局 current_bid，ledger 不能仅保存 leader。结算按 bid 降序、再按更早
accepted_order 选择；若最高者现金或建筑目标因恢复后的权威校验已失效，标记该项无效并
尝试下一有效项，直到成交或 NO_BIDS。所有会改变竞价者现金、资产或建筑合法性的非拍卖
动作在 lot OPEN 时都被阻塞，因此正常运行不会制造失效项；ledger 仍用于断电恢复、故障
注入和严格满足回退规则。ledger、当前 leader、masks、deadline 和 remaining lots 全部进入
blocking transaction；ConsoleView ACTIVE_WORKFLOW 投影当前公开状态，其他人的未领先报价
不公开。

BUILDING_STOCK 的 lot_id 只在所属 BUILDING_DEMAND transaction 内分配：持久
`next_lot_id u16` 建立 demand 时为 1；开启每个库存 lot 时把当前值写入 `current_lot_id` 后加一。
0/0xFFFF 保留且不回绕；v1 每名玩家最多登记一个单位，所以合法 workflow 最多六个 lot，达到
sentinel 表示损坏/不变量失败。current_lot_id、next_lot_id 与 remaining lots 一起进入 snapshot/
state_delta。UNOWNED_ASSET 不使用该 counter，始终要求 lot_id=asset_id。

每个 lot OPEN 时固定 `auction_version=1、next_accepted_order=1`。客户端命令必须回显动作前
auction_version；只有 pre-version≤65533 才能接受 BID/PASS。合法动作先把
accepted_order=next_accepted_order 写入 bidder ledger/事件，再把 next_accepted_order 加一，并令
post auction_version=pre+1；若 post=65534，同一 COMMIT 接受该动作后立即按 ledger 结算，绝不
把 OPEN v65534 暴露给下一命令。0/65535 对 version/order 都保留，最大实际 accepted_order 为
65533，不回绕。每 peer 对 AUCTION 命令还受 CommandBus 每秒 10 条的有界入口限制，超限走未
入队 SERVER_BUSY。新 lot 使用新 lot_id，并把 version/next order 重新置 1。

当前 leader 提交 AUCTION_PASS 一律 ACTION_NOT_ALLOWED，其 ledger bid 不撤销。非 leader
PASS 的每个成功 COMMIT 都先写 AUCTION_PARTICIPANT_PASSED；若 eligible 中只剩 leader，随后在
同一 COMMIT 追加 AUCTION_CLOSED 并提前成交。否则只更新 passed_mask/version/order 也已有明确
审计事件。deadline 到达时有
leader 即按 ledger 回退规则成交，没有任何有效 leader 才 NO_BIDS。UNOWNED_ASSET 与
BUILDING_STOCK 完全相同。

地标出售通常降为四级且需要系统取出四个 BUILDING。若库存不足，玩家可把等级 5 原子
清算到 0–3 中库存足以表示的目标等级；收入等于地标半价加被同时出售的 `(4-target)` 个
建筑半价，最终只从库存取出 `target` 个建筑。该特例是一个提交，不暴露库存为负的中间态。

## 9. 实体移动和模拟棋盘

当前测试服务器没有真实智能格子。BoardPositionAdapter 由网页或确定性场景模拟：

1. 服务端持久化骰子结果并计算目标格。
2. 向当前圆屏发送 MOVE_GUIDANCE_STARTED。
3. 网页可注入正确格、错误格、延迟、无响应或顺序错误。
4. 服务端发送 RFID_POSITION_CONFIRMED 或 RFID_POSITION_REJECTED。
5. 圆屏超时后的手动确认仍只是命令，由服务器决定是否接受。
6. 位置确认后才进入 RESOLVE_TILE。

guidance 提交时同时分配 movement deadline instance：manual_available_at_ms=start+12000，
movement_deadline_ms=start+60000。错误格、错误 token、无响应和重复 observation 都不重置
这两个绝对时点。到 +60000 仍未确认时，SYSTEM COMMIT 把 workflow stage 改为
WAIT_MANUAL_CONFIRM、递增 workflow_revision、清除 deadline、写 MOVE_MANUAL_FALLBACK_OPENED
但保持路径/目标；此后真实正确 observation、玩家
RFID_TIMEOUT 手动确认或管理员测试确认仍可结束移动，服务器不得猜测落点或自动结算。
该 movement source 的 original_window_ms/saved_remaining_ms 以 60000ms 总窗口保存；12000ms
手动开放点不另建第二个 trigger，恢复时按第 19.4 节从总剩余唯一派生。

`/board/observe` 的有效输入先以一条有界候选 COMMIT 建立唯一 persistent pending source：保存
observation_id、目标 movement transaction/revision、position/result、source transaction_id、
deadline_instance_id、`original_window_ms=delay_ms`、接受提交时的 `saved_remaining_ms=delay_ms`
和 due_ms；同一 HTTP operation 不得建立第二份。delay_ms=0 也仍通过队列，只是 due 等于该
COMMIT.received_at_ms。到期后以 actor_kind=BOARD_ADAPTER、trigger_kind=15 和该 instance
裁决，并在同一 COMMIT 关闭 pending；过时 transaction/revision 只关闭为 stale，不把观察应用
到后来的移动。它与 BOT_PENDING 一样属于第 19.4 节必须恢复 remaining-time 的权威 time source。

`POSITION_UNKNOWN` 在线/API 统一为 u8 0xFF。result/observed_position 组合在 admission 与 dispatch
都必须复核：correct 要求 observed_position 精确等于该 movement 的 target；wrong 要求
`observed_position<tile_count` 且不等于 target；timeout/no_response 要求 observed_position=0xFF。
任何其他组合以 INVALID_ARGUMENT 成为该 HTTP operation 的可重放拒绝终态，不建立 observation
source，也不能把 0xFF 当作实际棋盘格。

API 的 transaction_id 是目标 movement transaction；source transaction_id 必须从第 7.3 节
全局 next_transaction_id 另行分配，不能复用目标 ID。它与新 observation_id、deadline instance
在 CREATED COMMIT 中原子分配，供 system_trigger_id、全局 timer tuple 和重放使用。

这两次 Authority COMMIT 都用既有 0x0280 BOARD_OBSERVATION_RECORDED 保证 event_count≥1。
其 FieldTLV 固定含 observation_id u32、stage u8、target_transaction_id u32、
target_workflow_revision u32、observed_position u8、result u8、original_window_ms u32、
deadline_instance_id u32、outcome u8；stage 1 CREATED 的 outcome 固定 0，随建立 pending 的
COMMIT 写入。stage 2 PROCESSED 随到期/关闭 COMMIT 写入，outcome：1 CONFIRMED、2 WRONG、
3 TIMEOUT、4 NO_RESPONSE、5 STALE_TRANSACTION。CONFIRMED/WRONG 可在同一 COMMIT 另带
RFID_POSITION_CONFIRMED/REJECTED 及后续规则事件；TIMEOUT/NO_RESPONSE/STALE 即使没有其他
规则变化也靠这条 stage 2 event 合法关闭 source。result：1 correct、2 wrong、3 timeout、
4 no_response；枚举不匹配拒绝创建。domain-events-v1.json 必须冻结这些 field ID/type/required。

pending observation 使用 AuthorityState 的固定八槽数组，槽态为 EMPTY/ACTIVE；另有持久
`next_observation_id u32`，每个新房间从 1 开始，每次成功建立 pending 时分配当前值并在同一
COMMIT 加一。0 不分配，0xFFFFFFFF 仅作 exhausted sentinel；分配 0xFFFFFFFE 后进入 sentinel，
后续创建以 REVISION_EXHAUSTED 失败并要求新 room，绝不回绕或复用已关闭 ID。八槽已满或
无法取得第 17 节的 admission reservation 时返回 OPERATION_CAPACITY，不淘汰活动观察。

未来真实格子只替换 BoardPositionAdapter，不修改规则核心或玩家屏语义消息。

## 10. 地图与经济

### 10.1 通用约束

- 角落必须位于 0、N/4、N/2、3N/4。
- 恰好一个 START、HOLD、REST 和 GOTO_HOLD。
- 资产主键为 map_id 与 asset_id 的组合；同一 A1 在不同地图可有不同经济表。
- 所有 v1 地图（包括 grid-city-28-v1 legacy）的可购 tile 固定
  `asset_id=u16(tile_position)`；position 0 是 START 且不可购，所以 asset_id 0 保留。颜色组
  A..H 的 group_id 固定为 1..8；交通、设施和非颜色 tile 的 group_id=0。任何生成表、
  UNOWNED_ASSET lot_id、wire/state/HTTP 投影都使用此数值，不能另按出现顺序编号。
- 地区组至少两块地产。
- 卡片目标使用语义标签，不写死显示名。
- 所有金额是正常节奏的显式整数，固件不运行插值公式。
- 28-v1 保留用于旧存档；28-v2 使用新 map_id、economy_id 和内容哈希。

### 10.2 地图摘要

| 地图 | 组规模 | 推荐人数 | 初始现金 | 起点 | 限制区 | 费用 | 建筑/地标 | 可购资产总值 |
| --- | --- | ---: | ---: | ---: | ---: | --- | --- | ---: |
| grid-city-16-v1 | 2/3/2 | 2–3 | 500 | 80 | 20 | 70 | 12/4 | 1,900 |
| grid-city-24-v1 | 2/3/3/2/2 | 2–4 | 800 | 120 | 30 | 110 | 18/6 | 3,040 |
| grid-city-28-v2 | 2/2/2/2/3/3 | 2–5 | 1,200 | 140 | 40 | 160 | 20/8 | 4,550 |
| grid-city-32-v1 | 2/3/3/2/3/2/2 | 2–6 | 1,200 | 160 | 40 | 160 | 24/10 | 4,550 |
| grid-city-40-v1 | 2/3/3/3/3/3/3/2 | 2–6 | 1,500 | 200 | 50 | 200、100 | 32/12 | 5,690 |

16 格库存从口头设计的 10 个建筑修正为 12 个。三地产组需要同时存在十二级建筑
才能第一次升级地标；库存为 10 会使地标永远不可达，违反已批准的经典建设规则。

“推荐人数”只用于预计时长和平衡提示，不是硬上限。五种地图的新房间都允许二至六名
总玩家，并可混合真实圆屏、网页玩家和机器人；超出推荐范围时显示警告但不拒绝。单人
Demo 只用于协议和 UI 验证，不计入正式平衡或胜率报告。

content release_status 与 room mode 是两个正交枚举，不能再把 candidate 当作 mode：

| 枚举 | 数值/JSON string | 含义 |
| --- | --- | --- |
| content | 1 / candidate | 完整可测、尚未通过 P14 平衡发布门禁 |
| content | 2 / formal | 指定 revision 已通过全部结构与平衡门禁 |
| content | 3 / legacy-recovery-only | 只允许旧存档恢复/回放，不允许 session/new |
| mode | 1 / test | 2–6 人；candidate/formal；fault 与本地 runner 可用；不计平衡发布样本 |
| mode | 2 / demo | 恰好 1 人；candidate/formal；fault 禁止、runner 可用；使用单人终局例外且不计平衡 |
| mode | 3 / standard | 2–6 人；只允许 formal；fault 与 auto/step/speed runner 禁止；正常经典终局 |

disconnect_policy 的持久数值/JSON string 冻结为 1/`pause`、2/`bot_takeover`、
3/`web_admin`。test 允许三者；demo 与 standard 只允许 pause。已知枚举但 mode 不允许返回
ROOM_MODE_FORBIDDEN，未知字符串、错误人数或字段组合返回 INVALID_ARGUMENT；policy 数值进入
ROOM_CONFIG、content-bound room hash、snapshot 和 replay，不能只保留显示字符串。

当前 `grid-city-16-v1`、`grid-city-24-v1`、`grid-city-28-v2`、`grid-city-32-v1`、
`grid-city-40-v1` 均为 candidate；`grid-city-28-v1` 为 legacy-recovery-only。因此当前可立即建立
test/demo 房间，standard 要等 P14 发布新的 formal revision。release_status 不添加到 manifest
root/entry：它是所选 map 源 JSON 的 required hashed 字段，session/new 从该 map_id/revision entry
解析并校验；promotion 必须改变 map bytes/hash/revision 与完整 manifest，不能原地把同一 hash 从
candidate 改名为 formal，同时仍满足第 6 节 manifest entry 只含六字段的合同。P14 的离线确定性模拟报告是发布判据；ESP32 上 test/demo/standard
实局都不被偷偷混入发布样本，standard 只可作为 formal 版的后验人工观察。

### 10.3 特殊资产

| 地图 | 交通价格/抵押 | 交通租金 | 设施价格/抵押 | 设施租金 |
| --- | --- | --- | --- | --- |
| 16 | 210 / 105 | 25 | 160 / 80 | 一个：骰子 ×4 |
| 24 | 160 / 80 | 20 / 40 / 80 | 110 / 55 | 一个：骰子 ×4 |
| 28-v2 | 160 / 80 | 20 / 40 / 80 / 160 | 120 / 60 | 一个 ×4；两个 ×10 |
| 32 | 160 / 80 | 20 / 40 / 80 / 160 | 120 / 60 | 一个 ×4；两个 ×10 |
| 40 | 200 / 100 | 25 / 50 / 100 / 200 | 150 / 75 | 一个 ×4；两个 ×10 |

城市事件要求前往设施并重新掷骰时仍使用卡片规定的 ×10 rent_roll。

### 10.4 完整逐格顺序

#### 16 格

~~~text
00 CORNER-START  START
01 A1            PROPERTY[A]
02 CARD-CF-1     CARD[公共基金]
03 A2            PROPERTY[A]
04 CORNER-HOLD   HOLD
05 B1            PROPERTY[B]
06 T-WEST        TRANSIT
07 B2            PROPERTY[B]  CORE_DESTINATION
08 CORNER-REST   REST
09 B3            PROPERTY[B]
10 U-ENERGY      UTILITY
11 CARD-CE-1     CARD[城市事件]
12 CORNER-GOTO   GOTO_HOLD
13 C1            PROPERTY[C]  HIGH_VALUE_ENTRY
14 FEE-CITY      FEE[70]
15 C2            PROPERTY[C]
~~~

#### 24 格

~~~text
00 CORNER-START  START
01 A1            PROPERTY[A]
02 CARD-CF-1     CARD[公共基金]
03 A2            PROPERTY[A]
04 FEE-CITY      FEE[110]
05 T-WEST        TRANSIT
06 CORNER-HOLD   HOLD
07 B1            PROPERTY[B]
08 B2            PROPERTY[B]
09 B3            PROPERTY[B]
10 U-ENERGY      UTILITY
11 T-NORTH       TRANSIT
12 CORNER-REST   REST
13 C1            PROPERTY[C]
14 CARD-CE-1     CARD[城市事件]
15 C2            PROPERTY[C]  CORE_DESTINATION
16 C3            PROPERTY[C]
17 T-EAST        TRANSIT
18 CORNER-GOTO   GOTO_HOLD
19 D1            PROPERTY[D]
20 CARD-CF-2     CARD[公共基金]
21 D2            PROPERTY[D]
22 E1            PROPERTY[E]  HIGH_VALUE_ENTRY
23 E2            PROPERTY[E]
~~~

#### 28 格 v2

~~~text
00 CORNER-START  START
01 A1            PROPERTY[A]
02 CARD-CE-1     CARD[城市事件]
03 A2            PROPERTY[A]
04 FEE-CITY      FEE[160]
05 T-WEST        TRANSIT
06 B1            PROPERTY[B]
07 CORNER-HOLD   HOLD
08 B2            PROPERTY[B]
09 CARD-CF-1     CARD[公共基金]
10 C1            PROPERTY[C]
11 U-ENERGY      UTILITY
12 C2            PROPERTY[C]
13 T-NORTH       TRANSIT
14 CORNER-REST   REST
15 D1            PROPERTY[D]  CORE_DESTINATION
16 CARD-CE-2     CARD[城市事件]
17 D2            PROPERTY[D]
18 E1            PROPERTY[E]
19 T-EAST        TRANSIT
20 E2            PROPERTY[E]
21 CORNER-GOTO   GOTO_HOLD
22 E3            PROPERTY[E]
23 F1            PROPERTY[F]  HIGH_VALUE_ENTRY
24 U-WATER       UTILITY
25 F2            PROPERTY[F]
26 T-SOUTH       TRANSIT
27 F3            PROPERTY[F]
~~~

#### 32 格

~~~text
00 CORNER-START  START
01 A1            PROPERTY[A]
02 CARD-CF-1     CARD[公共基金]
03 A2            PROPERTY[A]
04 FEE-CITY      FEE[160]
05 T-WEST        TRANSIT
06 B1            PROPERTY[B]
07 B2            PROPERTY[B]
08 CORNER-HOLD   HOLD
09 B3            PROPERTY[B]
10 U-ENERGY      UTILITY
11 C1            PROPERTY[C]
12 CARD-CE-1     CARD[城市事件]
13 C2            PROPERTY[C]
14 C3            PROPERTY[C]
15 T-NORTH       TRANSIT
16 CORNER-REST   REST
17 D1            PROPERTY[D]  CORE_DESTINATION
18 D2            PROPERTY[D]
19 CARD-CF-2     CARD[公共基金]
20 E1            PROPERTY[E]
21 T-EAST        TRANSIT
22 E2            PROPERTY[E]
23 E3            PROPERTY[E]
24 CORNER-GOTO   GOTO_HOLD
25 F1            PROPERTY[F]
26 U-WATER       UTILITY
27 F2            PROPERTY[F]
28 CARD-CE-2     CARD[城市事件]
29 G1            PROPERTY[G]  HIGH_VALUE_ENTRY
30 T-SOUTH       TRANSIT
31 G2            PROPERTY[G]
~~~

#### 40 格

功能顺序对应经典美版 40 格母版，显示内容仍使用 Gridopoly 原创资源。

~~~text
00 CORNER-START    START
01 A1              PROPERTY[A]
02 CARD-CF-1       CARD[公共基金]
03 A2              PROPERTY[A]
04 FEE-CITY        FEE[200]
05 T-WEST          TRANSIT
06 B1              PROPERTY[B]
07 CARD-CE-1       CARD[城市事件]
08 B2              PROPERTY[B]
09 B3              PROPERTY[B]
10 CORNER-HOLD     HOLD
11 C1              PROPERTY[C]
12 U-ENERGY        UTILITY
13 C2              PROPERTY[C]
14 C3              PROPERTY[C]
15 T-NORTH         TRANSIT
16 D1              PROPERTY[D]
17 CARD-CF-2       CARD[公共基金]
18 D2              PROPERTY[D]
19 D3              PROPERTY[D]
20 CORNER-REST     REST
21 E1              PROPERTY[E]
22 CARD-CE-2       CARD[城市事件]
23 E2              PROPERTY[E]  CORE_DESTINATION
24 E3              PROPERTY[E]
25 T-EAST          TRANSIT
26 F1              PROPERTY[F]
27 F2              PROPERTY[F]
28 U-WATER         UTILITY
29 F3              PROPERTY[F]
30 CORNER-GOTO     GOTO_HOLD
31 G1              PROPERTY[G]
32 G2              PROPERTY[G]
33 CARD-CF-3       CARD[公共基金]
34 G3              PROPERTY[G]
35 T-SOUTH         TRANSIT
36 CARD-CE-3       CARD[城市事件]
37 H1              PROPERTY[H]  HIGH_VALUE_ENTRY
38 FEE-DENSITY     FEE[100]
39 H2              PROPERTY[H]
~~~

### 10.5 产权经济表

L0 是基础租金；L1–L4 是建筑等级；LM 是地标。完整持有未建设组时租金为
2 × L0。抵押值已经显式列出。

#### 16 格

| ID | 价格 | L0 | L1 | L2 | L3 | L4 | LM | 建设 | 抵押 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| A1 | 100 | 2 | 10 | 30 | 90 | 133 | 183 | 20 | 50 |
| A2 | 120 | 3 | 13 | 33 | 100 | 150 | 200 | 20 | 60 |
| B1 | 180 | 5 | 23 | 67 | 183 | 250 | 317 | 30 | 90 |
| B2 | 200 | 5 | 23 | 67 | 183 | 250 | 317 | 30 | 100 |
| B3 | 220 | 5 | 27 | 73 | 200 | 267 | 333 | 30 | 110 |
| C1 | 330 | 12 | 58 | 167 | 367 | 433 | 500 | 70 | 165 |
| C2 | 380 | 17 | 67 | 200 | 467 | 567 | 667 | 70 | 190 |

#### 24 格

| ID | 价格 | L0 | L1 | L2 | L3 | L4 | LM | 建设 | 抵押 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| A1 | 60 | 1 | 5 | 16 | 48 | 85 | 133 | 30 | 30 |
| A2 | 70 | 2 | 11 | 32 | 96 | 171 | 240 | 30 | 35 |
| B1 | 100 | 3 | 16 | 48 | 144 | 213 | 293 | 30 | 50 |
| B2 | 110 | 3 | 16 | 48 | 144 | 213 | 293 | 30 | 55 |
| B3 | 120 | 4 | 21 | 53 | 160 | 240 | 320 | 30 | 60 |
| C1 | 170 | 7 | 37 | 107 | 293 | 400 | 507 | 50 | 85 |
| C2 | 180 | 7 | 37 | 107 | 293 | 400 | 507 | 50 | 90 |
| C3 | 190 | 9 | 43 | 117 | 320 | 427 | 533 | 50 | 95 |
| D1 | 260 | 12 | 59 | 176 | 427 | 520 | 613 | 80 | 130 |
| D2 | 280 | 13 | 64 | 192 | 453 | 547 | 640 | 80 | 140 |
| E1 | 430 | 19 | 93 | 267 | 587 | 693 | 800 | 110 | 215 |
| E2 | 480 | 27 | 107 | 320 | 747 | 907 | 1,067 | 110 | 240 |

#### 28 格 v2

| ID | 价格 | L0 | L1 | L2 | L3 | L4 | LM | 建设 | 抵押 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| A1 | 80 | 2 | 8 | 24 | 72 | 128 | 200 | 40 | 40 |
| A2 | 90 | 3 | 16 | 48 | 144 | 256 | 360 | 40 | 45 |
| B1 | 130 | 5 | 24 | 72 | 216 | 320 | 440 | 40 | 65 |
| B2 | 150 | 6 | 32 | 80 | 240 | 360 | 480 | 40 | 75 |
| C1 | 190 | 8 | 40 | 120 | 360 | 500 | 600 | 80 | 95 |
| C2 | 210 | 10 | 48 | 144 | 400 | 560 | 720 | 80 | 105 |
| D1 | 240 | 11 | 56 | 160 | 440 | 600 | 760 | 80 | 120 |
| D2 | 270 | 13 | 64 | 176 | 480 | 640 | 800 | 80 | 135 |
| E1 | 310 | 18 | 88 | 264 | 640 | 780 | 920 | 120 | 155 |
| E2 | 330 | 18 | 88 | 264 | 640 | 780 | 920 | 120 | 165 |
| E3 | 360 | 19 | 96 | 288 | 680 | 820 | 960 | 120 | 180 |
| F1 | 400 | 22 | 120 | 360 | 800 | 960 | 1,120 | 160 | 200 |
| F2 | 430 | 28 | 140 | 400 | 880 | 1,040 | 1,200 | 160 | 215 |
| F3 | 480 | 40 | 160 | 480 | 1,120 | 1,360 | 1,600 | 160 | 240 |

#### 32 格

| ID | 价格 | L0 | L1 | L2 | L3 | L4 | LM | 建设 | 抵押 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| A1 | 60 | 2 | 8 | 24 | 72 | 128 | 200 | 40 | 30 |
| A2 | 70 | 3 | 16 | 48 | 144 | 256 | 360 | 40 | 35 |
| B1 | 100 | 5 | 24 | 72 | 216 | 320 | 440 | 40 | 50 |
| B2 | 110 | 5 | 24 | 72 | 216 | 320 | 440 | 40 | 55 |
| B3 | 120 | 6 | 32 | 80 | 240 | 360 | 480 | 40 | 60 |
| C1 | 150 | 8 | 40 | 120 | 360 | 500 | 600 | 80 | 75 |
| C2 | 160 | 8 | 40 | 120 | 360 | 500 | 600 | 80 | 80 |
| C3 | 180 | 10 | 48 | 144 | 400 | 560 | 720 | 80 | 90 |
| D1 | 210 | 11 | 56 | 160 | 440 | 600 | 760 | 80 | 105 |
| D2 | 230 | 13 | 64 | 176 | 480 | 640 | 800 | 80 | 115 |
| E1 | 250 | 14 | 72 | 200 | 560 | 700 | 840 | 120 | 125 |
| E2 | 270 | 14 | 72 | 200 | 560 | 700 | 840 | 120 | 135 |
| E3 | 290 | 16 | 80 | 240 | 600 | 740 | 880 | 120 | 145 |
| F1 | 330 | 21 | 104 | 312 | 720 | 880 | 1,020 | 160 | 165 |
| F2 | 350 | 22 | 120 | 360 | 800 | 960 | 1,120 | 160 | 175 |
| G1 | 370 | 28 | 140 | 400 | 880 | 1,040 | 1,200 | 160 | 185 |
| G2 | 420 | 40 | 160 | 480 | 1,120 | 1,360 | 1,600 | 160 | 210 |

#### 40 格经典数值母版

| ID | 价格 | L0 | L1 | L2 | L3 | L4 | LM | 建设 | 抵押 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| A1 | 60 | 2 | 10 | 30 | 90 | 160 | 250 | 50 | 30 |
| A2 | 60 | 4 | 20 | 60 | 180 | 320 | 450 | 50 | 30 |
| B1 | 100 | 6 | 30 | 90 | 270 | 400 | 550 | 50 | 50 |
| B2 | 100 | 6 | 30 | 90 | 270 | 400 | 550 | 50 | 50 |
| B3 | 120 | 8 | 40 | 100 | 300 | 450 | 600 | 50 | 60 |
| C1 | 140 | 10 | 50 | 150 | 450 | 625 | 750 | 100 | 70 |
| C2 | 140 | 10 | 50 | 150 | 450 | 625 | 750 | 100 | 70 |
| C3 | 160 | 12 | 60 | 180 | 500 | 700 | 900 | 100 | 80 |
| D1 | 180 | 14 | 70 | 200 | 550 | 750 | 950 | 100 | 90 |
| D2 | 180 | 14 | 70 | 200 | 550 | 750 | 950 | 100 | 90 |
| D3 | 200 | 16 | 80 | 220 | 600 | 800 | 1,000 | 100 | 100 |
| E1 | 220 | 18 | 90 | 250 | 700 | 875 | 1,050 | 150 | 110 |
| E2 | 220 | 18 | 90 | 250 | 700 | 875 | 1,050 | 150 | 110 |
| E3 | 240 | 20 | 100 | 300 | 750 | 925 | 1,100 | 150 | 120 |
| F1 | 260 | 22 | 110 | 330 | 800 | 975 | 1,150 | 150 | 130 |
| F2 | 260 | 22 | 110 | 330 | 800 | 975 | 1,150 | 150 | 130 |
| F3 | 280 | 24 | 120 | 360 | 850 | 1,025 | 1,200 | 150 | 140 |
| G1 | 300 | 26 | 130 | 390 | 900 | 1,100 | 1,275 | 200 | 150 |
| G2 | 300 | 26 | 130 | 390 | 900 | 1,100 | 1,275 | 200 | 150 |
| G3 | 320 | 28 | 150 | 450 | 1,000 | 1,200 | 1,400 | 200 | 160 |
| H1 | 350 | 35 | 175 | 500 | 1,100 | 1,300 | 1,500 | 200 | 175 |
| H2 | 400 | 50 | 200 | 600 | 1,400 | 1,700 | 2,000 | 200 | 200 |

### 10.6 节奏和固定卡牌金额

每个地图的上述金额先作为正常模式冻结，再应用节奏预设：

| 参数 | 快 | 正常 | 慢 |
| --- | ---: | ---: | ---: |
| 初始现金 | ×0.80 | ×1.00 | ×1.20 |
| 起点奖励 | ×0.75 | ×1.00 | ×1.25 |
| 资产价格 | ×0.90 | ×1.00 | ×1.10 |
| 建设成本 | ×0.80 | ×1.00 | ×1.15 |
| 租金 | ×1.25 | ×1.00 | ×0.85 |
| 系统支付玩家 | ×0.80 | ×1.00 | ×1.20 |
| 玩家支付系统 | ×1.20 | ×1.00 | ×0.80 |
| 行动者支付每名其他玩家 | ×1.20 | ×1.00 | ×0.80 |
| 每名其他玩家支付行动者 | ×0.80 | ×1.00 | ×1.20 |

限制区费用不受节奏改变。初始现金、起点奖励、价格和建设取整到最近 $10；租金和固定
收支取整到最近 $1；恰好位于中点时向远离零的方向取整。抵押值从解析后价格取 50%，
赎回为抵押值的 110% 并向上取整。

派生租金运算顺序也冻结：普通地产先把表中对应 level 的租金应用 pace 并取整；完整颜色
组且未建设的 ×2 在该已解析 L0 上最后执行，不把原始 L0 先翻倍再乘 pace。交通资产先按
持有数量选择表项、应用 pace/取整，卡牌指定双倍再对该解析结果 ×2。设施普通租金先算
`dice_total×(持有1项?4:10)`，再应用 rent pace 并取最近 $1；卡牌“十倍骰子”把持有倍率
替换为 10（不是再乘一次），然后同样应用 pace/取整。所有后置 ×2 都做 int64 溢出检查。

正常模式卡牌固定金额由内容生成器按 map_cash_scale = 地图初始现金 / 1500
离线生成，非零结果取最近 $5 且最少 $5，再写入地图卡组解析表。固件不在运行时使用
map_cash_scale。

“向每名其他玩家支付/从每名其他玩家收取”的卡牌使用显式 per_target_amount。正常单目标
金额先按上述 map_cash_scale 生成；房间锁定时仅按卡牌相对行动者的方向应用一次对应的
玩家间节奏倍率，并取最近 $1，中点远离零。不得再乘总人数、聚合成一次债务、套用租金
倍率，或从付款方和收款方视角重复应用倍率。

两套 v1 牌堆数值身份冻结为：deck_id CE=1、CF=2；CE01..CE16 的 card_catalog_id 为 1..16，
CF01..CF16 为 17..32；effect_id 与 card_catalog_id 逐值相同。每局每个 catalog 恰有一个实体，
故初始及全生命周期 `card_instance_id=card_catalog_id`，抽取、持有、交易、使用、回牌库都不得
重分配。显示文案、地图解析金额或 revision 改变不能改变这些 ID；未来若同 catalog 允许多份，
必须升级 schema，不能在 v1 临时派生实例号。

卡牌开始时冻结 eligible_targets：排除行动者和开始前已破产玩家，断线、限制区或恢复中
玩家仍保留；从行动者后一座位起顺时针排列。`card_resolution_id` 固定等于抽卡父 workflow 的
transaction_id u32。扁平 continuation 最多五项，每项持久
`{item_index u8,target_player_id u8,per_target_amount i32,item_status u8,
child_payment_transaction_id u32}`；status：0 PENDING、1 PAYMENT_OPEN、2 DEBT_RESOLUTION、
3 PAID、4 BANKRUPT、5 SKIPPED。PENDING/SKIPPED 的 child ID 为 0，其他状态必须非零。
eligible_targets 为空时不创建 continuation/trigger，抽卡 COMMIT 直接以 CARD_EFFECT_APPLIED
恢复原 successor。

抽卡父 workflow transaction 记为 P。抽卡 COMMIT 令 Authority 唯一
`blocking_transaction=P/kind=CARD_RESOLUTION`，并建立绑定 P 的 parent trigger_kind=11 immediate
instance（due_ms=received_at_ms、original/saved remaining=0）。trigger 11 的 SYSTEM COMMIT 必须
使用 transaction_id=P，每次只找最小 index 的 PENDING：从全局 next_transaction_id 分配 child
payment transaction C，按卡牌方向唯一确定 payer/creditor，把该项改为 PAYMENT_OPEN、关闭当前
parent instance、创建绑定 C 的普通 trigger_kind=4 付款 deadline，并在同一候选原子把唯一
blocking transaction 从 P 切为 `C/kind=PAYMENT_DEBT`；它不直接扣款。P 仍作为持久 card
continuation identity 保存，但在 C 活跃期间绝不能同时投影成第二个 blocking transaction。

PAY_NOW、付款 deadline、SELL_BUILDING、MORTGAGE_ASSET 和 DECLARE_BANKRUPTCY 的
envelope/COMMIT transaction_id 都必须等于当前 C：现金不足则项改为 DEBT_RESOLUTION；付清为
PAID；payer 破产为 BANKRUPT。唯一例外是第 18 节直接筹资 TRADE_CREATE：envelope 引用 C，
成功 COMMIT/终态使用新 U 并把 C 暂置 DEBT_WAIT_FUNDING_TRADE；U 关闭后才回 C。C 达终态且仍有 PENDING
时，同一 COMMIT 关闭 C/payment-debt source，把唯一 blocking transaction 切回
`P/kind=CARD_RESOLUTION`，再为 P 分配一个新的 trigger 11 immediate instance；下一次 trigger 11
才执行 P→C2。没有 PENDING 时，同一 C 终态 COMMIT 同时关闭 C、父 continuation P，并恢复冻结
successor；不得留下短暂的 NONE/P 双 blocking 可观察状态。

行动者逐人付款且行动者在任一 child 破产时，同一 COMMIT 把所有后续 PENDING 标为 SKIPPED、
child ID 保持 0 并关闭 parent；其他玩家逐人付款时，该目标 BANKRUPT 后仍创建下一 parent
instance。trigger 11 绝不跳过 PAYMENT_OPEN/DEBT_RESOLUTION 项，也不与同一项 trigger 4 同时
active。每次创建 child、进入债务、PAID/BANKRUPT/SKIPPED 或关闭 parent 都至少写一条
CARD_MULTI_PAYMENT_PROGRESS，required 字段为 resolution_id、item_index、target/payer/creditor、
amount、old_status、new_status、child_payment_transaction_id、next_pending_index（无则 0xFF）。
快照/state_delta 保存整个列表、P、当前 C（如有）、parent/child instance、当前唯一 blocking
identity 和原 successor；恢复不变量要求 blocking 只能是 P 或当前未终态 C，并从状态/source 唯一
继续，不按 event 再执行。ConsoleView 的 ACTIVE_WORKFLOW、AVAILABLE_ACTIONS 和 deadline 只投影
当下唯一 blocking identity。名义总额只供界面展示，不是权威一次性债务。

冻结测试向量：28-v1 CE11 正常单目标 $50，在快/正常/慢为 $60/$50/$40；28-v1
CF09/CF10 正常 $10，对应 $8/$10/$12。28-v2 CE11 正常 $40，对应 $48/$40/$32；
28-v2 CF09/CF10 正常 $10，对应 $8/$10/$12。

五个新地图包均使用 `Docs/game/game-content-catalog.md` 中完整的 CE01–CE16 与
CF01–CF16，不因格数删减卡牌。内容生成器将移动目标解析为地图语义标记，将所有
固定金额解析为地图+节奏后的整数，并将 32 个解析结果写入本房间引用的两份 resolved-deck
源 JSON；manifest 仍只用六字段 entries 引用这两份 bytes/hash，不把卡牌字段直接塞进 manifest。

房屋/地标修缮卡先按 map_cash_scale 离线解析每级建筑和每个地标的正常单价，再只应用
一次“玩家支付系统”节奏倍率。抽卡时冻结该玩家的建筑数与地标数，两种单价分别
乘冻结数量后合并为一笔玩家→系统义务；不得在筹资过程中重新计数。

每局开始时，两套卡各以本局规则 RNG 做标准 Fisher–Yates 洗牌，洗后顺序直接进入首个
权威快照和回放。从顶部抽取：普通卡结算完置于底部；CE07/CF05 通行许可从牌堆移入
玩家持有区，使用、交易后使用，或破产归还系统时才置于原牌堆底部。牌堆顺序、抽取
游标、持有者与正在解析的卡牌全部持久化；恢复不重洗。

卡牌引起的移动依旧执行完整二次落点，但一个原始抽卡事务的嵌套解析深度最大为 8。
GameData 验证器必须证明所有卡牌移动图不会超过此上限或形成无界循环；否则内容包不可发布。

### 10.7 平衡门禁

每个地图、人数和节奏组合运行四套机器人矩阵：全保守、全均衡、全激进，以及三策略按
座位轮换的混合矩阵。每套候选至少 10,000 局；发布判定提高到 50,000 局。混合矩阵必须
让每个策略在每个座位出现次数之差不超过一。记录：

模拟局 `game_index` 从 0 连续递增。matrix 枚举为 1 CONSERVATIVE、2 BALANCED、
3 AGGRESSIVE、4 MIXED；pace 为 1 FAST、2 NORMAL、3 SLOW。每局唯一种子为
`SHA-256(ASCII "GRIDOPOLY-BALANCE-V1" || map_id_length u8 || map_id 小写 ASCII ||
map_revision u16 LE || pace u8 || player_count u8 || matrix u8 || game_index u32 LE ||
rules_randomness_hash[32])` 的前 8 字节小端。MIXED 中座位 `seat_id` 的策略索引固定为
`(seat_id - 1 + game_index) % 3`，依次对应保守、均衡、激进；其他矩阵全部同策略。
同一组合必须恰好运行索引 `[0,N)`，不能按线程数、墙钟或失败重试换种子。并行 worker
只改变执行顺序，汇总按 game_index 排序。

- 含双骰、卡片和限制区的逐格落点概率。
- 首个完整组、首次建设、首次破产和结束回合。
- 每组购买率、建设率、投资回收和胜率贡献。
- 座位、先手、交通、设施和交易的影响。
- 拍卖成交率、折价和现金枯竭时间。
- 进入 1,000 回合上限仍未结束的比例。

发布门槛：

- 任意座位胜率相对平均值偏差不超过 5 个百分点。
- 1,000 回合未结束率低于 0.5%。
- 管制站出口后的中价热点可以保留，但最高普通地产落点概率不得超过普通地产中位数
  的 1.35 倍。
- 特殊资产压倒门禁使用两个整数指标。对每个 map/player_count/pace/matrix，某一资产组或
  特殊类别的 `rent_roi_permille=floor(1000×累计实收租金/累计购买与建设现金支出)`；
  `winner_hold_permille=floor(1000×结束时赢家持有该组或该类至少一项的已完成局数/
  已完成局数)`。支出分母为 0 时该类不能判为压倒。交通类或设施类只有在 ROI 严格大于
  该组合所有普通颜色组最大 ROI 的 110%，且 winner_hold 严格高于普通组最大值 50 permille，
  才在该 matrix 记为 DOMINANT；同一 map/player_count/pace 下四个 matrix 全部 DOMINANT 才
  使发布失败。比较全部用交叉相乘，不能先做浮点百分比。
- 正常模式中位时长目标：16 格 20–35 分钟，24 格 30–50 分钟，28 格 35–60 分钟，
  32 格 45–75 分钟，40 格 60–120 分钟。

未通过门禁只能生成新的 economy_id 或 map revision，不能静默修改已发布表。
所有比例门槛使用整数计数交叉相乘比较，不使用浮点；1,000 回合仍未结束的局计入未结束
分母和位置统计，但不伪造赢家。中位数采用排序后 `floor((N-1)/2)` 项，时长由确定性虚拟
时钟累计，而不是主机运行耗时。虚拟时钟只累加 bot `planned_delay_ms`、协议中实际走到的
deadline 等待和模拟棋盘每次固定 750ms 的 observation delay；规则计算、网页、无线、动画
和主机调度耗时均为 0。队列跳到下一个 ready 时必须复用第 7.3 节完整全局
`(due_ms, trigger_kind, transaction_id, owner_sort_id, deadline_instance_id, request_sort_id)` tuple；
bot 的 seat_id/decision_index 只分别映射到 owner_sort_id/request_sort_id，不能另立简化排序。
不得按测试机速度采样分钟。

## 11. 多玩家屏身份和视图

device_id、seat_id 和 player_id 相互独立：

- server_id：u64，服务端安装时生成并持久化。
- device_id：u64，圆屏首次启动生成，不等于 MAC。
- binding_id：u64，一次设备配对关系。
- room_id：u64，一局游戏的持久标识。
- session_id：u32，每个 binding 每次成功 LINK 握手都由服务端 CSPRNG 生成的全新非零
  链路会话 ID；不是 room ID，也不在 peer 间共享。
- sender_epoch：u32，每个可靠发送 incarnation 独立生成的非零 CSPRNG 值；bootstrap 与
  current session 的具体生命周期按第 13.2/14.2 节，不能简化成“每设备每次启动一个”。
- seat_token：从 NVS 中的 CSPRNG 主密钥按下述 PRF 派生的非零 16 字节座位恢复凭据。
- request_id：u32；0x1000–0x1013 在每个 current session+sender_epoch 内共享一个严格递增
  玩家命令序列，其他会话请求按各自 message_type 域。
- transaction_id：u32，按第 7.3 节持久单调分配的服务端工作流标识。
- state_version：u32，每次权威提交严格加一。

request_id 接近保留上限时重建 sender_epoch/link。state_version 的普通/恢复 COMMIT 最大
`committed_version=0xFFFFFFFE`；达到该值后只开放只读诊断、导出与 reset。0xFFFFFFFF 仅允许
恰好一条把 ACTIVE 改为 RESET_PREPARED 的 ROOM_CLOSED fence 使用，且该生命周期下不再追加
COMMIT。ROOM_CLOSED 的 transaction_id=0，不消费 next_transaction_id，因此 transaction counter
已进入 0xFFFFFFFF sentinel 也不能堵死 reset。其他 counter/revision 耗尽同样只开诊断/reset gate，
不得要求一个已经无法分配的普通 transaction 才能清除房间。零值始终保留。

每座 `assignment_version u32` 新 room 初值 0，第一次绑定变 1；以后每次绑定、解绑或永久
接管严格加一。`control_epoch u32` 新 room 初值 1，每次 effective controller 改变严格加一。
两者的 0/0xFFFFFFFF 均不作为活动版本；当前值达到 0xFFFFFFFE 且下一动作需要递增时，
拒绝该动作并进入 RECOVERING/要求新 room，绝不回绕。presence_generation 同样不回绕，
但每个新 assignment 可重新从 1 开始，因为 assignment_version 已改变其身份域。

房间可混合真实圆屏、网页控制和机器人。真实设备绑定后，该座位机器人立即停止。

服务端先构造 ConsoleView(player_id)，再序列化。不得先发送全局状态再删除字段。

本玩家可见完整现金、资产、卡片、债务、合法操作和参与中的交易。其他玩家公开余额、
位置、限制区、破产、资产、抵押和建筑。禁止发送其他玩家私有卡片、无关交易草案、
牌堆顺序、随机种子、机器人评分、密钥和其他设备 seat_token。

全局 state_version 每次提交都递增。每个在线圆屏必须收到个性化 STATE_PATCH；若该
版本没有可见内容，则收到空 VERSION_ADVANCE，避免全局版本与隐私过滤产生缺口。

## 12. 语义消息

ESP-NOW 二进制和未来 WebSocket JSON 都映射到同一 Command/Event 语义。

### 12.1 链路与会话消息号

| ID | 消息 |
| ---: | --- |
| 0x0001 | LINK_ACK |
| 0x0002 | PING |
| 0x0003 | PONG |
| 0x0004 | LINK_HELLO |
| 0x0005 | LINK_WELCOME |
| 0x0006 | PROTOCOL_ERROR |
| 0x0100 | DISCOVERY |
| 0x0101 | PAIR_REQUEST |
| 0x0102 | PAIR_CHALLENGE |
| 0x0103 | PAIR_CONFIRM |
| 0x0104 | PAIR_ACCEPT |
| 0x0105 | PAIR_REJECT |
| 0x0106 | UNPAIR |
| 0x0107 | RECOVERY_BEACON |
| 0x0200 | CLIENT_HELLO |
| 0x0201 | SERVER_HELLO |
| 0x0202 | JOIN_REQUEST |
| 0x0203 | SEAT_ASSIGNED |
| 0x0204 | SEAT_ACK |
| 0x0205 | SESSION_RESUME |
| 0x0206 | SESSION_REVOKED |
| 0x0207 | RESYNC_REQUEST |
| 0x0208 | SNAPSHOT_APPLIED |
| 0x0209 | SYNC_COMPLETE |

### 12.2 玩家命令

| ID | 命令 |
| ---: | --- |
| 0x1000 | ROLL_REQUEST |
| 0x1001 | PURCHASE_ACCEPT |
| 0x1002 | PURCHASE_DECLINE |
| 0x1003 | CLAIM_RENT |
| 0x1004 | PAY_NOW |
| 0x1005 | AUCTION_BID |
| 0x1006 | AUCTION_PASS |
| 0x1007 | TRADE_CREATE |
| 0x1008 | TRADE_UPDATE |
| 0x1009 | TRADE_REJECT |
| 0x100A | TRADE_CONFIRM |
| 0x100B | MORTGAGE_ASSET |
| 0x100C | MORTGAGE_BATCH_REQUEST |
| 0x100D | UNMORTGAGE_ASSET |
| 0x100E | BUILD_LEVEL |
| 0x100F | SELL_BUILDING |
| 0x1010 | HOLD_DECISION |
| 0x1011 | DECLARE_BANKRUPTCY |
| 0x1012 | MOVE_MANUAL_CONFIRM_REQUEST |
| 0x1013 | MORTGAGE_TAKEOVER_DECISION |

### 12.3 玩家命令体与唯一终态

所有 0x1000–0x1013 命令使用 13 字节前缀；全部多字节字段为小端：

~~~text
body_schema       u8      v1 固定为 1
request_id        u32     非零
transaction_id    u32
client_time_ms    u32     只用于诊断，不参与裁决
~~~

权威 expected_state_version 取自信封 state_version。所有金额为非负 int32，服务端中间
计算使用 int64 并检查溢出。payload 长度必须与动态计数精确相等；资产和卡片实例 ID 的
0 保留；动态 ID 数组升序且不得重复。发送者由加密 peer 与座位绑定推导，payload 不得
自报 player_id。引用 offer、debt、auction、trade 或 movement 的命令必须携带对应非零
transaction_id；独立自愿操作按下表使用 0。所有游戏命令 payload 上限 210 字节。

四个交易列表编码固定为四个 u8 计数，随后依次编码 give_asset_ids、
receive_asset_ids、give_card_ids、receive_card_ids，每项为 u16。两资产列表合计最多
28 项，两卡片列表合计最多 8 项；列表内升序、无重复，give/receive 集合不得相交；
任一方向都必须包含正现金或至少一个对象。

| ID | 13 字节前缀后的字段，按线序 | transaction_id | 长度 | 成功终态 |
| ---: | --- | --- | ---: | --- |
| 0x1000 ROLL_REQUEST | 无 | 0 | 13 | ROLL_RESULT |
| 0x1001 PURCHASE_ACCEPT | asset_id u16；quoted_price i32，均大于 0 | PURCHASE_OFFER | 19 | ASSET_UPDATED，必须显示购买完成 |
| 0x1002 PURCHASE_DECLINE | asset_id u16，非零 | PURCHASE_OFFER | 15 | AUCTION_STATE，必须显示拍卖已开启 |
| 0x1003 CLAIM_RENT | debtor_player_id u8 1–6；asset_id u16；quoted_amount i32>0 | RENT_OPPORTUNITY | 20 | PAYMENT_REQUIRED |
| 0x1004 PAY_NOW | quoted_amount i32>0 | debt | 17 | 现金足够为 PAYMENT_COMPLETED；不足为 DEBT_RESOLUTION_REQUIRED |
| 0x1005 AUCTION_BID | auction_kind u8；lot_id u16；auction_version u16>0；bid_amount i32>0；target_asset_id u16 | auction | 24 | AUCTION_STATE，含已接受版本、领先者和目标 |
| 0x1006 AUCTION_PASS | auction_kind u8；lot_id u16；auction_version u16>0 | auction | 18 | AUCTION_STATE，含退出后的版本 |
| 0x1007 TRADE_CREATE | counterparty_id u8；give_cash i32；receive_cash i32；四个列表 | 自愿为 0；债务直接筹资为当前父 debt D | 26–98 | TRADE_OFFER，trade_version=1；成功返回新 trade U |
| 0x1008 TRADE_UPDATE | expected_trade_version u16>0；counterparty_id u8；give_cash i32；receive_cash i32；四个列表 | trade | 28–100 | TRADE_OFFER，版本递增 |
| 0x1009 TRADE_REJECT | expected_trade_version u16>0 | trade | 15 | TRADE_OFFER，REJECTED/CLOSED |
| 0x100A TRADE_CONFIRM | expected_trade_version u16>0 | trade | 15 | TRADE_OFFER，WAITING_COUNTERPARTY 或 SETTLED |
| 0x100B MORTGAGE_ASSET | asset_id u16 | 自愿为 0；债务中为 debt | 15 | ASSET_UPDATED |
| 0x100C MORTGAGE_BATCH_REQUEST | asset_count u8 1–28；asset_ids u16 × count | 自愿为 0；债务中为 debt | 16–70 | MORTGAGE_BATCH_COMPLETED |
| 0x100D UNMORTGAGE_ASSET | asset_id u16；quoted_cost i32>0 | 0 | 19 | ASSET_UPDATED |
| 0x100E BUILD_LEVEL | asset_id u16；target_level u8 1–5；quoted_cost i32>0 | 即时/新需求为 0；登记为 building demand | 20 | 即时为 ASSET_UPDATED；需求窗口为 BUILDING_DEMAND_STATE |
| 0x100F SELL_BUILDING | asset_id u16；target_level u8 0–4；quoted_proceeds i32>0 | 自愿为 0；债务中为 debt | 20 | ASSET_UPDATED |
| 0x1010 HOLD_DECISION | decision u8；card_instance_id u16；quoted_amount i32 | hold decision | 20 | 见下文 |
| 0x1011 DECLARE_BANKRUPTCY | 无 | debt | 13 | BANKRUPTCY_RESOLVED |
| 0x1012 MOVE_MANUAL_CONFIRM_REQUEST | target_position u8；reason u8 | movement | 15 | RFID_POSITION_CONFIRMED 或 RFID_POSITION_REJECTED |
| 0x1013 MORTGAGE_TAKEOVER_DECISION | asset_id u16；decision u8；quoted_amount i32>0 | mortgage takeover | 20 | PAYMENT_REQUIRED |

counterparty_id 不能是发送者。交易 give/receive 字段始终从发送者视角解释。BUILD_LEVEL
必须恰好从当前等级加一，等级 5 表示地标。SELL_BUILDING 通常恰好减一；只有第 8.4 节
“地标且建筑库存不足”的原子清算允许从 5 直接降到 0–3。客户端 quote 只是 UI 所见预期，
服务端必须重算；不一致返回 STALE_STATE、ASSET_CHANGED 或 INVALID_ARGUMENT。

auction_kind：1 UNOWNED_ASSET、2 BUILDING_STOCK。无主资产拍卖要求 lot_id=asset_id 且
target_asset_id=0；建筑库存拍卖要求 lot_id 精确匹配当前库存 lot，target_asset_id 是该
竞价者要放置该单位的合法地产。kind、lot、version 或目标任一不匹配都拒绝，不能把建筑
竞价解释成资产购买。

AUCTION_STATE 的 kind 相关规范值也冻结：UNOWNED_ASSET 要求 lot_id=asset_id!=0、
building_type=0、leader_target_asset_id=0；remaining_unit_count 在 OPEN 为 1、终态为 0。
BUILDING_STOCK 要求 asset_id=0、lot_id!=0、building_type=1 BUILDING 或 2 LANDMARK；没有
leader 时 leader_target_asset_id=0，有 leader 时必须是其当前 ledger 中的合法目标；OPEN 的
remaining_unit_count 是“包含当前 lot 在内仍计划拍卖的单位数”，每成交一 lot 递减，任何
终态事件为 0，不代表系统物理库存被清零。valid_bidder_mask 的 bit 仅表示该玩家已有一笔
当前仍通过现金/目标复核的 standing ledger bid；它是 eligible_mask 的子集，与 passed_mask
不相交，leader 非零时必须位于其中。NO_BIDS/CANCELLED 强制 current_bid、leader、
leader_target、valid_bidder_mask 和 remaining_unit_count 全零；SETTLED 保留成交价、赢家和
建筑目标（如有），remaining_unit_count 为 0。ACTIVE_WORKFLOW kind=3 完全复用这些值域，
且因只投影活动流程，其 status 必须为 OPEN、remaining_unit_count 必须非零。

MORTGAGE_TAKEOVER_DECISION 的 decision：1 REDEEM_NOW，quoted_amount 必须是抵押本金加
向上取整 10%；2 KEEP_MORTGAGED，quoted_amount 必须是向上取整 10%。命令只接受
AVAILABLE_ACTIONS/ACTIVE_WORKFLOW 所列当前 asset_id；选择后形成同 transaction 下的
PAYMENT_REQUIRED。REDEEM_NOW 现金不足用 INSUFFICIENT_CASH 拒绝并保留提示；
KEEP_MORTGAGED 的强制付款不足则进入债务流程。

MORTGAGE_TAKEOVER_REQUIRED.item_index 与 ACTIVE_WORKFLOW.cursor 均为零基，合法范围
0..item_count-1；current_asset_id 必须等于 asset_ids[cursor]。

HOLD_DECISION 枚举：1 TRY_DOUBLES 要求 card=0、amount=0，终态 ROLL_RESULT；2
PAY_FEE 要求 card=0、amount>0，终态 PAYMENT_REQUIRED；3 USE_CARD 要求 card 非零、
amount=0，终态 TURN_STATUS。MOVE_MANUAL_CONFIRM_REQUEST 的 reason：1 RFID_TIMEOUT、
2 RFID_HARDWARE_FAILURE、3 ADMIN_ASSISTED_TEST；普通圆屏只允许 1，2/3 需要相应能力
或管理员测试注入。

在同一活动 session 内，每个首次合法 admission、并绑定到该 request_id/semantic hash 的原命令
必须向发起 peer 发送且只发送一个成功终态，或一个 COMMAND_REJECTED。同 ID 异 hash 不是第二个
可裁决命令，不进入 CommandBus、不取得/覆盖终态槽，只走第 13.2/15.3 节会话致命协议例外。
COMMAND_ACCEPTED 可选，只表示进入权威队列，永远不是终态。所有与命令相关的成功终态
以如下字段开始：

~~~text
body_schema       u8 = 1
request_id        u32，与原命令一致
transaction_id    u32
replayed          u8，0 首次、1 幂等缓存重放
~~~

同一领域事件发给其他 peer 时 request_id=0、replayed=0。终态之后的移动、付款、交易等
事件保留 transaction_id，但 request_id=0。已完成请求的重复提交直接重放原终态，不再
发送新的 COMMAND_ACCEPTED。PAY_NOW 现金不足不是拒绝：同一候选把 PAYMENT 工作流原子推进
到 DEBT_RESOLUTION、递增 workflow_revision、写 DEBT_STARTED，并把匹配原 request_id 的
DEBT_RESOLUTION_REQUIRED 作为唯一成功终态缓存；不得先发 COMMAND_REJECTED，也不得再给
发起者补发 request_id=0 的同一事件。其他 peer 的同一领域广播仍使用 request_id=0。
INSUFFICIENT_CASH 只用于 REDEEM_NOW 等不改变权威状态、明确保留原可选窗口的真正拒绝。
ROLL_RESULT 是
ROLL_REQUEST 的终态，MOVE_GUIDANCE_STARTED/MOVE_RESULT 是后续事务事件。交易 SETTLED
之后的 ASSET_UPDATED/PLAYER_UPDATED 也不是原交易命令终态。

当命令以 transaction_id=0 开始新工作流时，成功终态可以携带服务端新分配的非零
transaction_id；后续事件必须沿用它。拒绝终态仍携带命令中的 transaction_id。

### 12.4 服务端事件

| ID | 事件 |
| ---: | --- |
| 0x2000 | COMMAND_ACCEPTED |
| 0x2001 | COMMAND_REJECTED |
| 0x2010 | TURN_STATUS |
| 0x2011 | ROLL_RESULT |
| 0x2012 | MOVE_GUIDANCE_STARTED |
| 0x2013 | RFID_POSITION_CONFIRMED |
| 0x2014 | RFID_POSITION_REJECTED |
| 0x2015 | MOVE_RESULT |
| 0x2020 | PURCHASE_OFFER |
| 0x2021 | RENT_OPPORTUNITY |
| 0x2022 | PAYMENT_REQUIRED |
| 0x2023 | PAYMENT_COMPLETED |
| 0x2024 | DEBT_RESOLUTION_REQUIRED |
| 0x2025 | MORTGAGE_BATCH_COMPLETED |
| 0x2026 | BANKRUPTCY_RESOLVED |
| 0x2030 | TRADE_OFFER |
| 0x2031 | AUCTION_STATE |
| 0x2032 | CARD_REVEALED |
| 0x2033 | ASSET_UPDATED |
| 0x2034 | PLAYER_UPDATED |
| 0x2035 | ROOM_RECOVERING |
| 0x2036 | GAME_OVER |
| 0x2037 | MORTGAGE_TAKEOVER_REQUIRED |
| 0x2038 | BUILDING_DEMAND_STATE |
| 0x3000 | STATE_SNAPSHOT |
| 0x3001 | STATE_PATCH |
| 0x3002 | VERSION_ADVANCE |

同一活动 session 内，客户端只有收到匹配 request_id 的上述成功终态或
缓存终态 COMMAND_REJECTED 才以“已知成功/失败”清除 pending。唯一例外是
`COMMAND_REJECTED(SERVER_BUSY, RETRY_SAME_REQUEST)`：它是 admission 非终态，客户端必须
保留 pending/同一 ID 和 body，且在重试成功前不得发送更高 command request_id。新 LINK_WELCOME 分配不同 session_id
时使用下述会话失效对账规则，不将这些旧请求带入新幂等命名空间。

## 13. ESP-NOW v1 二进制绑定

### 13.1 40 字节头

当前线协议版本冻结为 1.0。首次配对由 PAIR_REQUEST/PAIR_CHALLENGE 在证明生成前选择
共同 minor；已有 binding 重连则由 LINK_HELLO/LINK_WELCOME 选择。主版本不一致直接
拒绝，没有共同 minor 也拒绝。CLIENT_HELLO/SERVER_HELLO 只确认已经选定的版本、能力
和资源上限，不进行第二次协商。完成选择后，所有会话帧的 protocol_minor 必须等于该值。

为使 codec 在读取协商 body 前有唯一入口，v1 bootstrap 头版本冻结如下：DISCOVERY、
PAIR_REQUEST、PAIR_CHALLENGE、PAIR_REJECT、RECOVERY_BEACON、LINK_HELLO 和 LINK_WELCOME
的 header `protocol_major=1, protocol_minor=0`，不表示最终选择；PAIR_CHALLENGE/WELCOME
body 中的 selected_major/minor 才是结果。PAIR_CONFIRM、PAIR_ACCEPT 使用 challenge 已选版本；
新 session 从 CLIENT_HELLO 起全部使用 WELCOME 已选版本。UNPAIR current 域使用该 binding
当前选定版本，session_id=0 recovery 域使用 UnpairRecord 保存的选定版本。LINK_ACK 使用
被确认可靠域的头版本；bootstrap PROTOCOL_ERROR 使用已认证请求的头版本。接收端先按本段
识别固定 1.0 bootstrap，再解析 body 范围；不得以本地首选 minor 在读 body 前拒绝合法协商。

能力位同样属于 v1 兼容契约：bit 0 FRAGMENTATION、bit 1 STATE_PATCH、bit 2
AUCTION_UI、bit 3 TRADE_UI、bit 4 BUILD_UI、bit 5 DEBT_UI、bit 6
MANUAL_POSITION_CONFIRM、bit 7 UTF8_ZH_CN、bit 8 CLOCK_SYNC、bit 9
MORTGAGE_TAKEOVER_UI；bit 10–31 必须
发送为零并由接收方忽略。服务器在入房前按房间所需功能校验能力，缺少必需能力时返回
可诊断拒绝，不允许进入后静默降级规则。

selected_capabilities 必须是双方能力按位交集的子集；服务端可以清除房间不需要的可选
位，但不能设置任一方未声明的位。required_capabilities 必须全部存在于 selected 中。

具体 mask 不留给实现猜测：room_id=0 的 PAIRED_UNSEATED 基础 required mask 为
`0x00000181`（FRAGMENTATION | UTF8_ZH_CN | CLOCK_SYNC）；服务端此时仍把双方共同支持的
全部已知 bit 0–9 放入 selected，不得因大厅暂时“不需要”而提前清除未来入房能力。任一可
开局 v1 房间的 required mask 固定为 `0x000003FF`，即 bit 0–9 全部必需，包括
MANUAL_POSITION_CONFIRM；因为每局都可能进入拍卖、交易、建设、债务、抵押接管和 RFID
超时手动兜底。JOIN_REQUEST 在任何座位 COMMIT 前检查当前房间 mask，不足则以
UNSUPPORTED_CAPABILITY 拒绝且不分配座位。一次 LINK 中 LINK_WELCOME、CLIENT_HELLO 和
SERVER_HELLO 的 selected_capabilities 必须逐位相等；HELLO 只能确认，不能二次增删。

整个 esp_now_send 数据不得超过 250 字节。

v1 线格式的全局标量规则不可由宿主 ABI 推断：除明确写成 raw bytes、ASCII、UTF-8 或单字节
u8/i8 的字段外，所有多字节整数一律按小端编码。所有具有布尔语义的线字段一律编码为
u8，合法值只有 0 和 1；接收端必须拒绝其他值，不能按“非零即真”宽松解析。

| 偏移 | 字段 | 类型 |
| ---: | --- | --- |
| 0 | magic，固定 47 50 | 2 bytes |
| 2 | protocol_major | u8 |
| 3 | protocol_minor | u8 |
| 4 | header_length，40 或 48 | u8 |
| 5 | flags | u8 |
| 6 | message_type | u16 LE |
| 8 | session_id | u32 LE |
| 12 | sender_epoch | u32 LE |
| 16 | frame_seq | u32 LE |
| 20 | ack_base | u32 LE |
| 24 | ack_bits | u32 LE |
| 28 | message_id | u32 LE |
| 32 | state_version | u32 LE |
| 36 | payload_length | u16 LE |
| 38 | fragment_index | u8 |
| 39 | fragment_count | u8 |

flags：

- bit 0 RELIABLE
- bit 1 ACK_ONLY
- bit 2 FRAGMENTED
- bit 3 PAIRING
- bit 4 RESPONSE
- bit 5 CRITICAL
- bit 6–7 必须为零

每种消息的合法信封冻结为；“current”表示 LINK_WELCOME 选定的非零 session：

| 消息 | flags | 加密 | session_id | header state_version |
| --- | ---: | --- | --- | --- |
| LINK_ACK | 0x02 | 是 | 被确认的 0/bootstrap 或 current | 0 |
| PING / PONG | 0x00 / 0x10 | 是 | current | 0 |
| DISCOVERY | 0x08 | 否 | 0 | 0 |
| PAIR_REQUEST / PAIR_CHALLENGE / PAIR_REJECT | 0x08 / 0x18 / 0x18 | 否 | 0 | 0 |
| PAIR_CONFIRM / PAIR_ACCEPT | 0x09 / 0x19 | 是 | 0 | 0 |
| UNPAIR REQUEST / ACKNOWLEDGED / FINAL | 0x09 / 0x19 / 0x09 | 是 | current；无活动 session 时为 0 的 UNPAIR recovery 域 | 0 |
| RECOVERY_BEACON | 0x08 | 否 | 0 | 0 |
| LINK_HELLO / LINK_WELCOME | 0x01 / 0x11 | 是 | 0 | 0 |
| PROTOCOL_ERROR | 0x11 | 是 | 认证发生在 bootstrap 则 0，否则 current | bootstrap 为 0；current 瞬时错误为首次构造版本，缓存终态为 terminal_state_version |
| CLIENT_HELLO / SERVER_HELLO | 0x01 / 0x11 | 是 | current | 0 |
| JOIN_REQUEST | 0x01 | 是 | current | 客户端 expected_state_version |
| SEAT_ASSIGNED | 0x11 | 是 | current | NEW_JOIN 为座位提交后版本；WAIT_ONLINE 的 RESUME 为 presence 提交后版本；ONLINE 重复 RESUME/REISSUE 为当前版本 |
| SEAT_ACK | 0x01 | 是 | current | SEAT_ASSIGNED 的版本 |
| SESSION_RESUME | 0x01 | 是 | current | 0 |
| SESSION_REVOKED | 直接响应 0x11；异步撤销 0x01 | 是 | current | 直接响应为缓存 terminal_state_version；异步撤销为首次构造版本 |
| RESYNC_REQUEST | 0x01 | 是 | current | 0 |
| SNAPSHOT_APPLIED | 0x01 | 是 | current | 已应用的 snapshot_state_version |
| SYNC_COMPLETE | 0x11 | 是 | current | catchup_target_version |
| 0x1000–0x1013 玩家命令 | 0x01 | 是 | current | 客户端 expected_state_version |
| COMMAND_ACCEPTED | 0x11 | 是 | current | 首次接受时版本；它不是终态 |
| COMMAND_REJECTED | 0x11 | 是 | current | 缓存终态取 terminal_state_version；唯一非缓存 SERVER_BUSY 取首次构造时 Authority current_state_version；两者都等于错误体 current_state_version |
| 仅第 12.4 节明确注册的成功事件 ID，request_id 非零的发起者直接成功终态 | 0x11 | 是 | current | 产生终态的提交版本 |
| 仅第 12.4 节明确注册的成功事件 ID，request_id=0 的异步/后续/他座投影 | 0x01 | 是 | current | 产生事件的提交版本 |
| STATE_SNAPSHOT | 0x05 | 是 | current | snapshot_state_version |
| STATE_PATCH | 未分片 0x01；分片 0x05 | 是 | current | to_version |
| VERSION_ADVANCE | 0x01 | 是 | current | to_version |

方向也是 v1 严格信封的一部分：BIDI 仅有 LINK_ACK 和按 UnpairRecord stage 限制的 UNPAIR。
圆屏→服务端仅允许 PING、LINK_HELLO、
PAIR_REQUEST、PAIR_CONFIRM、CLIENT_HELLO、JOIN_REQUEST、SEAT_ACK、SESSION_RESUME、
RESYNC_REQUEST、SNAPSHOT_APPLIED 和 0x1000–0x1013。服务端→圆屏仅允许 PONG、
LINK_WELCOME、PROTOCOL_ERROR、DISCOVERY、PAIR_CHALLENGE、PAIR_ACCEPT、PAIR_REJECT、RECOVERY_BEACON、
SERVER_HELLO、SEAT_ASSIGNED、SESSION_REVOKED、SYNC_COMPLETE、COMMAND_ACCEPTED/
COMMAND_REJECTED、第 12.4 节明确注册的成功事件 ID 和
0x3000–0x3002。接收端必须在更新 ACK 窗口前检查本地 role、物理来源和方向；未认证的反向
消息静默丢弃，已认证的方向错误也不生成错误响应，而是拒绝/关闭所属 session 或 bootstrap
incarnation。绝不能因为消息号可解析就 ACK 或进入业务队列。

PROTOCOL_ERROR 只由服务端响应已认证且可安全归属的入站错误；圆屏永不发送它，服务端也
绝不以 PROTOCOL_ERROR 响应另一个 PROTOCOL_ERROR、方向错误消息或无法认证/归属的帧，避免
错误回环。圆屏遇到坏的服务端状态同步帧按第 13.3 节 RESYNC，其他已认证但破坏信封不变量的
服务端帧关闭 session 并重做 LINK。

0x2002–0x200F、0x2016–0x201F、0x2027–0x202F 以及该区间其他未在第 12.4 节逐项注册的 gap
全部 reserved，不因落在数值范围就合法。任一端在 current session 收到已认证、未注册且
CRITICAL=0 的 message_type，必须在更新 RX/ACK 窗口前拒绝、不 ACK、不回 PROTOCOL_ERROR，
并关闭 current session；发送方只能重新 LINK/升级，不能当 unknown optional 跳过。若同一未知
message_type 设置 CRITICAL=1，则严格走下段未来扩展规则：服务端仅对可归属圆屏回
UNSUPPORTED_MESSAGE 并安全暂停，圆屏侧关闭/re-LINK。P7 semantic golden 必须分别覆盖一个
gap+CRITICAL=0 和一个 unknown+CRITICAL=1，不能混成同一预期。

RELIABLE=1 的行必须使用非零 frame_seq 和 message_id；RELIABLE=0 的行两者固定为 0。
JOIN_REQUEST 明确携带提交前的期望版本；NEW_JOIN 的 SEAT_ASSIGNED 携带座位提交后
版本。REISSUE 不做新权威提交；SESSION_RESUME 若处于 WAIT_ONLINE，则其
SEAT_ASSIGNED(RESUME) 是第 13.5 节 presence COMMIT 的 SESSION_ASSIGNMENT terminal，携带
提交后版本；已 ONLINE 的幂等重复不增版，携带裁决时当前版本。

FRAGMENTED 必须同时设置 RELIABLE，并在基础值上增加 0x04。RESPONSE 只表示对请求或
握手的直接响应，不表示命令终态。CRITICAL 预留给未来未知消息，当前所有已定义消息的
bit 5 必须为零。服务端从已认证、方向可归属的圆屏收到未知 CRITICAL 消息时才可返回
UNSUPPORTED_MESSAGE 并安全暂停；圆屏从服务端收到它则关闭 session/re-LINK，不发
PROTOCOL_ERROR。未认证来源静默丢弃。

ACK_ONLY 只能用于 LINK_ACK，且不得同时设置 RELIABLE、FRAGMENTED 或 PAIRING。
PAIRING 只能用于 0x0100–0x0107，不能降低加密要求。DISCOVERY、PAIR_REQUEST、
PAIR_CHALLENGE、PAIR_REJECT 和 RECOVERY_BEACON 可未加密；PAIR_CONFIRM 与 PAIR_ACCEPT
必须是新 LMK 下的加密单播，UNPAIR 必须是当前 binding 的加密单播。所有 LINK、session
和 game 消息必须加密单播。任何不在矩阵中的组合：仅服务端对已认证、方向可归属且不是
PROTOCOL_ERROR 的圆屏入站可返回 MALFORMED_FRAME；圆屏侧关闭 session/re-LINK，方向错误、
无法归属或未认证来源不产生响应。
物理目的 MAC 也属于契约：DISCOVERY、PAIR_REQUEST 和 RECOVERY_BEACON 使用
FF:FF:FF:FF:FF:FF 广播，PAIR_REQUEST.target_server_id 使非目标服务端静默忽略；
PAIR_CHALLENGE/REJECT 是发回来源 MAC 的明文单播，其余按绑定 MAC 单播。双端始终保留
一个独立 encrypt=false 的广播 peer，它不占用六个加密玩家 peer 槽。

未分片帧 header_length=40、fragment_index=0、fragment_count=1，最大 payload 210。
完整物理长度必须严格等于 header_length + payload_length。未分片帧依赖 ESP-NOW
MAC FCS 和加密单播的 CCMP 完整性，不另加应用 CRC；分片逻辑消息必须使用下述
message_crc32。

分片帧 header_length=48，并增加：

| 偏移 | 字段 | 类型 |
| ---: | --- | --- |
| 40 | total_length | u16 LE |
| 42 | fragment_offset | u16 LE |
| 44 | message_crc32 | u32 LE |

分片最大 payload 202。CRC 使用 CRC-32/ISO-HDLC，反射多项式 0xEDB88320、
init 0xFFFFFFFF、xorout 0xFFFFFFFF，覆盖完整逻辑 payload，不覆盖信封；标准校验向量
ASCII `123456789` 的结果必须为 `0xCBF43926`。

FRAGMENTED 帧必须 header_length=48、fragment_count=2–64、fragment_index 小于 count、
message_id 非零且 total_length=1–8192；fragment_offset 与 payload_length 不能越界。
非 FRAGMENTED 帧必须 header_length=40、fragment_count=1、fragment_index=0，且不得携带
扩展字段。违反任一条件在进入 ACK 窗口前拒绝。

禁止直接发送结构体、C/C++ `bool`、指针、隐式 enum、padding 或位域。金额使用 int32，
通用字符串为 u16 length 加 UTF-8 bytes；本规格明确命名的 name/content ID 使用各自
u8 长度前缀。玩家 ID 为 1–6，格子索引为 u8，资产 ID 为
u16。可靠物理帧的 frame_seq 非零，每个可靠逻辑消息的 message_id 非零；同一逻辑消息
的所有分片共享 message_id。非零会话中，两者分别在 `(session_id, sender_epoch)` 序号域内单调
递增，每个新 session_id 的序号从 1 开始。session_id=0 使用下述显式 bootstrap_generation，
不把一整个 sender_epoch 误当为一个永不重置的窗口。零表示不参与对应的
可靠或逻辑消息序号空间。request_id 的零值只允许没有对应客户端命令的事件。序号接近
回绕时必须生成新 sender_epoch 并完整重建链路，不定义跨 0xFFFFFFFF 排序。

协议中的 server_time_ms、client_time_ms 和 deadline_ms 均为同一会话内的 u32 单调
毫秒，不代表墙上时间；未来期限必须小于 2^31 ms，并用有符号差值处理回绕。PING 携带
probe_kind、ping_id、requested_gate_generation、client_send_ms、last_applied_state_version 与
rx_free_frames/tx_free_frames 剩余槽数；
两者取 0–255，实际空闲超过 255 时饱和为 255，不是已用队列深度。PONG 原样回显
probe_kind、ping_id 与 client_send_ms，返回服务器当前 server_gate_generation，并携带 server_receive_ms、
server_send_ms、当前 state_version、gate_disposition、radio_epoch 和 Wi-Fi channel。三种 PONG
都先用 base tuple `(session_id,probe_kind,ping_id,echoed client_send_ms)` 关联唯一 outstanding；
base tuple 不匹配只能更新脱敏诊断，不得转态。base tuple 匹配时，disposition 1/2/3 可携服务器
较新的 generation，并据此进入对应的关 gate 同步/SUSPECT/re-LINK 状态；只有 disposition=0 的
SYNC_GATE/SUSPECT_RECOVERY 成功开户 proof 才额外要求 server_gate_generation 等于请求/本地
expected generation 且 phase 匹配。HEARTBEAT 的 disposition=0 永远只用于诊断，不开 gate。
权威超时始终由服务器判定。

### 13.2 ACK、重试和幂等

可靠序号空间只包含 RELIABLE=1 的帧。RELIABLE=0 的帧固定 frame_seq=0；LINK_ACK、
PING、PONG 和未加密配对提示还固定 message_id=0。ack_base 是同一 session_id 与对端
sender_epoch 下已收到的最大连续可靠序号；ack_bits bit n 表示 ack_base+1+n。

LINK_ACK 的字段固定为：message_type=0x0001、flags=0x02、header_length=40、
frame_seq=0、message_id=0、state_version=0、payload_length=0、fragment_index=0、
fragment_count=1；session_id 为被确认会话，bootstrap ACK 使用 0。ack_base/ack_bits
携带当前窗口。ACK_ONLY 是 sender_epoch 的唯一特例：header.sender_epoch 必须原样回显
被确认可靠帧的 sender_epoch，而不是 ACK 发送者自己的 epoch；发送端只用
`source MAC/binding + session_id + echoed sender_epoch` 定位 TX 窗。未知或只读墓碑 epoch
的迟到 ACK 只能更新/忽略墓碑，绝不能清当前槽。LINK_ACK 不进入接收窗口，也永远不要求 ACK。

未认证且 RELIABLE=0 的 DISCOVERY、PAIR_REQUEST、PAIR_CHALLENGE、PAIR_REJECT 和
RECOVERY_BEACON 必须令 sender_epoch=ack_base=ack_bits=0，不能携带不可信 piggyback ACK。
current PING/PONG 的 sender_epoch 使用发送方该 current session epoch，允许携带该 session 的
piggyback ACK；其他非 ACK 可靠帧使用其所属 bootstrap/current 发送 epoch。LINK_ACK 仍严格
使用上段的 echoed-epoch 特例。

只有通过信封、长度、版本、session、来源和容量检查并成功进入队列的可靠帧才能更新
ACK 窗口。重复可靠帧重新 ACK，但不再次交付。发送端重传时，除 ack_base/ack_bits 外的
完整基础头、分片扩展和 payload 必须与初发帧逐字节相同；这两个 ACK 字段是唯一可变项。

接收端不承诺保存整场长 session 的逐帧全文历史。每个 peer 的当前已认证可靠接收域只为
当前 logical message 固定保存最多 64 项 identity：
`(frame_seq u32, fragment_index u8, reserved[3]=0, immutable_sha256[32])`，实现布局每项固定 40 字节；
immutable_sha256 覆盖除 ack_base/ack_bits 外的完整基础头、分片扩展和 payload。非分片项的
fragment_index 固定 0。当前 message 内已经接受的 frame_seq 再次到达时，必须与该项的 index/
hash 完全相同；相同者重新 ACK 但不交付，冲突者不 ACK、不交付并报告 PEER_DESYNC：current
session 立即关闭/re-LINK，bootstrap 按新 incarnation 重启。同一 fragment_index 也只能映射一个
frame_seq；不同 seq 复用 index 即使 payload 相同也按冲突处理。

每个接收域另保存单调不回绕的 `closed_message_last_seq`，初值 0。由于发送端采用下文
logical-message stop-and-wait，接收端首次接受下一 message_id 就证明前一 logical message 已被
发送端 ACK-drain；它先令 closed_message_last_seq=前一消息最大 frame_seq，再清空前一最多64项
identity，然后为新消息建表。此后任何已认证且 `frame_seq<=closed_message_last_seq` 的迟到帧
只触发当前累计 ACK 后丢弃，不交付、不重建消息，也不再承诺检测其正文冲突；安全性来自它永远
不能穿过 closed fence。不能用该规则接受窗口外未来帧或改变 ack_base。

每 peer 接收重排窗口为 32 个可靠帧。乱序帧可设置 ack_bits，但必须保存在有界窗口中，
只有从 ack_base+1 起连续的 frame_seq 才按序交给逻辑解码器；窗口以外的未来帧不 ACK。
发送端对同一 peer、每个发送方向始终采用 logical-message stop-and-wait：前一可靠
message_id 的全部物理帧 ACK 清除或该域明确终止前，不得交错任何其他 RELIABLE 逻辑消息；
分片消息可以同时占用四个物理 TX 槽，只有不参与序号的 LINK_ACK 可以穿插。因而逻辑消息
按其首个 frame_seq 严格交付，不再按
message_id 建第二套顺序。状态同步时服务器先排连续 PATCH/VERSION_ADVANCE，再排被抑制
的同版本或更旧终态事件；接收端不会因先到的选择 ACK 立即判 VERSION_GAP。

session_id=0 的可靠窗口唯一键为 `(source MAC, sender_epoch, bootstrap_kind,
bootstrap_generation)`：PAIR 的 kind=1、generation=transcript_sha256[0..15]；LINK 的
kind=2、generation=client_link_nonce[16]；UNPAIR recovery 的 kind=3、generation=
unpair_id u32 LE 后补 12 个零。每个新 generation 的 frame_seq/message_id 从 1 开始。

同一 binding 在当前启动周期内，每个新 bootstrap generation 和每次 incarnation 都必须为
每个发送方向生成从未使用过的非零 CSPRNG sender_epoch；不得因为 generation 已不同就复用
epoch，掉电恢复也生成新值。旧 ACK 的 echoed epoch 因而无法命中新 seq=1 槽。非 ACK 帧的
header.sender_epoch 仍表示该帧发送方向自己的 epoch。

只有 PAIR_CONFIRM/PAIR_ACCEPT、LINK_HELLO/LINK_WELCOME 或无活动 session 时的 UNPAIR 可以
建立 session_id=0 域。首帧 payload 必须先经 LMK/proof、完整 body 和 generation 校验，才能初始化
ACK 窗口；不得用未认证头部重置它。每 peer 同时最多一个 bootstrap 可靠交易，优先级
UNPAIR > LINK > PAIR，LINK_ACK 属于当前该交易。上一个 generation 在本次交易结束后保留
30 秒的只读 nonce/hash 墓碑，重放旧首帧不得重置新窗口。因此任一端重启或发起新
client_link_nonce 时，即使另一端 sender_epoch 未变，也可安全从序号 1 重建 bootstrap。

LINK_WELCOME 的 bootstrap 窗口只能在第 14.2 节 SESSION_CONFIRMING/SESSION_ACTIVE 的明确时点
清空；不得以“已发送”或“本地已验证”替代线上确认。新 session 的 frame_seq 从 1 开始。
DISCOVERY 和 PAIR_* 的信封 state_version 固定为 0；LINK_WELCOME 的 body 携带当前权威
版本，信封 state_version 仍为 0。

LINK 的四个 epoch 角色不得混用：LINK_HELLO.header 使用该 client LINK bootstrap
incarnation 的 epoch；LINK_WELCOME.header 使用 server 响应 bootstrap epoch；WELCOME.body
的 server_sender_epoch 是服务端为 proposed current session 另抽的新 epoch。圆屏接受
WELCOME 后再抽新的 client_session_sender_epoch，首个 CLIENT_HELLO.header 用它并由服务端
固定为该 session 的客户端接收域；SERVER_HELLO 及后续服务端 current 帧 header 使用
WELCOME.body 的 server_sender_epoch。current session 存续期间两者固定，重建 LINK 必须
重抽。client epoch 无需重复放 body，它由 LMK 加密的首个 CLIENT_HELLO header 认证；
link_confirmation 只绑定 nonces+session_id，不声称包含尚未知的 client session epoch。

每 peer 可靠窗口为 4，P1/P2 语义队列各 8。重试间隔：

~~~text
80ms → 160ms → 320ms → 640ms → 640ms → 640ms
~~~

传输重试与业务重试严格分层。每个可靠物理帧 `(frame_seq,fragment_index)` 至多占一个 TX
槽；每 peer 同时至多一个活动可靠逻辑 message_id，分片消息可占窗口内最多四个不同 index
槽，同一 index 不得存在两个 frame_seq 副本。传输计时始终重发各槽原来的 session_id、
sender_epoch、frame_seq、message_id、index/offset 和 payload，只更新 ack 字段。业务层必须
等该逻辑消息全部物理片 ACK 清除或明确终止后，才可用新 frame_seq/message_id 重发同 body，
不能因其中一片 ACK 就启动业务副本。非分片消息就是一个槽。收到重复请求需要重放可靠
响应时同理；PAIR_ACCEPT、LINK_WELCOME 和 UNPAIR 响应不得并存两个未 ACK 业务副本。

frame_seq 只在一个物理 TX 槽成功取得并物化完整帧时才从该可靠域原子分配；语义队列项、
尚未物化的快照片或被 RESYNC 抑制的事件绝不预留/消费序号。分片 message_id 在其第一物理
槽物化时分配，后续片沿用；每个后续片的 frame_seq 仍在各自槽物化时分配。一旦 frame_seq
分配，该槽只能由 ACK 清除或随整个 session/incarnation 明确终止，不能取消后在同一域跳过
缺号。这样停止未物化项不会制造可靠序号洞。

传输重试耗尽会暂停该域而不跳过缺号。若仍要继续同一 bootstrap generation，发送端必须
生成新的非零 CSPRNG sender_epoch、清空旧 TX 窗并从 frame_seq/message_id=1 重发相同已
持久 body；接收端只有在首帧通过该 generation 的 LMK/proof、完整 body 和幂等校验后，才
原子 tombstone 旧 epoch 窗并建立新窗。接收端同时为该 generation 的响应方向生成新的本地
sender_epoch、从 seq/message_id=1 重放已持久响应，不能沿用旧响应方向的 N+1。任一端从
PRECONFIRM/CONFIRM_PENDING/BINDING_PENDING 或未完成 UnpairRecord 掉电恢复时，以及检测
到匹配 RECOVERY_BEACON/radio_epoch 表明对端已重启时，都必须启动这种新 bootstrap
incarnation；session_id=0 ACK 窗无需持久。新 incarnation 的首帧必须是 seq=1，旧
incarnation 只读墓碑保留 30 秒。逻辑层随后重放已持久结果而不重复副作用。普通
current session 不允许原地换 epoch，必须重做 LINK 获得新 session。这样永远不会用 N+1
跨过未确认的 N。

非 ACK 帧携带的 piggyback ack_base/ack_bits 只确认该帧所属同一 current session，或同一
bootstrap kind+generation+incarnation 的反向窗口；跨域值必须为 0。接收端用该帧已认证
domain 关联反向 sender_epoch，不得把旧 generation/incarnation 的 bits 应用到当前 TX 窗。

MAC 发送成功不等于业务完成。只有 body 中具有显式业务 request_id 的会话请求才使用
通用幂等键；PAIR、LINK、ACK、RESYNC 和 SNAPSHOT_APPLIED 各自使用其冻结状态机或本节
专用 correlation，不能把缺失的 request_id 当成 0 塞进通用缓存。通用键为：

~~~text
session_id + binding_id/source MAC + sender_epoch + message_type + request_id
~~~

玩家命令是明确例外：全部 0x1000–0x1013 把 message_type 归一为常量 PLAYER_COMMAND，
共享每 `(session_id,binding_id,sender_epoch)` 的 u32 high watermark，初值 0；新命令必须为
`high+1`，服务端缓存
`request_id → command_type + SHA-256(command_type u16 LE || expected_state_version u32 LE || 原始 body)`。
同 ID 改 command_type、expected_state_version 或正文一律按下文 REQUEST_ID_CONFLICT 会话致命
路径处理，绝不能执行两次；因此只回显
request_id 的成功终态也能唯一关联。JOIN_REQUEST 与 SESSION_RESUME 仍保留彼此独立的
message_type 域，可用相同数值 ID；但各自在
`(session_id,binding_id,sender_epoch,message_type)` 也有初值0的 u32 high watermark，新请求必须
恰为 high+1。缓存命中同 hash 重放，命中异 hash 为 REQUEST_ID_CONFLICT；`request_id<=high`
而终态槽已淘汰时返回 REPLAY_DETECTED 并强制 re-LINK，绝不能重建 PendingJoin/再次 presence
提交。三个 watermark 域达到 0xFFFFFFFF 后都必须新建 LINK，不回绕。

除上述玩家命令外，semantic hash 是 `参与裁决且不是固定常量的信封字段 || body 原始字节`
的完整 SHA-256；具体地，JOIN_REQUEST 必须以 `header.state_version u32 LE || raw body` 计算，
而 state_version 固定为 0 的 SESSION_RESUME 只计算 raw body。相同键和相同 hash 返回缓存结果。只有第 12.2
节命令终态和固定错误体含 replayed 字段，只有实际命中 TERMINAL cache 的业务重放才将其设为
1；下文 terminal 已淘汰后新构造的 REPLAY_DETECTED 明确保持 0。SEAT_ASSIGNED 和
SESSION_REVOKED 没有该字段，应使用同一业务 body 逐字节重发。相同键但不同内容走下述
REQUEST_ID_CONFLICT。全局终态缓存 64 项，每项固定槽 264 字节；每 peer 保证 8 项、最多
占 16 项，其余在 peer 间 LRU 共享。槽态固定为 FREE/RESERVED/TERMINAL：所有带业务 request_id
且可能产生终态或副作用的 current-session ingress——v1 为玩家命令、JOIN_REQUEST、
SESSION_RESUME——都必须在推进对应 high watermark、注册 PendingJoin/PendingResume 或进入
CommandBus 前取得 RESERVED terminal 槽，未完成请求小槽只保存指向它的索引；成功/业务拒绝
后原地转 TERMINAL。RESERVED 不可淘汰，只有未 pin 的 TERMINAL 可按 LRU 回收，因此后续完成
从不临时需要第65槽。每 peer 未完成请求上限4由三类请求合计共享，使用独立不可淘汰的小槽。
无槽时必须在任何注册/提交/high-watermark 推进前返回非缓存 SERVER_BUSY(RETRY_SAME_REQUEST)，
并计入既有 per-peer 8/16/global64 容量，不得为 JOIN/RESUME 另开无界旁路。
各域只与自己的 high watermark 比较。PLAYER_COMMAND 的 `request_id<=high` 且终态已淘汰时，
使用 current-session flags=0x11 的 PROTOCOL_ERROR 返回
REPLAY_DETECTED(RESYNC_NO_AUTOREPLAY)，回显该旧 request_id、replayed=0，header/body 版本取
本次串行构造时同一 Authority current_state_version；severity=ACTION_REJECTED、transaction_id=0、
retry_after_ms=0、detail_code=0。随后客户端作废 stale request、禁止自动
重试并进入完整 RESYNC。它是协议/幂等层例外，不使用 COMMAND_REJECTED。JOIN_REQUEST 或
SESSION_RESUME 的同类情况返回 PROTOCOL_ERROR(REPLAY_DETECTED,RECONNECT)，随后销毁 current
session 并 re-LINK。该 JOIN/RESUME 路径不是缓存重放，而是无槽时新构造的非缓存错误：必须
回显旧 join_request_id/resume_request_id、replayed=0、retry_policy=RECONNECT，并令
header.state_version 与 24 字节 body.current_state_version 同取本次串行构造时的 Authority
current_state_version；severity=SESSION_FATAL、transaction_id=0、retry_after_ms=0、detail_code=0。
服务端先关闭该 session 的业务 gate，可靠发送该 PROTOCOL_ERROR；收到
覆盖其 frame_seq 的 ACK 或传输重试耗尽后销毁旧 session，期间只接收该 ACK/PING 与
session_id=0 LINK_HELLO，合法 LINK_HELLO 可提前销毁旧 session。客户端收到后先 ACK、作废
对应 stale pending（不视为缓存终态、不自动重试），再 re-LINK。三类都绝不重新执行。每个 peer 的 JOIN_REQUEST domain 与 SESSION_RESUME
domain 各自至多一个 RESERVED/未终态请求，且二者对同一 binding 互斥；任一已存在时，新
`high+1` JOIN/RESUME 必须在预留槽和推进 watermark 前返回非缓存
SERVER_BUSY(RETRY_SAME_REQUEST)，不得替换旧 pending。它们仍与玩家命令共同计入每 peer
未完成请求合计上限4。服务端
每次成功 LINK 都产生新 session_id，因此任一端重启或显式重连后的旧请求均不属于
新会话。

当 current session 尚未进入第 13.5 节 RevocationDrain，且 PLAYER_COMMAND、JOIN_REQUEST 或
SESSION_RESUME 的同一完整幂等键仍有 RESERVED/TERMINAL 槽而 semantic hash 不同时，原槽、原 hash、对应 pending 和各域 high watermark 必须
逐字节保持不变，绝不能用冲突响应覆盖或终态化它。服务端关闭该 current session 业务 gate，
新构造 flags=0x11 的非缓存 PROTOCOL_ERROR(REQUEST_ID_CONFLICT,RECONNECT)：回显冲突的旧
request_id、replayed=0，header.state_version/body.current_state_version 同取本次串行构造的
Authority current_state_version；severity=SESSION_FATAL、transaction_id=0、retry_after_ms=0、
detail_code=0。它按与上文 evicted JOIN/RESUME 相同的可靠 ACK-drain、重试耗尽
或合法新 LINK 销毁旧 session 流程收敛。客户端先 ACK，作废该 session 的 pending/终态等待且不
把该错误当 COMMAND_REJECTED 业务终态，再 re-LINK；完整快照是唯一对账依据。若原 terminal 已
淘汰而只剩 watermark，服务端无法证明 hash 冲突，必须走各域上文 REPLAY_DETECTED 而不能猜测
REQUEST_ID_CONFLICT。

冲突检出本身不得改变原槽；随后 session teardown 对原 RESERVED 请求仍使用既有规则：已经进入
CommandBus 的 PLAYER_COMMAND 继续串行完成并原槽终态化，结果由新 session 快照对账；尚未产生
副作用的 JOIN/RESUME pending 按本节 context-invalid 规则释放，已 COMMIT 的则先原槽终态化。

P7 必须分别在原 PLAYER_COMMAND 槽为 RESERVED 和 TERMINAL 时注入异 hash，断言冲突检出点原
槽/hash/pending 完全不变、wire carrier 为上述 PROTOCOL_ERROR，随后按前段正常收敛；
JOIN/RESUME 各至少一条也必须证明冲突响应不覆盖原槽。

P7 必须为上述 PLAYER_COMMAND evicted-ID 路径断言 message_type=PROTOCOL_ERROR、flags=0x11、
error_code=0x010A、retry_policy=RESYNC_NO_AUTOREPLAY、旧 request_id、replayed=0 和逐值相等的
header/body current version，并明确断言不得生成 COMMAND_REJECTED 或重新进入 pending。
P7 还必须分别覆盖 JOIN_REQUEST 与 SESSION_RESUME 的 evicted-ID：断言同一 24 字节字段、
RECONNECT、可靠 ACK-drain/重试耗尽销毁路径，以及客户端 stale pending 作废/re-LINK。

每个 RESERVED/TERMINAL 槽都与 body 一起固定保存 `terminal_state_version u32`。成功终态取
产生它的 COMMIT.committed_version；未提交的业务拒绝、JOIN 超时、REISSUE、已 ONLINE 的合法
RESUME 与 current-session PROTOCOL_ERROR 取首次串行裁决时的 Authority state_version。
current session 上任何缓存终态的 header.state_version 永远等于该值；COMMAND_REJECTED 和
使用固定错误体的 PROTOCOL_ERROR 还要求 body.current_state_version 与它逐值相等，直接
SESSION_REVOKED 要求其 body.current_state_version 也相等。传输层重试除 ack_base/ack_bits 外
逐字节不变；同一业务 request 的后续缓存重放仍使用原 terminal_state_version，不能采样后来
版本，只有含 replayed 字段的成功终态/固定错误体把 replayed 从 0 改为 1。SEAT_ASSIGNED 与
SESSION_REVOKED 没有 replayed 字段，重放时 header 与 body 都逐字节保持首次终态。bootstrap
PROTOCOL_ERROR 的 header.state_version 仍按矩阵固定为 0，不把 current-session 规则反套到
bootstrap。P7 必须覆盖状态继续前进后重放旧 COMMAND_REJECTED：header 与错误体仍为原裁决
版本且只改变 replayed。

### 13.3 分片与快照

- 最大逻辑消息 8,192 字节。
- 最大 64 片。
- 单 peer 同时重组一个消息。
- 两秒无分片进展即丢弃。
- 绝对重组超时 15 秒。
- 对所有 FRAGMENTED 类型（包括 SNAPSHOT 和 PATCH），protocol major/minor、message_type、
  flags、session_id、sender_epoch、message_id、state_version、fragment_count、total_length 和
  message_crc32 必须逐片相同。不同 index 的初发片可有不同 frame_seq、ack_base/ack_bits、
  fragment_index、fragment_offset、payload_length 和 payload；同一片重传只允许更新
  ack_base/ack_bits，frame_seq/index/offset/length/payload 必须不变。ack 字段不参与
  reassembly identity 或 message_crc32；FRAGMENTED header_length 固定 48。
- 同一 fragment_index 的重复只有在 offset、length 和 payload 逐字节相同时才接受；冲突
  重复、不同 index 的任何部分重叠或越界都拒绝。每片 fragment payload_length 必须为
  1–202；只有恰好收齐 `0..fragment_count-1` 每个 index 的一份已验证片（不同 index 数等于
  count）后才允许完成。此时按 offset 排序的 ranges 必须无缝精确覆盖
  `[0,total_length)`，无洞、无尾随，再校验完整 message_crc32；乱序本身可接受。
- 首片分配 staging 前必须验证 `fragment_count <= total_length <= 202*fragment_count`，否则
  FRAGMENT_INVALID，不占重组槽。
- 快照必须完成 CRC、TLV、数组数量和引用验证后原子应用。
- 重连始终发送完整快照；补丁只用于持续在线。

一旦发生两秒无进展、15 秒绝对超时、已建 staging 后的片冲突/范围冲突或完整
message_crc32 失败，接收端必须丢弃 staging，并在该 `(session_id,sender_epoch,message_id)`
下保存唯一 active cancelled tombstone；当前消息最多64项 identity 表继续保留。其后属于该
message_id 的片仍必须帮助发送端 drain：已存在 frame_seq/index 的项按当前表逐项比对，相同者
重新 ACK；此前未到达、但在窗口内且 index 未被占用的新片加入同一有界 identity 表并 ACK。
两者都绝不重建 staging 或交付，任何 frame_seq/index/hash 冲突仍按 PEER_DESYNC。
stop-and-wait 发送端 ACK-drain 后首次发送下一 message_id；接收端接受
其首帧时按上段推进 closed_message_last_seq、清空 identity 与 cancelled tombstone。更晚的旧片
因 `frame_seq<=closed_message_last_seq` 只获累计 ACK 后丢弃，不重建、不做无界正文冲突检测。
STATE_SNAPSHOT 与 STATE_PATCH 的重组超时都发送可靠
RESYNC_REQUEST reason 8；snapshot message CRC 用 reason 2、schema/TLV 用 reason 3；patch
message CRC 或 schema 用 reason 4。任何类型的片字段冲突、range 重叠/越界或同 frame_seq
冲突都不尝试在可疑序号流上 resync：圆屏关闭 current session 并重做 LINK；服务端对已认证
入站可先发 FRAGMENT_INVALID/PEER_DESYNC 再关闭。服务端接收其他已认证、方向合法的圆屏
分片类型发生超时/CRC 时，可发 PROTOCOL_ERROR 的 REASSEMBLY_TIMEOUT/CRC_MISMATCH 后
关闭 current session；圆屏接收服务端其他分片失败时只 close/re-LINK，不发送错误。

服务端收到任一合法 RESYNC_REQUEST 先关闭该 peer 的玩家命令 gate。该消息不用通用
request_id 缓存，其专用身份是 `(session_id, client_sender_epoch, reliable message_id)`，semantic
hash 为完整 body；相同身份/正文只重新 ACK/coalesce，冲突副本按 PEER_DESYNC。session 内维护
唯一 runtime `sync_generation`：新的合法 RESYNC 可用更新后的 observed/last_message_id 取代旧
计划，但只能合并为一份“捕获最新权威版本的完整快照”，不得并发创建多份 snapshot 或重发
PATCH。关 gate 的同一临界区还必须把该 peer 当前 session 的全部 RESERVED/TERMINAL cache
槽（明确包括玩家命令、JOIN_REQUEST 与 SESSION_RESUME）pin 住；这就是有界 post-sync replay set，不复制 body、不含无槽的瞬时 SERVER_BUSY 或
非终态 COMMAND_ACCEPTED。已接受的至多四个 pending 继续串行完成并在各自 RESERVED 槽原地
转 TERMINAL；服务端必须等它们全部终态后才捕获最新快照，所以结果必已反映在 N 中。

服务端立即停止物化旧 sync/事件链中所有尚未占物理槽的项；已占槽的任何旧
server→client 可靠逻辑消息——无论分片快照/补丁，还是未分片 PATCH、SYNC_COMPLETE、领域
事件或三域请求终态——都继续按原 frame_seq 重传。客户端从发出 RESYNC 起进入 SYNCING，对这些
旧帧照常 ACK-drain 但不交付；分片失败项还使用 cancelled tombstone。被抑制的命令终态继续
留在上述 pin 集合。即使首个 RESYNC 丢失、某终态先被客户端 ACK-drain，它仍是刚触碰 LRU 的
current-session TERMINAL；每 peer 保底八槽大于最大四个 pending，重传到达时仍会被 pin。
全部旧 TX 槽清空、该 peer pending=0 后，服务端才以新的 message_id
生成该 generation 的最新完整快照。传输重试耗尽时销毁 current session 并重做 LINK，绝不能
跳过缺失 frame_seq 后在同一可靠域发送新快照。

STATE_SNAPSHOT 的逻辑 payload 固定为：

~~~text
body_schema             u8 = 1
snapshot_schema         u8 = 1
room_id                 u64 LE
snapshot_state_version  u32 LE
server_tick_ms          u32 LE
snapshot_sections_crc32 u32 LE
section_count           u8，v1 固定为 11
sections                section_count 个 TLV section
~~~

snapshot_sections_crc32 使用与 message_crc32 完全相同的 CRC-32/ISO-HDLC 参数和标准向量，
覆盖 section_count 字节到最后 section 末尾，独立于分片层 message_crc32。
snapshot_state_version 必须等于信封 state_version。

生成快照时服务器原子捕获 snapshot_message_id、snapshot_state_version=N、total_length、
CRC 和 ConsoleView(N)。同一快照及其重传必须完整遵守本节上方通用 FRAGMENTED 逐片不变量、
range 覆盖和“重传只能更新 ack_base/ack_bits”规则，不再定义任何快照专用例外。

快照传输期间，客户端只写 staging buffer，不改变已应用状态，也不应用后续补丁。服务端
继续裁决，但为该 peer 从 N+1 起缓存最多 32 个版本、单 peer 最多 8KiB 的连续个性化
STATE_PATCH/VERSION_ADVANCE；六 peer 共享 24KiB pool，每 peer 保证 2KiB，耗尽即转新
快照。在确认前不发送。客户端完整验证后原子应用 N，并发送：

~~~text
SNAPSHOT_APPLIED
body_schema             u8 = 1
snapshot_message_id     u32 LE
snapshot_state_version  u32 LE
snapshot_crc32          u32 LE
~~~

服务端以 `(session_id,snapshot_message_id,snapshot_state_version,snapshot_crc32)` 作为唯一
sync-generation correlation；首次验证 SNAPSHOT_APPLIED 时原子捕获且永久冻结该 generation
的 `catchup_target_version=M`。同一 correlation 的业务重发只能重新 ACK，并继续或可靠重放
同一条已冻结 N+1…M 链与同一 SYNC_COMPLETE，绝不能再次捕获更晚的 M、建立第二条链或产生
第二个 SYNC_COMPLETE；只有新完整快照或新的合法 RESYNC generation 才明确作废旧 generation。
若 N+1…M 的个性化
缓存链有缺口、溢出或无法连续，则丢弃旧链并生成最新完整快照，绝不发送不连续
补丁。链有效时，服务端在同一可靠序号流中依次排入 N+1…M 的 STATE_PATCH/
VERSION_ADVANCE，紧接一条 SYNC_COMPLETE，其 body 回显原 snapshot_message_id、N、M 和本次
sync 开始时已分配的 gate_generation。
若 M=N，直接排入 SYNC_COMPLETE。

捕获 M 后新提交的 M+1 以上更新依然可继续生成，但必须排在 SYNC_COMPLETE 之后的正常
消息流；不得将它们插入已冻结的 N+1…M 链。客户端顺序应用到 M，只有收到匹配的
SYNC_COMPLETE 且本地 last_applied_state_version=M 时才从 SYNCING 转
SYNCED_WAIT_GATE，而不是 ONLINE；任一字段、版本或 gate_generation 不符即发
RESYNC_REQUEST。进入该子态立即分配一个新非零 ping_id，发送
PING(probe_kind=SYNC_GATE,requested_gate_generation=SYNC_COMPLETE 所带值,
last_applied_state_version>=M)，未收到匹配 PONG 时每 1 秒
重发同一 ping_id/body。此时必须隐藏或禁用 AVAILABLE_ACTIONS，且不得发送任何
0x1000–0x1013。服务端只有在 RuntimePingGate.phase=SYNC_PROOF_PENDING、PING generation 等于
当前 generation、sync correlation 仍匹配且可靠链连续时才裁决：客户端版本等于当前 Authority
版本才原子开 gate 并回 gate_disposition=OPEN_OR_NO_ACTION；客户端版本较低则保持关 gate，回
SYNC_GATE_REQUIRED，客户端继续应用已排队 patch（缺口则 RESYNC），完成后用新 ping_id 再
证明；客户端版本高于服务端则 PEER_DESYNC、销毁 current session/re-LINK，绝不能用快照把
客户端静默“回滚”。客户端只有在 PONG 的
`(session_id,probe_kind,ping_id,requested_gate_generation,echoed client_send_ms)` 精确匹配当前唯一
outstanding gate tuple、gate_disposition=OPEN_OR_NO_ACTION 且 PONG.current_state_version 等于
当下本地 last_applied_state_version，且 PONG.server_gate_generation 等于 tuple 中的
requested_gate_generation 时才转 ONLINE/启用动作。不能把一次旧 PONG 当永久 gate。
新 session、新快照或更高优先级重同步会使旧
staging 和旧 SYNC_COMPLETE 立即失效。P7 golden 必须覆盖首次 PONG 丢失、PONG 显示 M+1、
迟到旧 PING/PONG 恰好版本相等、客户端版本大于服务端，以及 gate 证明前点击命令被客户端
本地阻止；非 outstanding
PONG 只更新脱敏诊断，绝不打开 UI/gate。

从快照开始到 SYNC_COMPLETE 被客户端可靠收到，服务端还必须抑制该 peer 的所有版本化
P1/P2 领域事件。仅当快照属于原 session_id 未变的就地 RESYNC 时，必须在 SYNC_COMPLETE 后
可靠重放 post-sync replay set 中全部缓存 TERMINAL 响应，而不只玩家命令：带 replayed 字段的
成功终态、COMMAND_REJECTED 和缓存型 PROTOCOL_ERROR 设 replayed=1；没有该字段的
SEAT_ASSIGNED 保持首次 header/body。排序键严格为
`(request_domain u8, terminal_message_type u16, request_id u32)` 升序，其中 request_domain 的线序
值固定为 0 PLAYER_COMMAND、1 JOIN_REQUEST、2 SESSION_RESUME，不得使用 C++ enum 声明顺序。
已经进入 session-fatal ACK-drain/销毁路径的非缓存 PROTOCOL_ERROR 不参与就地 RESYNC set；
SESSION_REVOKED 的七种 reason 全部 session-fatal，因此包括直接缓存终态在内一律不进入该
set，而按第 13.5 节 SESSION_REVOKED 唯一矩阵完成自身 drain/销毁。已清 pending 的客户端按
domain+request_id 幂等忽略。全部 replay 帧 ACK
后才 unpin，重复 RESYNC 复用同一集合。新 session 同步不跨 session 重放旧终态；完整视图是
唯一对账依据。其他动画/
公开提示可丢弃，因为完整视图已包含结果。
客户端处于 SYNCING 或 SYNCED_WAIT_GATE 时仍 ACK 到达的旧可靠事件以停止重传，但不交付
过期 UI；两个子态都不得发送玩家命令。

P7 必须覆盖 JOIN_REQUEST 的缓存 PROTOCOL_ERROR 在旧 TX 帧被 SYNCING 客户端 ACK-drain 而未
交付后，仍以同一 terminal_state_version、replayed=1 排在 SYNC_COMPLETE 后重放，客户端清除
对应 JOIN pending；并用三域同数 request_id 断言上述复合排序键。

STATE_SNAPSHOT v1 始终使用 FRAGMENTED；SNAPSHOT_APPLIED.snapshot_crc32 必须等于该
逻辑消息分片扩展中的 message_crc32。它与 payload 内的 snapshot_sections_crc32 是两个
独立校验值，任一不匹配都不得应用。

STATE_PATCH 只做整个个性化 section 的替换，不使用未冻结的细粒度路径：

~~~text
body_schema    u8 = 1
patch_schema   u8 = 1
room_id        u64 LE
from_version   u32 LE
to_version     u32 LE，必须等于 from_version + 1
server_tick_ms u32 LE
section_count  u8，1–11
sections       section_count 个替换 TLV
~~~

VERSION_ADVANCE 为 body_schema u8=1、room_id u64、from_version u32、to_version u32、
server_tick_ms u32，共 21 字节，且必须只前进一版。PATCH 对当前视图无变化时必须改发
VERSION_ADVANCE。两者信封 state_version 等于 to_version。服务端不能把传输中的快照
分片信封版本改为较新值。

客户端 last_applied_state_version 只能由 STATE_SNAPSHOT、STATE_PATCH 或
VERSION_ADVANCE 推进，普通领域事件的信封版本只是因果标签。P1 命令终态可以先于同版本
P2 patch 到达并清除对应 pending，但任何依赖新状态的 UI cue 要按版本暂存，直到本地视图
达到该版本；合法动作只能读取更新后的 AVAILABLE_ACTIONS。事件本身不得填补 patch 版本
缺口。

快照 TLV section：

~~~text
01 CORE
02 SELF_PLAYER
03 PUBLIC_PLAYERS
04 ASSETS
05 PRIVATE_CARDS
06 AVAILABLE_ACTIONS
07 OBLIGATIONS
08 TRADES
09 DEADLINES
0A CONTENT_REFS
0B ACTIVE_WORKFLOW
~~~

section 头为 section_id u8、flags u8、length u16。flags bit 0 为 critical，bit 1–7 必须
为零；未知可选 section 跳过，未知 critical section 拒绝。快照中的 section 按 ID 升序
且不得重复；补丁只包含改变的 section，也按 ID 升序。静态地图只引用 map_id、revision
和 content_hash。

### 13.4 六 peer 调度

驱动层任意时刻只允许一个 esp_now_send 在途。每 peer 独立维护序号、ACK、重试、
心跳和队列。优先级：

1. P0：ACK、到期重试、配对和恢复。
2. P1：命令结果、付款和当前回合强制事件。
3. P2：状态补丁、公开事件和心跳。
4. P3：快照分片、诊断和背景内容。

同级使用 Deficit Round Robin，每 peer 每轮 250-byte quantum，每次访问最多发送一帧。
ACK 使用绝对 20ms deadline，已到期 ACK 可打断任何公平性让出。除此之外 P0 最多连续
八帧，之后让出一次 P1/P2。P3 等待 500ms 后提升一级、等待 2 秒后必须获得一次发送
机会，但不得越过已到期 ACK。坏 peer 等待 ACK 或重试时不能阻塞其他 peer。

调度器必须 work-conserving：只有其他 peer 也有待发流量时，单 peer 重试才限制为最近
一秒物理帧的 25%；没有竞争者时可使用空闲带宽。达到本消息重试上限才标记该链路
SUSPECT/OFFLINE，不能仅因公平配额用尽而离线。

资源上限：

- 全局 RX ring 32 帧。
- 全局 TX frame pool 48 帧。
- 其中至少六个物理 buffer 专供 P0/ACK，P1–P3 不得占用。
- 同时只生成一个玩家快照。
- 每个快照分片发送后重新进入调度，不能连续独占射频。

每 peer 的 P1/P2 上限是语义消息项，不预占物理 frame buffer；调度时才物化一个帧。
全局池满时按固定顺序回收：仅回收尚未原子提交物理 TX 槽、因此没有 frame_seq/message_id
的 P3 semantic/staged 诊断或可再生成快照片段，随后合并同样尚未物化且仍能保持版本连续的
P2；若已物化快照片因独立 payload backing store 可释放，TX slot 与原 seq/index/offset/length/
payload hash 身份仍不可取消，重传前必须逐字节再生成相同 payload。若仍不足则把该 peer
标记 DESYNC、停止继续物化状态链并按第 13.3 节 ACK-drain 后计划完整快照。
绝不淘汰 P0、命令终态、或已持久化但尚未回应的结果。

延迟验收：

- ACK 计划发送不超过 20ms。
- 当前玩家正常响应不超过 100ms；六 peer 负载不超过 250ms。
- 公开状态送达六台圆屏不超过 500ms。
- 单台 8KiB 快照良好链路不超过 2 秒。
- 六台初始快照良好链路不超过 10 秒。

### 13.5 链路、配对和会话 body schema

除 STATE_SNAPSHOT/STATE_PATCH 外，本节消息不得分片且 payload≤210。全部多字节整数小端；
bool 使用 u8 的 0/1。名字编码为 name_length u8 + UTF-8 bytes，长度 1–48，禁止 NUL、
控制字符和无效 UTF-8。PLAYER_NONE=0、POSITION_UNKNOWN=0xFF、ASSET_NONE=0。

链路消息字段按线序冻结为：

| 消息 | body 字段；未列即无 payload |
| --- | --- |
| LINK_ACK | 无 |
| PING | schema u8=1；probe_kind u8；ping_id u32 非零；requested_gate_generation u32 非零；client_send_ms u32；last_applied_state_version u32；rx_free_frames u8；tx_free_frames u8 |
| PONG | schema；回显 probe_kind；ping_id；server_gate_generation u32 非零；回显 client_send_ms；server_receive_ms u32；server_send_ms u32；current_state_version u32；gate_disposition u8；radio_epoch u32；current_channel u8 1–14 |
| LINK_HELLO | schema；binding_id u64；device_id u64；last_session_id u32；last_room_id u64；last_state_version u32；client_link_nonce[16]；protocol_major u8；min_minor u8；max_minor u8；capabilities u32 |
| LINK_WELCOME | schema；binding_id u64；server_id u64；new_session_id u32；room_id u64；current_state_version u32；server_tick_ms u32；channel u8；resume_disposition u8；回显 client_link_nonce[16]；server_link_nonce[16]；selected_major u8；selected_minor u8；server_capabilities u32；selected_capabilities u32；required_capabilities u32；radio_epoch u32；server_sender_epoch u32 |
| PROTOCOL_ERROR | 第 15 节固定错误体 |

probe_kind：0 HEARTBEAT、1 SYNC_GATE、2 SUSPECT_RECOVERY；其他值在 ACK 前拒绝。每个 current
session 的圆屏持有 `next_ping_id u32`，新 session 从 1 开始；普通 heartbeat、sync gate proof 与
SUSPECT recovery proof 每建立一个新 probe 都分配当前 1..0xFFFFFFFE 后严格加一，同一 session 绝不复用。0 与
0xFFFFFFFF 保留；达到 sentinel 前必须关闭 current session 并重新 LINK，不回绕。任一 probe 的
重传保持相同 `probe_kind、ping_id、requested_gate_generation、client_send_ms,last_applied_state_version、rx/tx_free_frames` 完整 body；
新 probe 才消费下一 ID。同一时刻至多一个本地 outstanding PING；进入 SYNCING 会作废此前
heartbeat outstanding，进入 SYNCED_WAIT_GATE 再建立新的 gate tuple。

gate_disposition：0 OPEN_OR_NO_ACTION、1 SYNC_GATE_REQUIRED、2 SUSPECT_RECOVERY_REQUIRED、
3 RELINK_REQUIRED；其他值拒绝。值 0 对 HEARTBEAT 只表示“不改变 gate”，只有在
probe_kind=SYNC_GATE/SUSPECT_RECOVERY 且下述 proof 被接受时才表示服务器 gate 已打开；客户端
绝不能因普通 HEARTBEAT 的值 0 打开 UI。值 1 的动作由所回显 probe_kind 唯一决定：对
SYNC_GATE，客户端保持/进入 SYNCING，优先应用当前 sync generation 已有连续 patch，链缺失才
发 RESYNC_REQUEST，追平后仍用 SYNC_GATE；对 SUSPECT_RECOVERY，服务端已按下文启动新的完整
sync，客户端必须丢弃旧 patch 计划并等待 PONG.server_gate_generation 对应的完整快照与
SYNC_COMPLETE，不能提前发 kind1。值 2 令客户端立即进入 SUSPECT、隐藏动作并用新 ping_id 和服务器返回的
server_gate_generation
发 SUSPECT_RECOVERY；值 3 令客户端销毁 current session/re-LINK。

圆屏每个 current session 还保存 `highest_server_gate_generation`，接受 WELCOME 时为 1，严格按
无回绕 u32 比较。认证且匹配当前 sync correlation 的 SYNC_COMPLETE.gate_generation 也必须
不小于该值并先更新 highest；较小值是 stale sync、不得进入 WAIT_GATE。base tuple 匹配的 PONG
若 server_gate_generation 小于该值，只能更新诊断；
大于该值时先更新 highest，并且只允许 disposition 1/2/3 把状态收紧，绝不能用 disposition=0
开 UI；只有等于 highest、同时等于当前 proof.requested_gate_generation 的 kind1/kind2
disposition=0 才可能开户。由此已观察新 generation 后，任何旧 PONG 都不能复开客户端 gate。

服务端每个 current session 固定保存
`RuntimePingGate(gate_phase,gate_generation,last_seen_ping_id,last_ping_hash,
sync_generation)`。gate_phase 仅取 OPEN、SYNCING、SYNC_PROOF_PENDING、SUSPECT_PROOF_PENDING、
RELINK_REQUIRED。gate_generation 新 session 从 1 开始，圆屏接受 WELCOME 时也把本地已知值
初始化为 1。每个新 sync generation（无论由 RESYNC 还是完整快照建立，整个 sync 只增一次）、
每次新的 SUSPECT episode、OFFLINE 或 CONTEXT_REVOKE 都在关闭 gate 的同一临界区递增一次，0 与
0xFFFFFFFF 保留、绝不回绕，耗尽即 RELINK_REQUIRED。SYNC_COMPLETE 必须携带建立该
SYNC_PROOF_PENDING 的 generation。

`last_ping_hash = SHA-256(raw PING body)`，精确覆盖本节表中从 schema 到 tx_free_frames 的全部
body 字节。source MAC/binding、session_id 与 sender_epoch 必须先在外层 current-session 门禁匹配；
header.ack_base/ack_bits 不参与该 hash。因此相同 ping_id/raw body 的合法重传可以更新 piggyback
ACK 而不触发冲突，只有同 ID 改 body 才是下段 PEER_DESYNC。

对已通过 current-session 认证的 PING，服务端只接受 `ping_id>last_seen_ping_id` 建立新 probe；
PING 不可靠，允许中间 ID 丢失而不要求恰为 +1。同 ID/hash 相同绝不再次应用任何 gate 转换，
但必须按当前 gate_phase/gate_generation 重建 status-only PONG；若 phase/generation 未变，除
server_receive_ms/server_send_ms 外等价重放原 disposition，若已变化则返回当前
server_gate_generation 与当前所需 disposition，不能重放过时 OPEN。响应更新不得反过来改变
last_ping identity。同 ID/hash 不同为 PEER_DESYNC 并 close/re-LINK；更低 ID 是 stale，静默
丢弃且不得开 gate。新的 RESYNC/快照/SUSPECT/OFFLINE/context revoke 会改变 gate_phase/
generation，使此前尚未到达的更高旧 PING 即使版本数字碰巧相等，也因 generation/phase 不匹配
而不能开 gate。

新完整同步开始时 phase=SYNCING；服务端把该 generation 的 SYNC_COMPLETE 排入同一可靠链时才
变为 SYNC_PROOF_PENDING。HEARTBEAT 在 OPEN 返回 disposition=0 且不改 gate；在 SYNCING/
SYNC_PROOF_PENDING 返回 1；在 SUSPECT_PROOF_PENDING 返回 2；在 RELINK_REQUIRED 返回 3 后
停止 current PONG。SYNC_GATE 只有在 SYNC_PROOF_PENDING 且 generation 精确匹配时才按第 13.3
节裁决，其他 phase/generation 只返回其当前所需 disposition，绝不能开 gate。

resume_disposition：0 PAIRED_UNSEATED、1 MAY_RESUME_SEAT、2 ROOM_NO_LONGER_EXISTS、
3 BINDING_REVOKED、4 SERVER_RECOVERING。LINK_HELLO 固定 56 字节，LINK_WELCOME 固定
93 字节。selected_capabilities 必须是 LINK_HELLO.capabilities 与 server_capabilities 交集的子集，
且必须覆盖 required_capabilities。双方只在 client nonce 回显正确且 WELCOME 通过 LMK
验证时接受 proposed session。

配对消息字段按线序冻结为：

| 消息 | body 字段 |
| --- | --- |
| DISCOVERY | schema u8=1；profile u8=1；server_id u64；radio_epoch u32；pairing_window_id u32；major/min_minor/max_minor 各 u8；channel u8；server_capabilities u32；remaining_ms u16 0–60000；beacon_sequence u16 |
| PAIR_REQUEST | schema；profile；window_id u32；target_server_id u64；device_id u64；console_nonce[16]；console_major/min_minor/max_minor；console_capabilities u32；max_frame_length u16=250；max_message_length u16=8192；firmware major/minor/patch 各 u8；firmware_build_id u32 |
| PAIR_CHALLENGE | schema；profile；window_id；server_id u64；device_id u64；server_nonce[16]；回显 console_nonce[16]；server major/min/max；console major/min/max；selected_major/minor；server/console/selected capabilities 各 u32；max_frame_length u16=250；max_message_length u16=8192；server_proof[16]；attempt_timeout_ms u32=10000 |
| PAIR_CONFIRM | schema；profile；window_id；server_id u64；device_id u64；server_nonce[16]；console_nonce[16]；selected_major/minor；selected_capabilities u32；client_proof[16] |
| PAIR_ACCEPT | schema；profile；window_id；binding_id u64；server_id u64；device_id u64；key_version u16=1；selected_major/minor；selected_capabilities u32；channel u8；transcript_sha256[32] |
| PAIR_REJECT | schema；profile；window_id；server_id u64；device_id u64；console_nonce[16]；error_code u16；retry_after_ms u32 |
| UNPAIR | schema；unpair_id u32 非零；binding_id u64；reason u8；confirmation u8 |
| RECOVERY_BEACON | schema；server_id u64；radio_epoch u32；major u8；minor u8；channel u8；beacon_sequence u16 |

UNPAIR reason：1 USER_REQUEST、2 ADMIN_REPLACED_DEVICE、3 KEY_ROTATION、4 FACTORY_RESET、
5 SECURITY_REVOKED；confirmation 0 REQUEST、1 ACKNOWLEDGED、2 FINAL。

服务端 NVS 只存在 `BindingSlot[6]` 六个固定 key；slot 状态为 EMPTY、PAIR_PENDING、ACTIVE、
UNPAIR_PENDING、REVOKED_TOMBSTONE 或 FORCE_REVOKE_PENDING，BindingRecord 与
UnpairRecord 是同槽互斥 union，
禁止按 binding 动态创建 key。除 EMPTY 外全部计入六槽容量；六槽满时 PAIR_REQUEST 只能
得到 PAIR_REJECT(PEER_TABLE_FULL)，不得自动淘汰或在配对过程中临时需要第七个加密 peer。
未加密广播 peer 独立，不占这六槽。
每槽固定保存 `slot_id u8 0..5` 与 `slot_generation u32`；0 和 0xFFFFFFFF 保留。首次及
以后每次 EMPTY→PAIR_PENDING 都先把 generation 加一，配对/unpair 的正常 substage 转换
不增加；FORCE_FORGET commit 在任何删除前再加一并写 FORCE_REVOKE_PENDING，最终 EMPTY
保留该新 generation。达到 0xFFFFFFFE 的槽不得再配对或强制清除，只能 factory reset，绝不
回绕。所有管理操作必须精确回显 expected_slot_generation。
服务端文中的 PRECONFIRM 和 BINDING_PENDING 不是另一套外层状态：它们分别是
PAIR_PENDING 的 `pair_substage=1 SERVER_PRECONFIRM / 2 SERVER_BINDING_PENDING`；0 保留，
其他值拒绝。CONFIRM_PENDING 是玩家屏本地记录枚举，不出现在服务端 BindingSlot。每槽
PAIR_PENDING 固定包含恢复该 substage 所需 transcript/MAC/LMK，窗口同时最多一个，因此
不会产生第七个服务端 slot。
FORCE_REVOKE_PENDING 固定保留旧 state、binding_id（可为 0）、device_id、MAC、LMK、room_id、
seat_id、player_id、old assignment_version、release_state、peer_delete_state 和
force_request_id，直到最终 EMPTY 原子写；这些字段只用于完成不可回退清理，绝不恢复授权。

UNPAIR 的 NVS `UnpairRecord` 固定保存 schema=1、slot_id、slot_generation、非零 CSPRNG
unpair_id、binding_id、device_id、reason、
initiator_mac[6]、peer_mac[6]、selected_major/minor、key_version、LMK[16]（或同一加密 NVS
binding key slot 的稳定引用）、role（0 INITIATOR / 1 RESPONDER）、stage、retry_count 和
last_confirmation，以及旧 room_id、seat_id、player_id、assignment_version、
authority_release_state。tombstone 继续保留这些 occupant/释放字段，直到 FORCE_FORGET。
恢复域不得依赖已删除的活动 BindingRecord；上述字段足以重建 codec 头、
加密 peer 和 tombstone。stage 只有：

`authority_release_state`：0 NOT_APPLICABLE_OR_UNSEATED、1 PREPARED、2 COMMITTED。圆屏端
和服务端未入座 binding 固定为 0；服务端已入座 binding 必须先 1，座位 COMMIT 与 NVS
promotion 完成后才为 2，其他值拒绝。

~~~text
0 INIT_REQUEST_PENDING
1 INIT_FINAL_PENDING
2 RESP_ACK_WAIT_FINAL
3 REVOKED_TOMBSTONE
~~~

每个转换都必须先原子持久化新 stage，再发对应帧：

1. 发起端持久化 role=INITIATOR/stage=INIT_REQUEST_PENDING，立即禁止该 binding 的新游戏命令，
   发 REQUEST。传输 LINK_ACK 只清物理槽；未收到 ACKNOWLEDGED 时按 1s、2s、4s、
   之后每 5s 触发第 13.2 节两层重试：旧物理槽未 ACK 则重发原序号，已 ACK 才用新
   frame_seq/message_id 发送同 body。
2. 接收端收到 REQUEST 后持久化 role=RESPONDER/stage=RESP_ACK_WAIT_FINAL，立即禁止该
   binding 的所有游戏/session 消息。圆屏端或服务端未入座 binding 可随即发 ACKNOWLEDGED；
   服务端已入座 binding 必须先按第 17.1 节完成 PREPARED→SEAT_UNBOUND COMMIT→COMMITTED，
   之后才发 ACKNOWLEDGED。重启或重复 REQUEST 在 release 完成前只恢复 release，完成后
   重发 ACKNOWLEDGED，不创建新 unpair_id。
3. 发起端收到 ACKNOWLEDGED 后持久化 stage=INIT_FINAL_PENDING，发 FINAL。重启、重复
   ACKNOWLEDGED 或重试耗尽后链路再恢复，都必须重发 FINAL。
4. 接收端收到 FINAL 后持久化 stage=REVOKED_TOMBSTONE 再发传输 LINK_ACK；发起端
   收到覆盖 FINAL frame_seq 的 LINK_ACK 后也持久化 REVOKED_TOMBSTONE。

同一 binding 双端同时发起不同 unpair_id 时，以 `(unpair_id u32, initiator_mac 6 字节
字典序)` 较小者为唯一胜者；双方从物理来源 MAC 与各自已持久记录得到相同排序。收到败者
REQUEST 的胜者仍必须把该合法可靠帧放入当前接收窗口、推进 ack_base 并发送传输层
LINK_ACK，以免阻塞败者后续 frame_seq；它只是不为败者 unpair_id 发送业务层
UNPAIR(ACKNOWLEDGED)，并立即重发自己的胜者 REQUEST。session_id=0 的不同 UNPAIR
generation 也先完成各自合法帧的传输 ACK，再做同一业务仲裁。收到胜者 REQUEST 的败者
必须先原子把本地记录替换为胜者的 id/reason/initiator、
role=RESPONDER、stage=RESP_ACK_WAIT_FINAL，再发 ACKNOWLEDGED；旧败者 ID 永久无效。已进入
RESP_ACK_WAIT_FINAL/INIT_FINAL_PENDING 后只接受该胜者 ID 的同阶段重复帧，其他 ID 不得
改写状态。即使随机 ID 相同，MAC 次序也保证唯一结果。

若服务端已因本地败者 ID 写入 release PREPARED/排队 SEAT_UNBOUND，切换胜者记录时必须
保留 occupant 与 release state。尚未 dispatch 的败者 release 作废，按胜者 unpair_id 重新挂接；
dispatch 在持 BindingRegistry 锁后还要复核当前 record.unpair_id，败者不得提交。若座位已由
败者请求成功解绑，则不再产生第二个 SEAT_UNBOUND，只把胜者 record 直接标为 release
COMMITTED，并在此后发胜者 ACKNOWLEDGED。由此并发仲裁不产生两个座位终态。

INIT_REQUEST_PENDING、INIT_FINAL_PENDING 和 RESP_ACK_WAIT_FINAL 均不得因超时或重启自动
删除；它们按 role/stage 唯一决定待重发阶段。双方进入 REVOKED_TOMBSTONE 后，旧 LMK、
选定版本和对端 MAC 仍保存于同一 BindingSlot，但权限严格缩为同一
binding_id/unpair_id 的重复 UNPAIR 与对应 LINK_ACK；所有 LINK、座位、快照和游戏消息均
拒绝。所有非 EMPTY slot（包括两个 PAIR_PENDING substage、UNPAIR 和 tombstone）的
MAC+LMK 必须常驻 ESP-NOW encrypted peer 表；启动在开放网络前按 slot_id 升序预装并
校验，任一失败只进入诊断。SERVER_PRECONFIRM 若需重发明文 challenge，只能按第 14.1 节
在 RadioActor 内短暂串行切换同一 MAC peer，不能依赖先收到加密包才安装。
30 秒只能淘汰旧 ACK/incarnation 窗，绝不能删除 tombstone 的运行时 peer 后再期待收到加密
恢复帧。这样 FINAL 的 LINK_ACK 丢失且一端长期离线仍可收敛，且六槽/六 peer/NVS 容量均
有界。掉电恢复绝不得把 tombstone 变回 ACTIVE。

tombstone 只通过两条显式路径释放：factory reset，或管理员第 17.1 节双重确认的
FORCE_FORGET。v1 不提供“占着旧 tombstone 同时配对新设备”的隐式 replacement，因为六槽
满时它会需要第七个加密 peer；更换离线设备的唯一流程是 FORCE_FORGET 成功释放 EMPTY，
再开普通 pairing window。FORCE_FORGET 明确放弃离线旧端三阶段收敛，先持久化不可回退的
slot generation/撤销 fence 与座位释放，再删除旧运行时 peer/LMK 并置 EMPTY；旧设备以后
只能重新配对。默认 graceful UNPAIR 永不因超时隐式走该路径。

存在活动 current session 时，UNPAIR 三阶段使用该 session。任一端重启、换信道或 session 已失效
时，直接使用第 13.2 节 kind=UNPAIR、generation=unpair_id 的 LMK 加密 session_id=0
恢复域继续原 stage，不先恢复普通 LINK/SESSION。该域只接受 UNPAIR 和 LINK_ACK，绝不接受
座位、快照或游戏命令。
PAIR_REJECT 因未认证只能作为提示，不能改变 binding。

会话消息字段按线序冻结为：

| 消息 | body 字段 |
| --- | --- |
| CLIENT_HELLO | schema；device_id u64；binding_id u64；link_confirmation[8]；selected_major/minor；selected_capabilities u32；max_frame_length u16；max_message_length u16；display_width/height 各 u16；firmware major/minor/patch；firmware_build_id u32；supported_catalog_count u8 1–4；catalog_root_hash[32] × count |
| SERVER_HELLO | schema；server_id u64；room_id u64；selected_major/minor；server_capabilities u32；selected_capabilities u32；required_capabilities u32；current_state_version u32；server_tick_ms u32；room_status u8；resume_disposition u8；max_players u8=6；tile_count u8；selected_catalog_root_hash[32]；room_content_manifest_hash[32] |
| JOIN_REQUEST | schema；join_request_id u32 非零；join_nonce u32 CSPRNG；selected_catalog_root_hash[32]；room_content_manifest_hash[32]；设备不得指定座位 |
| SEAT_ASSIGNED | schema；correlation_request_id u32；assignment_reason u8；room_id u64；binding_id u64；assignment_version u32；seat_id u8；player_id u8；seat_token[16]；color_id u8；avatar_id u16；name |
| SEAT_ACK | schema；correlation_request_id u32；room_id u64；assignment_version u32；seat_id u8；player_id u8；SHA-256(seat_token) 前 8 字节 |
| SESSION_RESUME | schema；resume_request_id u32 非零；binding_id u64；room_id u64；seat_id u8；player_id u8；seat_token[16]；last_state_version u32；last_snapshot_message_id u32 |
| SESSION_REVOKED | schema；correlation_request_id u32；binding_id u64；room_id u64；seat_id u8；player_id u8；reason u8；rejoin_allowed bool；current_state_version u32 |
| RESYNC_REQUEST | schema；reason u8；last_applied_version u32；expected_from_version u32；observed_version u32；last_message_id u32 |
| SNAPSHOT_APPLIED | schema；snapshot_message_id u32；snapshot_state_version u32；snapshot_crc32 u32 |
| SYNC_COMPLETE | schema；snapshot_message_id u32；snapshot_state_version u32；catchup_target_version u32；gate_generation u32 非零 |

room_status：0 NO_ROOM、1 LOBBY、2 SETUP、3 IN_GAME、4 RECOVERING、5 GAME_OVER。
SESSION_REVOKED reason：1 TOKEN_INVALID、2 ROOM_ENDED、3 SEAT_REASSIGNED、4
BINDING_REVOKED、5 DEVICE_MISMATCH、6 ADMIN_UNSEATED、7 ROOM_CONTEXT_CHANGED。
`rejoin_allowed` 的唯一含义是“关闭本 session 后，客户端是否可自动重做 LINK→HELLO 并按新
HELLO disposition 继续加入流程”；它不承诺恢复原座位，也不改变所有 SESSION_REVOKED 都是
session-fatal 的事实。固定值为：TOKEN_INVALID=0、ROOM_ENDED=1、SEAT_REASSIGNED=0、
BINDING_REVOKED=0、DEVICE_MISMATCH=0、ADMIN_UNSEATED=0、ROOM_CONTEXT_CHANGED=1。

SESSION_REVOKED 的原因、来源与动作唯一矩阵如下；未列出的来源不得发该 reason：

| reason | 允许来源与唯一判定 | Authority/binding 状态 | 客户端在先排入传输 ACK 后的唯一动作 |
| ---: | --- | --- | --- |
| 1 TOKEN_INVALID | 仅直接 SESSION_RESUME；当前 binding、device、room、seat、player 全部匹配，且没有更高优先原因，常量时间 HMAC 比较失败 | 拒绝本身不改权威状态；binding 与原 seat 保持原样 | 清除本地 seat token 和旧座位声明并关闭旧 session；不得因本帧自动 LINK/JOIN/RESUME。只有用户明确选择重新获取座位凭据或管理员完成恢复后，才可手动新建 LINK→HELLO |
| 2 ROOM_ENDED | 直接：请求 room 已 tombstone、已 RESET_PREPARED/NO_ROOM，或不再是 current room；异步：reset/ROOM_CLOSED 完成 canonical NO_ROOM 激活 | reset 已把旧 occupant 清除，binding 保持 ACTIVE/PAIRED_UNSEATED；直接拒绝本身无新副作用 | 清除旧房间、座位、token 与全部旧 pending；允许自动 LINK→HELLO，等待新房间上下文 |
| 3 SEAT_REASSIGNED | 仅直接 SESSION_RESUME；current room 仍有效、binding/device 匹配，但请求 seat/player/assignment 不再属于该 binding，且没有 reason 2/4/5 | 拒绝本身无新副作用；以当前 Authority assignment 为准 | 清除旧座位/token 并关闭旧 session；不得因本帧自动 LINK/JOIN/RESUME。只有管理员明确分配或用户确认进入未入座状态后，才可手动新建 LINK→HELLO |
| 4 BINDING_REVOKED | 仅直接 SESSION_RESUME 的竞争窗口；以 current session 已认证 binding 查询的 BindingSlot 已非 ACTIVE，或命中 UNPAIR/FORCE/revocation fence。不得按请求中任意 binding_id 探测别槽；常规 graceful UNPAIR 仍走 UNPAIR 协议，LMK 已删除的 force-forget 只丢弃，不能伪造本响应 | 撤销在本拒绝前已成立；不再改变 seat/binding | ACK 排队后清除旧 session、seat token 和该 binding 凭据，进入 REPAIR/重新物理配对；不得自动 LINK/JOIN |
| 5 DEVICE_MISMATCH | 仅直接 SESSION_RESUME；current session 的已认证 binding 仍 ACTIVE，但请求 binding_id、来源 MAC、device_id 或 slot_generation 任一不等于该 session 冻结 identity | 不改权威 binding/seat；安全记录脱敏诊断 | ACK 排队后废弃旧 session/token、隔离本地 binding 并进入 REPAIR；未经物理确认不得自动清除或复用密钥，也不得自动 LINK/JOIN |
| 6 ADMIN_UNSEATED | 仅异步；已 flush 的管理员 SEAT_UNBOUND/CONTROLLER_CHANGED COMMIT 在提交边界仍有该 binding 的旧 current session 时触发 | COMMIT 已清座位/presence/occupant mirror，binding 保持 ACTIVE/PAIRED_UNSEATED | 清除旧 seat/token/pending 并关闭旧 session；不得因本帧自动 LINK/JOIN/RESUME 或抢回座位。只有用户确认提示或管理员明确重新分配后，才可手动新建 LINK→HELLO。提交边界已无旧 session 时不补造异步帧；后续 HELLO 显示 PAIRED_UNSEATED，客户端若竞态发送 stale RESUME 则按 reason 3 |
| 7 ROOM_CONTEXT_CHANGED | 仅异步；`/session/new`、ROOM_RESUMED 或 content/recovery context 变更使旧 HELLO 上下文失效 | 不撤销 binding，也不直接改变原 seat assignment | 放弃旧 session pending，允许自动 LINK→HELLO 取得新 room/content/recovery hashes，再严格服从新 resume disposition |

直接 SESSION_RESUME 在静态 schema/range、current-session 认证、方向、幂等容量和 RESERVED 槽
校验完成后，必须在同一串行裁决中按 `4 → 5 → 2 → 3 → 1 → success` 检查；首个成立原因胜出，
不得继续检查或用较低优先原因泄露状态。静态畸形仍用 PROTOCOL_ERROR(MALFORMED_FRAME 或
LENGTH_MISMATCH)，不进入本矩阵。直接响应固定
`correlation_request_id=resume_request_id`，并逐值回显已通过静态校验的请求
binding_id/room_id/seat_id/player_id；`current_state_version` 在首次裁决时采样一次，等于
header.state_version 和 terminal_state_version。reason 6/7 不允许作为直接拒绝。

异步响应固定 `correlation_request_id=0`、binding_id=current session 的已认证 binding_id；若旧
session 在触发点有合法 LastKnownSeatTuple，则 room_id/seat_id/player_id 使用触发前冻结的旧
tuple，否则三者全零，不允许部分为零。reason 2 的 current_state_version 是已激活 canonical
NO_ROOM 的 0，reason 6 是管理员 COMMIT.committed_version，reason 7 是新 context 首次可见的
Authority version；均要求 header.state_version 与 body 逐值相等。若同一串行点有多个尚未建立
drain 的异步原因，只能按 `2 → 6 → 7` 选择首个；一旦 drain 已建立，body 不再被后来原因替换。

每个 current session 只有一个固定容量
`RevocationDrain(reason, correlation_request_id, identity_tuple, current_state_version,
direct_terminal_slot_or_none, immutable_body)`。直接拒绝必须先把已 RESERVED 的 resume 槽原地写成
SESSION_REVOKED TERMINAL，再关 business gate 并挂接该槽；异步撤销使用独立的每-session drain
记录，不进入 request terminal cache。若直接终态已建立，它优先于后来异步原因且必须按原 body
完成。drain 挂起后不再接收业务 ingress，只允许覆盖既有可靠帧或撤销帧的 ACK、不能开 gate 的
PING、同 binding 的 session_id=0 LINK_HELLO，以及仅在 direct drain 上与原
`(correlation_request_id, semantic_hash)` 都相同的 SESSION_RESUME duplicate；该 duplicate 只附着/
重发原 terminal 槽，不算新业务 ingress。其他 SESSION_RESUME（包括同 ID 异 hash 或异 ID）
静默丢弃、不作业务 ACK 且不改原槽；本 drain ingress 规则严格优先于第 13.2 节通用的
same-key/异-hash REQUEST_ID_CONFLICT，进入 drain 后绝不再构造第二个 PROTOCOL_ERROR。
新 LINK 只有在其自身认证/状态仍合法时才可
提前终止旧 drain。撤销帧必须排在既有可靠序号链中，已占 TX 槽先正常 drain，不能跳过序号；
收到覆盖撤销帧的 ACK、接受合法新 LINK 或撤销帧重试耗尽时销毁旧 session。

同一直接 resume ID/hash 在旧 drain 存活期间只重发 terminal 槽内同一 SESSION_REVOKED；除
ack_base/ack_bits 外 header/body 不变，不再查询 Authority、重复清座或建立第二个 drain。客户端
验证撤销帧后必须先排入即时传输 ACK，再清除 correlation 对应的 PendingResume 和旧 session
全部其他 pending，随后执行矩阵动作；reason 4/5 不得在清密钥后才尝试 ACK。所有 reason 1–7
都关闭旧 session，全部 SESSION_REVOKED（包括直接缓存终态）都不得进入就地 RESYNC 的
post-sync replay set，也没有隐藏的 retry_policy 字段。

P7 必须覆盖：同一请求同时命中多项失配时严格得到 4/5/2/3/1 优先级；reason 1–7 的固定
rejoin_allowed、correlation 与 identity/current-version 规则；直接终态首次发送、同 hash duplicate、
传输重试和 ACK 后销毁均无第二次副作用；管理员 unseat 的 reason 6 异步 drain、reset 的 reason 2
异步 drain，以及二者在无旧 session 时不伪造帧；所有 SESSION_REVOKED 都不出现在 post-sync
集合。RESYNC_REQUEST reason：1
VERSION_GAP、2 SNAPSHOT_CRC_FAILED、3 SNAPSHOT_SCHEMA_FAILED、4 PATCH_SCHEMA_FAILED、
5 RX_QUEUE_OVERFLOW、6 LOCAL_STATE_INVALID、7 MANUAL_DIAGNOSTIC、8 REASSEMBLY_TIMEOUT。
所有长度由这些字段
和名字长度唯一确定；固定部分之外不得有尾随字节。

assignment_reason：0 NEW_JOIN、1 RESUME、2 REISSUE。JOIN_REQUEST 的唯一成功终态是同一
join_request_id 的 SEAT_ASSIGNED(NEW_JOIN 或 REISSUE)；SESSION_RESUME 的唯一成功终态是同一
resume_request_id 的 SEAT_ASSIGNED(RESUME)，失败终态是同一 ID 的 SESSION_REVOKED。
收到成功终态后对应 pending 即清除。
圆屏对 NEW_JOIN/REISSUE 发送 SEAT_ACK；对 RESUME 不再发送 SEAT_ACK，因为有效
SESSION_RESUME 已完成 presence COMMIT，SEAT_ASSIGNED(RESUME) 的传输 ACK 足够。若兼容客户端
仍发送完全匹配的重复 SEAT_ACK，服务端只做传输 ACK 并复用正在进行的 sync，绝不能启动
第二份快照或递增版本。

CLIENT_HELLO 的 catalog roots 必须按原始 32 字节字典序严格升序且不重复。无活动房间时
SERVER_HELLO 固定 room_id=0、current_state_version=0、room_status=NO_ROOM、
resume_disposition=PAIRED_UNSEATED、tile_count=0 且两个内容 hash 全零，设备可保持
PAIRED_UNSEATED；存在活动房间时，服务端只
选择自己 `console-catalog-v1.json` 的 root，若客户端列表不含它，立即用
UNSUPPORTED_CONTENT 结束 HELLO，不创建 PendingJoin 或座位。SERVER_HELLO 同时冻结当时
房间 manifest；JOIN_REQUEST 必须逐字节回显两个非零 hash，服务端在座位 COMMIT 前再次与
当前房间比较，变化则 UNSUPPORTED_CONTENT/STALE_STATE。由此内容能力检查发生在
SEAT_BOUND 之前，而不是依赖入座后的快照失败。

`/session/new` 或 ROOM_RESUMED 成功后，所有用旧 NO_ROOM/RECOVERING/room/hash context 建立
且尚无 drain 的 current session 必须按上表 reason 7 建立 RevocationDrain；reset/ROOM_CLOSED
对尚无 drain 的 session 按 reason 2 建立同一结构。已有 direct/async drain 均逐字节保留并继续
自身收敛，不替换 reason、不补第二帧。这里不再另定义第二套 context-revoke 状态或宽松字段规则。由此先配对
于 room_id=0 的屏，以及恢复期间已建链的屏，都不需要人工断线才能取得新上下文。

未入座 ACTIVE binding 的合法 JOIN_REQUEST 在其 BindingSlot 对应的固定 RAM 区建立唯一
`PendingJoin(session_id, binding_id, sender_epoch, join_request_id, semantic_hash, join_nonce,
expires_ms, terminal_slot_index)`；terminal_slot_index 必须指向第 13.2 节同一 JOIN_REQUEST
high-watermark domain 已预留的 RESERVED terminal 槽，并由该 peer 四个固定未完成小槽之一持有，
不能按 request_id 再扫描或临时分配第二槽。
semantic_hash 按第 13.2 节精确包含 JOIN_REQUEST.header.state_version 与完整 raw body。全局
最多六项、60 秒。相同幂等键/hash 继续等待，异 hash 返回 REQUEST_ID_CONFLICT；设备不得
指定 seat。超时必须把 terminal_slot_index 指向的原 RESERVED 槽原地写成
PROTOCOL_ERROR(SEAT_UNAVAILABLE) TERMINAL 后才清 PendingJoin；设备须用新 request_id 重试。PendingJoin
丢于重启是安全的：新 session 不会伪造旧终态，设备重发新 JOIN。只有第 17.1 节
`/peers/assign` 可消费它并建立座位；不存在 PendingJoin 时不得由网页先占实体绑定。
该 binding 接受新 WELCOME/SESSION_ACTIVE 时必须先作废所有旧 session PendingJoin；assign
dispatch 再复核 pending 的 session_id/sender_epoch 等于当前 SESSION_ACTIVE，不同则清除并
返回 409 SEAT_UNAVAILABLE。session reset/new 同样清除全局六项，旧请求不得跨房间消费。
assign 与 60 秒超时复用第 7.3 节入口边界：HTTP assign 的 received_at_ms 严格早于
PendingJoin.expires_ms 才有效，等于/晚于均超时；早到且已入队的 assign 即使稍后 dispatch
也先于 timeout，timeout dispatch 前必须复核不存在该更早合法 assign。assign 的 Authority
COMMIT flush 成功后，必须先把精确 SEAT_ASSIGNED(NEW_JOIN) body 写入 terminal_slot_index 指向
的原槽并原地转 TERMINAL，随后才清 PendingJoin、发布无线终态和完成 HTTP operation；不得在
已产生座位副作用后释放该槽或另建缓存项。

已分配座位的同一 ACTIVE binding 在同一 room 再发 JOIN_REQUEST 时，服务端不做新权威提交，
而是用当前 assignment_version 返回 SEAT_ASSIGNED(REISSUE)，并将请求时的当前版本放入信封。
该路径只能向原 binding 重发其自身 token；座位、binding 或 room 不一致均拒绝。这使设备在座位
提交已落盘、首次 SEAT_ASSIGNED 尚未到达时掉电后仍能幂等找回同一座位。REISSUE 也必须先按
第 13.2 节预留并推进 JOIN_REQUEST domain，且在发送前把同一 RESERVED 槽原地转为该
SEAT_ASSIGNED TERMINAL。

SESSION_RESUME 使用同一组每 peer 四个固定未完成小槽建立
`PendingResume(session_id, binding_id, sender_epoch, resume_request_id, semantic_hash, room_id,
seat_id, player_id, terminal_slot_index)`；terminal_slot_index 必须指向同一 SESSION_RESUME
high-watermark domain 在推进前预留的 RESERVED 槽。admission 先完成静态字段、session/domain
互斥和容量校验，再原子预留槽、推进该域 high watermark 并注册 PendingResume；dispatch 在任何
副作用前二次复核 session、sender_epoch、binding/room/seat/player 与当前 Authority/NVS mirror。
token、binding、room、seat 或 player 拒绝把 SESSION_REVOKED 精确 body 写入该槽并原地转
TERMINAL；合法 WAIT_ONLINE 恢复先完成下文唯一 presence COMMIT，flush 成功后再把
SEAT_ASSIGNED(RESUME) 写入原槽并转 TERMINAL；已经 ONLINE 的合法重复不提交，但仍把当时裁决
的 SEAT_ASSIGNED(RESUME) 写入原槽。三条路径都必须先完成 TERMINAL 转换再清
PendingResume/发送，重传只读该槽，绝不能再次做 presence 副作用。

JOIN/RESUME 的 current session 或 room/content context 若在任何 Authority/runtime 副作用前失效，
必须销毁对应 pending 小槽并释放它指向的 RESERVED 槽；该 session namespace 同时销毁，旧请求
只能在新 LINK 后以新 session 重新发起。若副作用已经 COMMIT，则禁止释放：必须先按上述规则把
原 RESERVED 槽终态化；之后若尚无 drain 才按第 13.5 节建立与触发原因对应的
RevocationDrain，已有 drain 逐字节保留且不补第二帧。该 terminal 槽由 cache/pin 规则保留到旧
session drain。P7 golden 必须覆盖三类请求合计四个 pending 占满（其中 JOIN 或 RESUME
至多一个且二者互斥）、第五个无副作用 SERVER_BUSY、同域第二个未终态请求不替换、终态淘汰后的旧 ID 防 ABA、assign COMMIT 后发送
丢失、resume presence COMMIT 后发送丢失，以及 context 在副作用前/后失效的两条不同轨迹。

seat_token_key 是服务端首次 provision 时由平台 CSPRNG 生成的 32 字节 NVS 密钥。令
`material = ASCII "seat-v1" || room_id u64 LE || binding_id u64 LE || assignment_version u32 LE ||
seat_id u8 || player_id u8 || derivation_counter u8`，seat_token 是
HMAC-SHA256(seat_token_key, material) 前 16 字节。derivation_counter 初值 0；若结果恰为全零，
递增并重算，最多到 255，该计数器与 assignment_version 一起进入 AuthorityStateBlob。
服务端只在 NVS 持久主密钥，在权威状态持久派生输入；收到 SESSION_RESUME 时重新派生
期望 token 并做常量时间比较，不需要持久明文 token 或另一份 verifier。

SEAT_ACK.correlation_request_id 只是回显 assignment 的关联 ID，不创建新请求，也不进入
玩家命令幂等命名空间；重复 ACK 按 room_id+assignment_version 幂等。`online` 是持久
GameState 字段而非链路旁路标志。每次 SEAT_BOUND 同一 COMMIT 分配唯一
presence_transaction_id；该 ID 必须取自第 7.3 节同一个 next_transaction_id，不另设座位
命名空间且不复用。随后创建 `WAIT_ONLINE(presence_generation=1, instance=0)`；generation
使用 u32、每次状态转换加一且不回绕。服务端第一次收到有效 SEAT_ACK，或离线后的有效
SESSION_RESUME 时，都先把它作为已认证 REAL_CONSOLE ingress 做同一幂等 COMMIT；request_id
分别为 correlation_request_id 与 resume_request_id，幂等键仍包含不同 message_type，
system_trigger_id=0：
online=false→true、WAIT_ONLINE→ONLINE_WATCH、分配新的 deadline_instance_id 并递增
player_revision/presence_generation，然后从这个已提交版本开始完整快照。若已经为 true，
重复 ACK/RESUME 不增版而直接从当前版本重同步。presence COMMIT 成功后才发送
SEAT_ASSIGNED(RESUME) 与快照。圆屏按
第 13.3 节完成 SNAPSHOT_APPLIED、补丁链和 SYNC_COMPLETE 后，还必须在后续 PING 回报
`last_applied_state_version >= catchup_target_version`，服务端才打开不持久的
RuntimeSeatGate.command_enabled，并在打开后返回匹配 PONG；客户端遵守
SYNCED_WAIT_GATE/PONG current-version 复核。SYNC_COMPLETE/LINK_ACK 本身不再修改 GameState。

LINK_UP/SUSPECT 只改变运行时链路 overlay，不直接增改 GameState；但发现 SUSPECT 的一端必须
立即关闭自身危险操作。圆屏本地发现时隐藏 AVAILABLE_ACTIONS、作废普通 heartbeat outstanding
且不得发送玩家命令；服务端本地发现时关闭 RuntimeSeatGate.command_enabled、把 phase 置
SUSPECT_PROOF_PENDING 并为新 episode 递增 gate_generation。普通加密游戏帧绝不能直接恢复
ONLINE。服务端在 SUSPECT_PROOF_PENDING 收到 HEARTBEAT 时保持关 gate，回
SUSPECT_RECOVERY_REQUIRED 和当前 generation；这条反向信号使“client→server 单向丢包、只有
服务端先发现”也能自动收敛。圆屏收到 base tuple 匹配的 HEARTBEAT PONG 的 disposition=2 后必须立即进入
本地 SUSPECT/禁 UI，并用新 ping_id、PONG 所带 generation 发送 SUSPECT_RECOVERY。

圆屏先发现而服务端仍为 OPEN 时，它可先用新 ping_id 和上次已知 generation 发送
SUSPECT_RECOVERY；服务端不得直接把这第一帧当 proof，而是先关 gate、建立新 SUSPECT episode、
递增 generation，并回 disposition=2。圆屏再用另一个更高 ping_id 和新 generation 发送真正
proof。服务端仅在 phase=SUSPECT_PROOF_PENDING、generation 精确匹配、received_at_ms 严格早于
offline_deadline_ms、同一 session/sender epoch、可靠链无缺口且客户端版本等于当前 Authority
版本时，原子恢复 OPEN/command_enabled 后回 disposition=0。客户端只有在
`(session_id,probe_kind,ping_id,requested_gate_generation,client_send_ms)` 与唯一 outstanding tuple
一致、disposition=0、PONG.server_gate_generation 等于 requested_gate_generation 且 PONG 版本仍
等于本地版本时恢复 ONLINE/UI。

SUSPECT proof 的客户端版本小于服务端或可靠链有缺口时，服务端必须在同一临界区结束该
SUSPECT_PROOF_PENDING、按“新 sync generation”规则恰递增一次 gate_generation、转 SYNCING，
缓存/返回 disposition=1 与新 server_gate_generation，并调度该代完整个性化快照；不能复用旧
patch 链。客户端收到后丢弃旧 patch 计划，等待这一代 SNAPSHOT→SYNC_COMPLETE，再用 kind1。
PONG 显示更高版本同样按该路径处理。客户端版本大于服务端是不可由服务端快照
解释的 PEER_DESYNC，立即关闭 current session/re-LINK，绝不能开 gate 或把它当普通 RESYNC。
phase=SYNCING/SYNC_PROOF_PENDING 的 SUSPECT_RECOVERY 只回 disposition=1，phase=RELINK_REQUIRED
只回 3。SUSPECT 中意外到达的玩家命令在检查/推进玩家命令 high watermark 前返回非缓存
SERVER_BUSY(RETRY_SAME_REQUEST)；客户端收到该错误也必须立即进入本地 SUSPECT，并走上述
kind2 流程，不能原样忙重试形成死锁。正常客户端本就不得在 SUSPECT 发送命令。

ONLINE_WATCH 中持久 instance 是
PRESENCE_OFFLINE 的 pending source；达到 OFFLINE 阈值时先立即关闭 command gate，再以
presence_transaction_id、trigger_kind=13 和该 instance 做一次 true→false SYSTEM COMMIT，
原子关闭 instance、ONLINE_WATCH→WAIT_ONLINE 并递增 presence_generation/player_revision；
重复超时因 state/instance
不匹配不增版。RuntimeLink 保存 `offline_deadline_ms=last_valid_frame.received_at_ms +
3×heartbeat_period`；重新收到帧只在其 received_at_ms 严格早于该 deadline 时取消尚未
dispatch 的 OFFLINE trigger。等于或晚于 deadline 时，按第 7.3 节队列次序先提交 OFFLINE 并
销毁旧 session；随后只接受 session_id=0 LINK_HELLO，成功建立新 LINK 后才接受 SESSION_RESUME
并始终发送完整个性化快照，不能因调度延迟反向取消已经到期的离线。dispatch 还须
复核 source instance 和所携 deadline 与当前 runtime 记录一致。这样不会在发完
版本 M 的快照后暗中提交 M+1 presence 状态。OFFLINE 边界若命中 BOT_TAKEOVER，必须在
这同一候选 COMMIT 内同时完成第 18 节控制器切换并产生
PLAYER_CONNECTION_CHANGED 与 CONTROLLER_CHANGED/SEAT_UNBOUND，不能留第二条易丢的
隐式接管任务；由于座位不再由 REAL_CONSOLE 控制，该 COMMIT 还要终止并清零 presence
workflow/transaction/instance。PAUSE 和 WEB_ADMIN 都保持 offline REAL_CONSOLE；WEB_ADMIN 只允许管理员
随后显式调用 `/seats/takeover action=web`。`ready_player_mask` 是
`online/controller_initialized/command_enabled` 的运行时合取，不进入 AuthorityStateBlob、
不递增 state_version；GET summary 必须标注 `ready_mask_scope="runtime"`。它的任何变化都递增
第 17.1 节 projection_revision，使进行中的分页 projection_epoch 失效。`/session/start` 在
dispatch 点同时原子复核请求 mask 和当前 runtime gate。
会话请求使用第 13.2 节已包含 message_type 的通用幂等键，因此 JOIN 和 RESUME 的
相同数值 ID 也不冲突。

SEAT_ASSIGNED 最大 96 字节；CLIENT_HELLO 因 1–4 个 catalog root 精确为 79/111/143/175
字节，SERVER_HELLO 固定 107 字节，JOIN_REQUEST 固定 73 字节，其余会话消息小于 90 字节；
全部仍≤210。任何声明长度与实际字段不一致、未知枚举或非零保留值都在 ACK 前拒绝。

### 13.6 服务端事件 body schema

除 COMMAND_REJECTED 外，只有 COMMAND_ACCEPTED 与第 12.4 节明确注册的成功事件 ID 使用
第 12.3 节的 10 字节成功事件前缀，随后按
下表字段编码。异步广播使用 request_id=0。所有多字节整数小端。

| 事件 | 成功前缀后的字段，按线序 |
| --- | --- |
| COMMAND_ACCEPTED | command_type u16；accepted_at_server_ms u32 |
| COMMAND_REJECTED | 不用成功前缀，使用固定 24 字节错误体 |
| TURN_STATUS | round_number u16；active_player_id u8；turn_phase u8；roll_ordinal u8 0–3；doubles_streak u8 0–2；available_actions u32；deadline_ms u32 |
| ROLL_RESULT | player_id、roll_kind、die_a、die_b、total、is_double、roll_ordinal、origin_position、computed_target、passed_start，均 u8 |
| MOVE_GUIDANCE_STARTED | player_id、origin_position、target_position、distance、target_tile_kind 各 u8；manual_available_at_ms u32；movement_deadline_ms u32；path_count u8 1–40；path_positions u8 × count |
| RFID_POSITION_CONFIRMED | player_id、target_position、observed_position、confirmation_method 各 u8；confirmed_at_ms u32 |
| RFID_POSITION_REJECTED | player_id、target_position、observed_position、reason 各 u8；manual_available_at_ms u32 |
| MOVE_RESULT | player_id、origin_position、final_position、steps_moved、passed_start 各 u8；start_award i32；landing_tile_kind u8 |
| PURCHASE_OFFER | offered_player_id u8；asset_id u16；tile_position u8；price i32；offer_revision u16；deadline_ms u32 |
| RENT_OPPORTUNITY | landlord_player_id u8；payer_player_id u8；asset_id u16；tile_position u8；amount i32；rent_basis u8；rent_multiplier_permille u16；deadline_ms u32 |
| PAYMENT_REQUIRED | payer_player_id、creditor_kind、creditor_player_id 各 u8；amount i32；payer_cash i32；reason u8；source_asset_id u16；deadline_ms u32；auto_pay_enabled bool |
| PAYMENT_COMPLETED | payer_player_id、creditor_kind、creditor_player_id 各 u8；amount、payer_cash_after、creditor_cash_after 各 i32；reason u8 |
| DEBT_RESOLUTION_REQUIRED | debtor_player_id、creditor_kind、creditor_player_id 各 u8；amount_due、cash_available、shortfall 各 i32；allowed_actions u16；eligible_asset_count u8 0–28；asset_ids u16 × count |
| MORTGAGE_BATCH_COMPLETED | player_id u8；asset_count u8 1–28；asset_ids u16 × count；proceeds、cash_after、debt_remaining 各 i32 |
| BANKRUPTCY_RESOLVED | debtor_player_id、creditor_kind、creditor_player_id 各 u8；cash_transferred i32；asset_count u8 0–28；asset_ids u16 × count；eliminated_rank u8；next_player_id u8 |
| TRADE_OFFER | trade_version u16；status、proposer_player_id、counterparty_player_id 各 u8；proposer_gives_cash、counterparty_gives_cash 各 i32；proposer_asset_count、counterparty_asset_count、proposer_card_count、counterparty_card_count 各 u8；按此顺序的四组 u16 ID 列表；deadline_ms u32；confirmed_player_mask u8 |
| AUCTION_STATE | auction_kind u8；lot_id u16；asset_id u16；building_type u8；auction_version u16；status u8；current_bid i32；leader_player_id u8；leader_target_asset_id u16；eligible_mask、passed_mask、valid_bidder_mask 各 u8；minimum_next_bid i32；remaining_unit_count u8；deadline_ms u32 |
| CARD_REVEALED | player_id u8；deck_id u8；card_instance_id、card_catalog_id、effect_id 各 u16；amount i32；target_player_id、target_position、keepable、public_detail 各 u8 |
| ASSET_UPDATED | asset_id u16；tile_position、owner_player_id、mortgaged、building_level 各 u8；asset_revision u16；cause u8 |
| PLAYER_UPDATED | player_id u8；cash i32；position u8；in_hold bool；failed_hold_rolls u8 0–2；bankrupt、online、controller_type 各 u8；player_revision u16 |
| ROOM_RECOVERING | reason u8；recovery_generation u32；writes_enabled bool，必须为 0；entered_at_server_ms u32；admin_resume_required bool |
| GAME_OVER | winner_player_id、reason、ranking_count 各 u8；ranking_players u8 × count，count 1–6 |
| MORTGAGE_TAKEOVER_REQUIRED | recipient_player_id u8；asset_id u16；mortgage_value、interest_due、redeem_now_cost 各 i32；item_index、item_count 各 u8；deadline_ms u32 |
| BUILDING_DEMAND_STATE | building_type u8；stock_available、eligible_mask、registered_mask、status 各 u8；self_target_asset_id u16；self_target_level u8；self_list_cost i32；deadline_ms u32 |

COMMAND_ACCEPTED 使用通用成功前缀中的 replayed 字段且该字段必须为 0；本行不追加第二个
replayed 字节。

BUILDING_DEMAND_STATE 在 OPEN 时 deadline_ms 非零、registered_mask 必须是 eligible_mask
子集；若接收者尚未登记，self_target_asset_id/self_target_level/self_list_cost 全为零。所有
终态的 deadline_ms 为 0。

turn_phase 数值依次为 0 WAIT_ROLL、1 AWAIT_MOVE_CONFIRM、2 RESOLVE_TILE、3
PURCHASE_OFFER、4 AUCTION、5 RENT_CLAIM、6 PAYMENT、7 DEBT_RESOLUTION、8 EXTRA_ROLL、
9 TURN_END、10 MORTGAGE_TAKEOVER、11 BUILDING_DEMAND、12 HOLD_DECISION、0xFF NONE。roll_kind：0 TURN_ROLL、1 HOLD_EXIT_ROLL、2
UTILITY_RENT_ROLL、3 SETUP_ORDER_ROLL。tile_kind：0 START、1 PROPERTY、2 TRANSIT、
3 UTILITY、4 CARD、5 FEE、6 HOLD、7 REST、8 GOTO_HOLD。

UTILITY_RENT_ROLL 和 SETUP_ORDER_ROLL 不产生棋子路径，origin_position 与
computed_target 固定为 POSITION_UNKNOWN，passed_start=0。

creditor_kind：0 SYSTEM、1 PLAYER。payment reason：1 RENT、2 PURCHASE、3 FEE、4 CARD、
5 HOLD_EXIT、6 MORTGAGE_TAKEOVER、7 AUCTION。DEBT allowed_actions bit 0 MORTGAGE、
bit 1 MORTGAGE_BATCH、bit 2 SELL_BUILDING、bit 3 TRADE、bit 4 BANKRUPTCY，其他位为零。

TURN_STATUS.available_actions 的 bit n 对应命令 0x1000+n，n=0–19，bit 20–31 为零。
所有 player mask 的 bit n 对应 player_id=n+1，bit 6–7 为零；confirmed_player_mask 表示已
确认交易的参与者，eligible_player_mask 表示仍可竞价者，passed_player_mask 表示已退出者。
rent_basis：1 PROPERTY、2 TRANSIT、3 UTILITY。deck_id：1 CITY_EVENT、2 CIVIC_FUND。
CARD_REVEALED.public_detail：0 PRIVATE_ID_REDACTED、1 FULL_DETAIL_PUBLIC。

confirmation_method：0 RFID、1 MANUAL_TIMEOUT、2 ADMIN_TEST。RFID reject reason：1
WRONG_TILE、2 WRONG_TOKEN、3 RFID_TIMEOUT、4 MANUAL_NOT_ALLOWED、5
MOVEMENT_TRANSACTION_CLOSED。trade status：1 OFFERED、2 WAITING_COUNTERPARTY、3
SETTLED、4 REJECTED、5 EXPIRED、6 CANCELLED。auction_kind：1 UNOWNED_ASSET、2
BUILDING_STOCK；building_type：0 NONE、1 BUILDING、2 LANDMARK；auction status：1 OPEN、
2 SETTLED、3 NO_BIDS、4 CANCELLED。building demand status：1 OPEN、2 DIRECT_SETTLED、
3 AUCTION_OPENED、4 CLOSED_NO_STOCK。

ASSET_UPDATED cause：1 PURCHASE、2 MORTGAGE、3 UNMORTGAGE、4 BUILD、5 SELL、6 TRADE、
7 BANKRUPTCY、8 AUCTION、9 MORTGAGE_TAKEOVER。controller_type：0 REAL_CONSOLE、1 WEB、
2 BOT、3 UNASSIGNED。UNASSIGNED 保留玩家/座位经济身份但没有可提交游戏命令的 actor，
LOBBY 新槽和 IN_GAME 强制解绑都使用它。恢复 reason：
1 SERVER_RESTART、2 PERSISTENCE_FAILURE、3 INVARIANT_FAILURE、4 SNAPSHOT_FAILURE、
5 MANUAL_ADMIN_RECOVERY、6 UNSUPPORTED_CONTENT、7 ROLLBACK_REQUIRED、8
STORAGE_SPACE_LOW、9 RESET_INTERRUPTED。GAME_OVER reason：1 LAST_SOLVENT_PLAYER、2
ADMIN_TERMINATED_TEST、3 SOLE_DEMO_PLAYER_BANKRUPT。reason=3 时 winner_player_id 必须为 0、
ranking_count=1 且唯一 ranking player 是已破产 demo 玩家；reason=1 时 winner 必须为 1–6。

四个交易列表遵循第 12.3 节同一编码和上限。所有路径、ID 列表均升序或按明确路径顺序，
计数决定唯一长度；事件不得携带未定义尾随字段。CARD_REVEALED 对非本人投影可把私有
catalog/effect 置零，但已结算的公开 amount/target 必须一致。

ROLL_RESULT.player_id 在普通/限制区掷骰时是实际掷骰玩家，在 SETUP_ORDER_ROLL 时是该次
排序掷骰候选；不得从当前 active_player 反推。PURCHASE_OFFER.offered_player_id 是唯一可
接受/拒绝该报价的玩家。两条事件都可以按公开事件投影给其他座位，接收端仍能在重排、
断线追赶和开局排序期间唯一确定主体。

TRADE_OFFER 始终以最初创建者为 proposer，不能因接收者查看而翻转：proposer_gives_cash
及 proposer 的资产/卡片列表从 proposer 流向 counterparty，counterparty 对应字段反向
流动。TRADE_UPDATE 的发送者视角输入由服务器转换成这一稳定规范视角后再递增版本。

trade_version 从 1 开始，每个有效 TRADE_UPDATE 加一；达到 65534 后拒绝更多 UPDATE，
仍允许按当前草案 CONFIRM/REJECT/EXPIRE。需要继续编辑只能关闭后以新 transaction 创建交易，
绝不回绕。asset_revision/player_revision 同样只使用 1..65534；任何下一次变更将溢出时，
以 REVISION_EXHAUSTED 拒绝并进入可诊断 RECOVERING，不能回绕后接受旧 quote/patch。

动态事件 payload 最大值：MOVE_GUIDANCE_STARTED 64、DEBT_RESOLUTION_REQUIRED 84、
MORTGAGE_BATCH_COMPLETED 80、BANKRUPTCY_RESOLVED 76、TRADE_OFFER 104、GAME_OVER 19、
MORTGAGE_TAKEOVER_REQUIRED 31、BUILDING_DEMAND_STATE 26
字节；其余由固定字段求和且均小于 210。超出相应计数或长度直接拒绝，不能截断。

### 13.7 快照和补丁 section schema

v1 快照必须按 0x01–0x0B 顺序各包含一次下列 CRITICAL section，即使内容为空。补丁中的
section 使用完全相同 body 并表示整节替换；未出现的节保持不变。

0x01 CORE：

~~~text
session_phase u8；turn_phase u8；round_number u16；active_player_id u8；
tile_count u8；seat_count u8 1–6；turn_order_count u8；
turn_order_players u8 × count；blocking_transaction_id u32；current_roll_valid bool；
current_roll_kind u8；current_die1 u8；current_die2 u8；current_dice_total u8；
current_is_doubles bool；current_roll_transaction_id u32；current_roll_state_version u32
~~~

session_phase：0 LOBBY、1 SETUP、2 IN_GAME、3 RECOVERING、4 GAME_OVER。turn order 只含
未破产玩家且无重复。
round_number=0 只用于 NO_ROOM/LOBBY/尚未开始，游戏回合只允许 1..65534；0xFFFF 保留。需要
从 65534 再增一时不得提交或回绕，服务端进入 REVISION_EXHAUSTED RuntimeHealthOverlay，只
开放只读诊断、replay export 与 session reset。ROOM_CLOSED 仍使用第 11 节 state_version 与
第 7.3 节 event_id
逃生量完成。

current_roll 是“当前回合最近一次已提交权威掷骰”的显示上下文：DICE_ROLLED COMMIT 原子写入，
同一回合的 utility/card 等后续 roll 会替换；下一次 TURN_STARTED、GAME_OVER 或 reset 清零。
valid=false 时其余七字段全零；valid=true 时 die1/die2 为 1–6、total=两者和、is_doubles 与相等
关系一致，transaction/state_version 精确指向产生该 roll 的 COMMIT。规则只使用对应 workflow
另存的 frozen dice，网页/圆屏不得把这个显示上下文当作可再次结算的输入。

0x02 SELF_PLAYER：

~~~text
player_id u8；seat_id u8；name；color_id u8；avatar_id u16；cash i32；position u8；
in_hold bool；failed_hold_rolls u8 0–2；doubles_streak u8；bankrupt bool；online bool；controller_type u8；
player_revision u16
~~~

0x03 PUBLIC_PLAYERS：player_count u8 0–5，随后按 player_id 升序重复以下记录，不含 SELF：

~~~text
player_id u8；seat_id u8；name；color_id u8；avatar_id u16；cash i32；position u8；
in_hold bool；failed_hold_rolls u8 0–2；bankrupt bool；online bool；controller_type u8；player_revision u16
~~~

0x04 ASSETS：asset_count u8 0–28，随后按 asset_id 升序重复：

~~~text
asset_id u16；tile_position u8；owner_player_id u8；mortgaged bool；
building_level u8 0–5；asset_revision u16；self_action_mask u8；mortgage_proceeds i32；
unmortgage_cost i32；next_build_target_level u8；next_build_cost i32；sell_option_count u8 0–5；
按 target_level 升序重复 target_level u8、quoted_proceeds i32
~~~

self_action_mask：bit 0 MORTGAGE、bit 1 UNMORTGAGE、bit 2 BUILD_NEXT、bit 3 SELL、bit 4
TRADE_GIVE，bit 5–7 为零。非 SELF 所有或不适用动作的位、quote 和 target 必须为零；
sell_option_count 只列出当前状态下满足库存与均匀出售规则的完整原子目标。

0x05 PRIVATE_CARDS：card_count u8 0–8，随后按 instance ID 升序重复：

~~~text
card_instance_id u16；card_catalog_id u16；deck_id u8；tradable bool
~~~

此节只能包含 SELF 的卡片。

0x06 AVAILABLE_ACTIONS：action_count u8 0–20，随后按 command_type 递增重复：

~~~text
command_type u16；transaction_id u32；subject_id u16；minimum_amount i32；
maximum_amount i32；deadline_ms u32；action_flags u16
~~~

action_flags bit 0 REQUIRES_HOLD、bit 1 OPTIONAL、bit 2 FORCED_FLOW、bit 3
MANUAL_FALLBACK，bit 4–15 为零。

同一 command_type 最多一条。subject_id=0 表示命令级能力；资产参数命令的精确候选与
报价只从 ASSETS.self_action_mask/quote 读取。阻塞事务只有一个，因此其命令可把非零
transaction_id/subject_id 指向 ACTIVE_WORKFLOW 当前对象。AVAILABLE_ACTIONS 决定“是否可
发该命令”，具体组合参数仍由服务器复核；客户端不得因为未列出每个资产就扩张 action_count。
v1 每名玩家最多参与一个未关闭交易，故 TRADE_UPDATE/REJECT/CONFIRM 各自也最多一条；
新 TRADE_CREATE 在本人已有未关闭交易时不可用。

0x07 OBLIGATIONS：obligation_count u8 0–16，随后重复：

~~~text
transaction_id u32；obligation_type u8；status u8；counterparty_id u8；amount i32；
source_asset_id u16；deadline_ms u32
~~~

type：1 RENT、2 PURCHASE、3 FEE、4 CARD、5 HOLD_EXIT、6 AUCTION、7
MORTGAGE_TAKEOVER。status：1 OFFERED、
2 AUTHORIZED、3 DEBT_RESOLUTION、4 PAID、5 CLOSED。

0x08 TRADES：trade_count u8 0–1，只含 SELF 参与的唯一未关闭交易。每项复用 TRADE_OFFER 从
trade_version 到 confirmed_player_mask 的完整后缀，不含 10 字节事件前缀。

0x09 DEADLINES：deadline_count u8 0–16，随后重复 transaction_id u32、
deadline_instance_id u32、deadline_kind u8、deadline_ms u32。kind：1 PURCHASE、2 AUCTION、
3 RENT、4 PAYMENT、5 TRADE、6
MOVE_MANUAL、7 MORTGAGE_TAKEOVER、8 BUILDING_DEMAND、9 HOLD_DECISION。

0x0A CONTENT_REFS 首先编码 manifest_sha256[32]，再依次编码 ruleset、map、economy；每项
均为 id_length u8 1–32、ASCII id、revision u16、sha256[32]。随后是 deck_count u8 1–2
及同格式 deck，bot_policy_count u8 0–3 及同格式 policy。ID 必须与房间冻结清单完全
一致。

0x0B ACTIVE_WORKFLOW 是断线重连后的唯一强制流程 UI 来源，不依赖旧 session 事件：

~~~text
workflow_kind u8；stage u8；transaction_id u32；deadline_ms u32；
workflow_revision u32；payload_length u16 0–160；payload bytes
~~~

kind=0 NONE 时其余字段全零。非零 kind 的 transaction_id/workflow_revision 必须非零；revision
从 1 开始，每次登记、有效竞价、PASS、付款/债务转换、游标或阶段变化都递增且不得回绕。
CORE.blocking_transaction_id 必须等于本节 transaction_id。各 kind 的合法 stage 为：1
MOVEMENT=1 WAIT_POSITION/2 WAIT_MANUAL_CONFIRM；2 PURCHASE=1 OFFER_OPEN；3 AUCTION=1
BID_OPEN；4 RENT=1 CLAIM_OPEN；5 PAYMENT_DEBT=1 PAYMENT_OPEN/2 DEBT_RESOLUTION/3
DEBT_WAIT_FUNDING_TRADE；6
HOLD_DECISION=1 DECISION_OPEN；7 MORTGAGE_TAKEOVER=1 CHOOSE；8 BUILDING_DEMAND=1
CLAIM_WINDOW；9 CARD_RESOLUTION=1 RESOLVING。随后 payload 按线序固定为：

- 1 MOVEMENT：player_id、origin_position、target_position、target_tile_kind 各 u8；
  manual_available_at_ms u32；last_observed_position、last_reject_reason 各 u8；path_count u8
  1–40；path_positions u8 × count。
- 2 PURCHASE：player_id u8；asset_id u16；tile_position u8；price i32；offer_revision u16。
- 3 AUCTION：与 AUCTION_STATE 成功前缀后的完整字段相同，但不重复 deadline_ms，另追加
  self_target_asset_id u16。无主资产和建筑库存拍卖由 auction_kind 严格区分。
- 4 RENT：landlord_player_id、payer_player_id 各 u8；asset_id u16；tile_position u8；
  amount i32；rent_basis u8；rent_multiplier_permille u16。
- 5 PAYMENT_DEBT：payer_player_id、creditor_kind、creditor_player_id 各 u8；amount_due、
  cash_available、shortfall 各 i32；reason u8；source_asset_id u16；auto_pay_enabled bool；
  allowed_actions u16；funding_trade_transaction_id u32；eligible_asset_count u8 0–28；
  asset_ids u16 × count。stage 3 要求 funding ID 非零、allowed_actions=0，其他 stage 要求 ID=0。
- 6 HOLD_DECISION：player_id u8；failed_hold_rolls u8 0–2；fee i32；card_count u8 0–8；
  card_instance_ids u16 × count。
- 7 MORTGAGE_TAKEOVER：recipient_player_id、origin_kind 各 u8；asset_count u8 1–28；
  asset_ids u16 × count；cursor u8；current_asset_id u16；mortgage_value、interest_due、
  redeem_now_cost 各 i32。origin_kind：1 TRADE、2 PLAYER_BANKRUPTCY。
- 8 BUILDING_DEMAND：building_type、stock_available、eligible_mask、registered_mask 各 u8；
  intent_count u8 1–6；按 player_id 升序重复 player_id u8、target_asset_id u16、
  target_level u8、list_cost i32。
- 9 CARD_RESOLUTION：player_id、deck_id 各 u8；card_instance_id、card_catalog_id、effect_id
  各 u16；item_index、item_count、target_player_id 各 u8；amount i32。无权看到卡面者的
  catalog/effect 按 CARD_REVEALED 同样置零。

stage 枚举由 kind 单独定义并进入 protocol semantic schema；未知 kind/stage 必须拒绝整个
critical section。服务端每次提交都从 AuthorityState 的 blocking transaction/continuation
重新投影该 union，因此移动路径、出价、购买、租金、付款/债务、限制区、接管、建筑竞争和
卡牌游标均可只靠完整快照恢复。deadline_ms 必须与 DEADLINES 中同 transaction 项逐值一致；
无 deadline 的阶段两处均为 0。PAYMENT_DEBT stage 3 的 ACTIVE_WORKFLOW deadline_ms 固定为 0；
它等待的 trade deadline 只以 funding_trade_transaction_id 出现在 TRADES/DEADLINES，不能伪装成
debt transaction 自身的 deadline。该 section 总长不超过 176 字节。

进入限制区的玩家回合开始时使用 turn_phase=HOLD_DECISION、ACTIVE_WORKFLOW kind=6，并只
开放 HOLD_DECISION。TRY_DOUBLES 转入一次 HOLD_EXIT_ROLL：成功则以已保存骰子移动且不因
双骰获得额外回合，失败则 failed_hold_rolls 加一并结束回合；第三次失败先建立强制 $50
付款，完成后用该次已保存骰子移动。PAY_FEE/USE_CARD 完成付款或消费卡后转 WAIT_ROLL，
再等待普通 ROLL_REQUEST。HOLD_DECISION 保存 20 秒 deadline 和 instance；到期 SYSTEM
动作与 TRY_DOUBLES 完全同义且只掷一次。停在 HOLD 格但 in_hold=false 的访客始终走普通
WAIT_ROLL，不能出现 HOLD_DECISION。

整个 STATE_SNAPSHOT 和 STATE_PATCH 逻辑 payload 上限均为 8192 字节。所有 section 内
计数、引用、排序、UTF-8、内容哈希和隐私范围全部验证后，客户端才能原子替换视图；失败
时保持旧视图并发送 RESYNC_REQUEST。

## 14. 配对、安全与换信道

### 14.1 TEST_PSK_V1

双方本地持有相同 32 字节 commissioning secret，只允许存于 Git 忽略文件或 NVS。

配对 transcript 的字段顺序冻结为；全部整数小端：

~~~text
pairing_profile      u8 = 1 TEST_PSK_V1
pairing_window_id    u32
server_id            u64
device_id            u64
server_mac           6 bytes
console_mac          6 bytes
server_nonce         16 bytes
console_nonce        16 bytes
server_major         u8
server_min_minor     u8
server_max_minor     u8
console_major        u8
console_min_minor    u8
console_max_minor    u8
selected_major       u8
selected_minor       u8
server_capabilities  u32
console_capabilities u32
selected_capabilities u32
max_frame_length     u16 = 250
max_message_length   u16 = 8192
~~~

流程：

服务端在处理每个 PAIR_REQUEST 前先按 slot_id 0..5 扫描全部 non-EMPTY slot。只有当前
SERVER_PRECONFIRM 中来源 MAC、device_id、window 和 body/transcript 全部相同才重放既有
challenge；除此以外，只要 source MAC 或 device_id 命中任一 PAIR_PENDING/ACTIVE/
UNPAIR/TOMBSTONE/FORCE slot，就不得分配。source MAC 已命中时静默丢弃，绝不因未认证
请求切换一个 ACTIVE/unpair/tombstone 的加密 peer 去发明文 reject；只有当前相同
SERVER_PRECONFIRM 可按既有 RadioActor 明/密切换流程重发 challenge。不同的新 MAC 复用
已有 device_id 视为克隆，可为该新 MAC 短暂添加 plaintext temp peer 发送
PAIR_REJECT(ALREADY_BOUND) 后删除，且不改任何 slot/既有 peer。设备必须先走 LINK、graceful UNPAIR 或
管理员 FORCE_FORGET；绝不能为同一 MAC/device 分配第二槽或临时覆盖现有 LMK。

1. DISCOVERY 公布 server_id、pairing_window_id、服务端版本范围、能力和信道，不含
   房间状态，session_id/state_version 为 0。
2. 圆屏在首次发 PAIR_REQUEST 前先将完整 body、来源 MAC 和状态 REQUEST_PENDING 保存到
   NVS。body 携带 pairing_window_id、目标 server_id、device_id、console_nonce、客户端
   major/min_minor/max_minor、capabilities、max_frame_length=250 和 max_message_length=8192；
   来源 MAC 就是 console_mac。PAIR_REQUEST 是不可靠提示帧，未收到有效 challenge 时
   按 250ms、500ms、1s、2s、之后每 2s 逐字节重发同一 body；重启后也从 250ms
   序列恢复，不生成新 nonce。
3. 服务端选择共同版本并构造完整 transcript。在发送 challenge 前，它必须先将
   `role=SERVER、slot.state=PAIR_PENDING、pair_substage=SERVER_PRECONFIRM、server/console MAC、完整 transcript、transcript_sha256、
   LMK、key_version=1` 原子写入 NVS 并读回验证。写入失败则不发 challenge。配对
   窗口一次只处理一个 SERVER_PRECONFIRM。
4. 服务端为该 MAC 添加 WIFI_IF_STA、当前信道、encrypt=false 的临时 peer，发送
   PAIR_CHALLENGE。challenge 携带 server_nonce、双方版本范围、selected_major/minor、双方及
   选定 capabilities，以及 HMAC-SHA256(secret, ASCII "server" || transcript) 的前 16 字节。
   发送完后将 peer 替换为派生 LMK 的加密 peer。服务端此时尚未分配 binding_id，但
   掉电后可从 PRECONFIRM 恢复 LMK 并解密原 proof。
   PRECONFIRM 状态收到同一来源 MAC 且 PAIR_REQUEST body 逐字节相同的重发时，必须使用
   已持久 server_nonce/transcript/proof 重发逐字节相同的 PAIR_CHALLENGE；
   attempt_timeout_ms 固定为 10000，始终是从每次收到该 challenge 起计的相对窗口。同 MAC
   但 body/window/transcript 不同按前置身份门禁静默丢弃；只有不命中任一既有 MAC/device 的
   其他新来源在全局 SERVER_PRECONFIRM 忙时，才用该新 MAC 的临时 plaintext peer 返回
   PAIR_REJECT(SERVER_BUSY) 后删除，不覆盖 PRECONFIRM。
   因 PAIR_REQUEST 是广播，即使该 MAC 的单播 peer 已切为 encrypt=true，服务端仍能收到
   重发。重发 challenge 必须在唯一 RadioActor 中串行执行：暂停该 MAC 的配对 RX
   交付，删除加密单播 peer，以相同 interface/channel 添加 encrypt=false peer，发送已保存
   challenge，等 esp_now_send callback（最多 50ms）后删除明文 peer，用原 LMK 重建
   encrypt=true peer，再恢复 RX 交付。此窗口内到达的迟到 PAIR_CONFIRM 不 ACK、不改状态；
   圆屏按第 13.2 节继续重传同一未 ACK 槽，不能跳到新 frame_seq。禁止在回调中直接增删 peer。
5. 圆屏验证 server_proof 后删除未加密 peer、安装 PMK 和加密服务端 peer，先把
   `role=CONSOLE、state=CONFIRM_PENDING、transcript、LMK` 保存到 NVS，再严格按第 13.5 节
   发送 PAIR_CONFIRM。client_proof 是 HMAC-SHA256(secret, ASCII "client" || transcript) 的前
   16 字节。
6. 服务端常量时间验证 proof。失败时持久撤销 PRECONFIRM 后才删除临时 peer；成功则
   生成非零 binding_id，并将同一 NVS 记录原子转为
   `slot.state=PAIR_PENDING、pair_substage=SERVER_BINDING_PENDING、binding_id、
   transcript_sha256、key_version u16=1`。
7. PAIR_ACCEPT 严格使用第 13.5 节唯一布局和新 LMK 可靠发送。同一 transcript 的重复
   PAIR_CONFIRM 必须重放同一 binding_id/PAIR_ACCEPT，不得生成第二个 binding；原 ACCEPT
   槽未获 LINK_ACK 时必须复用该槽，已清槽后才分配新序号。
8. 收到 PAIR_ACCEPT 的圆屏将记录转为 BINDING_PENDING，发送 LINK_HELLO。服务端只有在该 binding
   完成 LINK_WELCOME 和新 session 的 CLIENT_HELLO 后才把 NVS 状态改为 ACTIVE；圆屏在
   SERVER_HELLO 后同样改为 ACTIVE。PRECONFIRM/CONFIRM_PENDING/BINDING_PENDING 都不能
   分配座位。

服务端处于 SERVER_BINDING_PENDING 时，一个通过 LMK、binding_id、device_id 和 fresh
client_link_nonce 全部校验的 LINK_HELLO 本身就是“PAIR_ACCEPT 已被圆屏收到”的业务证据；
即使 ACCEPT 的 standalone LINK_ACK 丢失，也要先把旧 PAIR TX/RX incarnation 转为 30 秒只读
墓碑、停止重发 ACCEPT，再建立 LINK generation。不得一端卡在等 ACCEPT ACK、另一端已进入
LINK。

传输 LINK_ACK 只清除 PAIR_CONFIRM 的物理发送槽，不清除 CONFIRM_PENDING。圆屏等待
PAIR_ACCEPT 的业务计时依次为 1s、2s、4s；每次超时都使用完全相同的 PAIR_CONFIRM
body/proof，并严格按第 13.2 节先复用未 ACK 槽、已 ACK 才分配新 frame_seq/message_id。
三次业务计时后暂停发送，保留 CONFIRM_PENDING；
再次收到同一 server_id 的 DISCOVERY/RECOVERY_BEACON 或设备重启时，恢复加密 peer 并从 1s
序列重新发送同一 proof。

若任一端在 CHALLENGE 与 ACCEPT 之间掉电，双方启动后根据上述明确状态恢复 transcript、
LMK 和来源 MAC。PRECONFIRM/CONFIRM_PENDING/BINDING_PENDING 没有静默超时删除；
互动尝试的 10 秒 deadline 过后，服务端仍只接受与已持久 transcript 逐字节匹配的
原 proof。管理员只能通过第 17.1 节 FORCE_FORGET 双重确认撤销该 PAIR_PENDING，释放为
EMPTY 后再开全新的配对窗口；v1 不原位替换仍占槽的 transcript。

PAIR_REJECT 是未认证的诊断提示，不能创建、改变或撤销既有 binding。UNPAIR 只接受当前
binding 的加密单播。pairing_window_id、双方 nonce、server_id、device_id、binding_id、
session_id 和 sender_epoch 均由平台 CSPRNG 生成；旧窗口或旧 nonce 的证明不得复用。
服务端不得同时保留超过一个未完成临时配对 peer，新的请求返回 SERVER_BUSY。

服务端全局 PMK：

~~~text
HKDF-SHA256(
  secret,
  salt = server_id 的 8 字节小端表示,
  info = ASCII "gridopoly-pmk-v1",
  length = 16
)
~~~

每 peer 独立 LMK：

~~~text
HKDF-SHA256(
  secret,
  salt = server_nonce[16] || console_nonce[16],
  info = ASCII "gridopoly-lmk-v1" || server_mac[6] || console_mac[6],
  length = 16
)
~~~

六个 peer 共享同一 PMK，每台使用不同 LMK。发现和恢复提示为未加密广播；玩家状态、
快照、座位和游戏事件只能加密单播。

TEST_PSK_V1 的共享 commissioning secret 被任意一台设备提取后会降低整套测试网络的
配对安全，因此只用于本地原型和多屏联调，不作为量产安全声明。

配对状态：

~~~text
UNPAIRED → DISCOVERING → PAIRING → SECURE_VERIFY
→ PAIRED_PENDING → PAIRED_UNSEATED → SEATED → SYNCING → SYNCED_WAIT_GATE → ONLINE
~~~

SYNCING/SYNCED_WAIT_GATE 的进入、失效、PING/PONG 证明与命令禁用完全以第 13.3 节为准；
ONLINE 遇版本缺口回 SYNCING，任一子态断链回 SEATED/LINK 重建路径，不能从 SYNCING 因收到
SYNC_COMPLETE 直接跳 ONLINE。

- 网页显式开启 60 秒配对窗口。
- 新 transcript 的初始交互尝试 10 秒；已持久 PRECONFIRM/CONFIRM_PENDING 的原 proof 恢复
  按第 14.1 节处理，不被该 10 秒静默删除。
- 连续三次失败冷却 30 秒。
- 最多六个有效加密 peer。
- 更换设备前撤销旧 binding 并删除旧 peer。
- 配对广播不携带房间状态、Wi-Fi 凭据、密钥或 seat_token。
- 服务端单边 FORCE_FORGET 后，离线旧圆屏仍可能保留本地 tombstone/LMK；它不得因收不到
  服务端、PAIR_REJECT 或其他未认证提示自动清除。用户必须在圆屏本地物理确认
  `FORGET_SERVER`（显示 server_id/binding_id）或 factory reset，原子删除本地 tombstone 后
  才能回到 UNPAIRED/DISCOVERING 并重新配对。

### 14.2 Wi-Fi 和信道恢复

服务端启动顺序：

1. WIFI_STA 连接家庭 2.4GHz Wi-Fi。
2. 获得 IP 后读取实际主信道。
3. 初始化 ESP-NOW。
4. peer 使用 WIFI_IF_STA 和 channel=0 跟随实际信道。
5. 启动网页和游戏服务。

`radio_epoch` 是非零 u32 CSPRNG：每次服务端 boot，以及 Wi-Fi 断线后重新获得信道并重绑
ESP-NOW 时都重新抽取，若等于上一 runtime 值则重抽；同一 radio incarnation 内固定。圆屏
只在 server_id 相同且 beacon/PONG 的 epoch 与已保存值不同（或本地刚重启无值）时启动新
bootstrap incarnation；同 epoch 的重复 RECOVERY_BEACON 不重置窗口。epoch 只是重启提示，
不是认证，仍须 LMK LINK 握手。

服务端连接 AP 后不扫描、不跳频。圆屏先尝试保存信道一至两秒，再扫描本地区允许的
信道。RECOVERY_BEACON 只是候选提示，只有加密 LINK_HELLO/LINK_WELCOME 成功才
确认服务端身份。

DISCOVERY、RECOVERY_BEACON 和全部首次 PAIR_* 使用 session_id=0。已持久化 binding 的
圆屏即使不知道服务端新 session，也可用 session_id=0 发送 LMK 加密的 LINK_HELLO；
服务端以来源 MAC、binding_id 和 LMK 认证，不要求旧 session 匹配。LINK_WELCOME 使用
bootstrap 可靠窗口返回新非零 session_id、server sender_epoch、radio_epoch、选定版本、
当前信道和 current_state_version。
每一个通过验证的新 client_link_nonce 都必须抽取新的非零 CSPRNG session_id，并拒绝等于
该 binding 当前 active/proposed/30 秒 bootstrap tombstone 中任一 ID；v1 不持久化“终身用过
的所有 u32”，因此不声称数学上的永久不重复。`link_confirmation` 只绑定双方 nonce 与
new_session_id；server current-session epoch 由 LMK 加密的 WELCOME.body 认证，client
current-session epoch 由首个 LMK 加密 CLIENT_HELLO.header 认证，两个 bootstrap header epoch
又各自只标识其可靠 incarnation。这四个 epoch 角色是分阶段认证，不存在一个包含“双方
sender_epoch”的单一 transcript preimage。nonce 防止旧 WELCOME/HELLO 接管。普通
current-session 帧头不携带 nonce，建链后的旧帧
隔离实际依赖隐含 binding/source MAC 与随机 `session_id+sender_epoch+frame_seq` 组合，因此
是 64 位以上概率唯一而非终身计数证明。即使服务端未重启，也不得有意沿用上一 active ID。
LINK_HELLO.last_session_id 只用于诊断和拒绝延迟帧，不是复用请求。

切换状态冻结为 BOOTSTRAP → WELCOME_PROPOSED → SESSION_CONFIRMING → SESSION_ACTIVE：

1. 圆屏收到有效 WELCOME 后保存 proposed session，但保留 bootstrap 窗口；立即发送
   session_id=0 的 LINK_ACK，再在 proposed session 中只发送 CLIENT_HELLO。
2. CLIENT_HELLO.link_confirmation 是
   SHA-256(client_link_nonce || server_link_nonce || new_session_id 小端) 前 8 字节。
3. 服务端收到覆盖 WELCOME frame_seq 的 bootstrap ACK 只标记 WELCOME_DELIVERED 并继续
   AWAIT_CLIENT_HELLO，绝不能单独发送 SERVER_HELLO，因为还没有 catalog/display/capability
   输入。收到具有正确 confirmation 的 CLIENT_HELLO（即使 standalone WELCOME ACK 丢失）
   才固定 client_session_sender_epoch、进入 SESSION_CONFIRMING，并据其 catalog roots 生成/
   重放唯一 SERVER_HELLO；此时只接受 CLIENT_HELLO、ACK/PING。
4. 服务端发送 SERVER_HELLO 后仍处于 SESSION_CONFIRMING。收到 standalone LINK_ACK，或
   任一语法/认证均合法的新 session 非 ACK 帧（包括 PING 或普通 RELIABLE 帧），其
   piggyback ack 覆盖 SERVER_HELLO 的 frame_seq 时，服务端必须先原子转 SESSION_ACTIVE、
   清 bootstrap 发送窗口，再裁决该帧（例如紧随其后的 JOIN_REQUEST）；未覆盖前的普通
   消息不 ACK、不交付，也不得只清 HELLO TX 槽。这样 HELLO ACK
   与 JOIN 重排或单独 ACK 丢失都只有一种结果。
5. 圆屏收到 SERVER_HELLO、发送对应 ACK 后进入 SESSION_ACTIVE；再保留 WELCOME 的
   nonce/session 缓存两秒。期间收到完全相同的 session_id=0 WELCOME 必须重新 ACK，不能
   重置新 session；字段不同则丢弃并重新 LINK_HELLO。

圆屏接受新 WELCOME 的 new_session_id 时，必须立即将所有旧 session 未终态命令标记为
ABANDONED_SESSION，从 TX/自动重试队列移除，并且绝不用新 request_id 自动重发。本地只临时
保留旧 request_id、command_type 和包含 expected_state_version 的 semantic request hash 作诊断。完成新 session 的完整快照、
SYNC_COMPLETE 与 SYNCED_WAIT_GATE/PONG 证明后，以 ConsoleView 中的余额、产权、阻塞事务和合法动作为唯一结果，
清除 ABANDONED_SESSION 记录并显示“旧操作结果已按服务器状态对账”，不猜测成功/
失败。若当前视图仍允许类似动作，必须由玩家再次明确操作并生成新会话请求。

因此 bootstrap ACK 丢失不会造成两端分叉。bootstrap 期间和新 session ACTIVE 后收到的旧普通
会话帧均静默丢弃，且永远不能进入命令队列。接收端不为旧 session 创建新的
session_id=0 可靠窗口；发送端由心跳/重试耗尽转入 LINK_HELLO。

正常心跳五秒，有活动 deadline 时两秒。两个周期无有效加密数据进入 SUSPECT，并严格按第
13.5 节在双方关 gate；三个周期或重试耗尽进入 OFFLINE。未到 OFFLINE 可用同一 session 的
SUSPECT_RECOVERY PING 证明连续性，落后则 RESYNC；达到 OFFLINE 后只能新 LINK、用 seat_token
发 SESSION_RESUME，服务端始终发送完整个性化快照，匹配的 SYNC_COMPLETE 后还须完成
SYNCED_WAIT_GATE/PONG 才能恢复操作。P7 必须覆盖 client→server 与 server→client 各自单向丢包：
轨迹从普通 HEARTBEAT 自动得到 SUSPECT_RECOVERY_REQUIRED 并收敛，不能假设双方同时发现；双方
各自在观察到 SUSPECT/新 generation 时必须立即关闭本地 gate。异步单向网络允许尚未收到远端
撤销信号的圆屏短暂显示 stale UI，但服务端 gate 已关闭，任何命令必须在 watermark 前被
SERVER_BUSY 拒绝，且首个该错误或 HEARTBEAT disposition=2 必须令圆屏禁 UI/进入 kind2，不能
产生权威副作用。还必须覆盖旧 generation 的更高迟到 PING、迟到旧 PONG、相同 ping/body 仅
更新 piggyback ACK 的合法重传，以及同 ID HEARTBEAT 初次获 disposition=0 后服务器进入新
SUSPECT、重复该 PING 必须返回当前 server_gate_generation/disposition=2：旧消息绝不能复开
服务端 gate；圆屏一旦已观察较新 generation 或作废 outstanding tuple，也绝不能被旧 PONG 复开。

服务端 Wi-Fi 重连后重新读取信道、恢复 NVS peer、使所有旧链路会话失效，并按上述
session_id=0 的 LINK_HELLO 逐 peer 引导重连；每个成功 WELCOME 各自分配新 session_id。
旧信道和旧普通会话帧不再有效。

## 15. 稳定错误码

### 15.1 协议错误

| 代码 | 名称 |
| ---: | --- |
| 0x0101 | MALFORMED_FRAME |
| 0x0102 | PROTOCOL_MISMATCH |
| 0x0103 | UNSUPPORTED_MESSAGE |
| 0x0104 | UNSUPPORTED_CAPABILITY |
| 0x0105 | LENGTH_MISMATCH |
| 0x0106 | FRAGMENT_INVALID |
| 0x0107 | MESSAGE_TOO_LARGE |
| 0x0108 | REASSEMBLY_TIMEOUT |
| 0x0109 | CRC_MISMATCH |
| 0x010A | REPLAY_DETECTED |
| 0x010B | UNENCRYPTED_FORBIDDEN |
| 0x010C | SESSION_MISMATCH |
| 0x010D | VERSION_GAP |
| 0x010E | PEER_DESYNC |

### 15.2 配对和会话错误

| 代码 | 名称 |
| ---: | --- |
| 0x0201 | PAIRING_CLOSED |
| 0x0202 | AUTH_FAILED |
| 0x0203 | PEER_TABLE_FULL |
| 0x0204 | SEAT_UNAVAILABLE |
| 0x0205 | BINDING_REVOKED |
| 0x0206 | RESUME_DENIED |
| 0x0207 | SERVER_BUSY |
| 0x0208 | UNSUPPORTED_CONTENT |
| 0x0209 | ACTIVE_ROOM |
| 0x020A | RESET_TOKEN_INVALID |
| 0x020B | UNSUPPORTED_CAPABILITY |
| 0x020C | ALREADY_BOUND |

### 15.3 业务错误

| 代码 | 名称 |
| ---: | --- |
| 0x1001 | UNAUTHORIZED |
| 0x1002 | WRONG_PLAYER |
| 0x1003 | STALE_STATE |
| 0x1004 | DEADLINE_EXPIRED |
| 0x1005 | ACTION_NOT_ALLOWED |
| 0x1006 | INSUFFICIENT_CASH |
| 0x1007 | ASSET_CHANGED |
| 0x1008 | REQUEST_ID_CONFLICT |
| 0x1009 | INVALID_ARGUMENT |
| 0x100A | NOT_YOUR_TURN |
| 0x100B | TRANSACTION_NOT_FOUND |
| 0x100C | TRANSACTION_CLOSED |
| 0x100D | PLAYER_BANKRUPT |
| 0x100E | SERVER_RECOVERING |
| 0x100F | PROJECTION_TOO_LARGE |
| 0x1010 | OPERATION_EXPIRED |
| 0x1011 | ROOM_MODE_FORBIDDEN |
| 0x1012 | STORAGE_SPACE_LOW |
| 0x1013 | BUILDING_SUPPLY_EXHAUSTED |
| 0x1014 | INTERNAL_LIMIT |
| 0x1015 | AUTH_ROTATION_BUSY |
| 0x1016 | OPERATION_CAPACITY |
| 0x1017 | REVISION_EXHAUSTED |
| 0x1018 | EVENT_HISTORY_GAP |
| 0x1019 | EXPORT_NOT_FOUND |
| 0x101A | EXPORT_NOT_READY |
| 0x101B | EXPORT_FAILED |

COMMAND_REJECTED 和已认证 PROTOCOL_ERROR 统一使用固定 24 字节错误体：

~~~text
body_schema           u8 = 1
error_code            u16 LE
severity              u8
retry_policy          u8
replayed              u8
request_id            u32 LE，无对应请求时为 0
transaction_id        u32 LE，无事务时为 0
current_state_version u32 LE
retry_after_ms        u32 LE
detail_code           u16 LE
~~~

severity：0 NOTICE、1 RETRYABLE、2 ACTION_REJECTED、3 SESSION_FATAL。retry_policy：
0 NEVER、1 RETRY_SAME_REQUEST、2 RESYNC_NO_AUTOREPLAY、3 RECONNECT、4 REPAIR。
RETRY_SAME_REQUEST 必须保持 session_id、sender_epoch、message_type、request_id、
header.state_version 和完整 payload 完全相同。STALE_STATE、
ASSET_CHANGED 使用 RESYNC_NO_AUTOREPLAY；
UNAUTHORIZED 在固定 24 字节错误体中按 binding 状态使用 REPAIR 或 RECONNECT；SERVER_BUSY 可
使用 RETRY_SAME_REQUEST 并设置非零 retry_after_ms，其他情况该字段为 0。危险命令在
重同步后不得自动重发。

RETRY_SAME_REQUEST 只允许用于“尚未预留未完成请求槽、尚未进入 CommandBus、也尚未推进
该 peer 幂等水位”的入口 SERVER_BUSY，以及 HTTP 预留前的 OPERATION_CAPACITY 或
AUTH_ROTATION_BUSY。它是非缓存瞬时响应，replayed 固定为 0；客户端必须
在发送任何更高 request_id 前按 retry_after_ms 重发完全相同请求。只要请求已预留/入队，
其成功或业务拒绝就是缓存终态，重复返回相同 code/detail 并设置 replayed=1，绝不能再返回
可同 ID 重评的 SERVER_BUSY。服务端必须在任何领域副作用前完成预留，因此不存在“执行了
一半但未缓存”的路径。

玩家命令入口唯一合法的非缓存 COMMAND_REJECTED(SERVER_BUSY,RETRY_SAME_REQUEST) 必须在
串行构造该响应时采样一次 Authority current_state_version，并令 header.state_version 与固定
错误体 current_state_version 逐值相等、replayed=0、severity=RETRYABLE、transaction_id=已解析
原命令 transaction_id、retry_after_ms 非零、detail_code=0。该物理响应的传输重试除 ACK 字段外逐字节
不变；客户端稍后按同一请求做新的业务重评时，若仍未能预留，服务端可重新采样并构造新的
SERVER_BUSY。不得使用 0、客户端 expected_state_version 或未初始化槽值替代这次采样。
P7 必须逐字节覆盖首次 SERVER_BUSY、同一物理响应重传，以及 Authority 进版后同 request 的
下一次非缓存业务重评，证明每个响应内部 header/body 相等而两次独立构造允许版本不同。
对玩家命令，固定槽预留、记录第 13.2 节完整 semantic hash、推进统一 high watermark 与入
CommandBus 必须处于同一入口临界区。已预留但尚未终态的同 ID/hash 重发只附着到原 pending；
可重放一次原 COMMAND_ACCEPTED 或只做传输 ACK，绝不能再次入队。预留前容量不足才可发送
上述非终态 SERVER_BUSY。

错误承载唯一映射：

- 0x0101–0x010E 仅在服务端收到已认证、可安全归属、方向合法且入站并非 PROTOCOL_ERROR
  的圆屏协议/幂等错误时由 PROTOCOL_ERROR 返回；明确包含第 13.2 节 PLAYER_COMMAND、
  JOIN_REQUEST、SESSION_RESUME 旧 ID 已越各自 watermark 但终态槽淘汰的 REPLAY_DETECTED。
  圆屏按各路径指定的 RESYNC 或 close/re-LINK 收敛。
- PAIRING_CLOSED、AUTH_FAILED、PEER_TABLE_FULL、ALREADY_BOUND 和配对阶段 SERVER_BUSY 只用
  PAIR_REJECT.error_code，且该提示不改变状态。
- 已认证 current session 的 CLIENT_HELLO/JOIN_REQUEST/SESSION_RESUME 等会话层请求中的
  SEAT_UNAVAILABLE、SERVER_BUSY、UNSUPPORTED_CONTENT、UNSUPPORTED_CAPABILITY 用
  PROTOCOL_ERROR，并回显相应 request_id；本条明确排除 0x1000–0x1013 玩家命令。
- PLAYER_COMMAND/JOIN_REQUEST/SESSION_RESUME 使用同一完整幂等键但 semantic hash 不同时，
  都用 current session 上 flags=0x11 的 PROTOCOL_ERROR 承载 REQUEST_ID_CONFLICT，request_id
  回显原命令/join/resume ID，retry_policy=RECONNECT；它是第 13.2 节会话致命、原槽不变的明确
  载体例外，不得使用 COMMAND_REJECTED。
- RESUME_DENIED 或 BINDING_REVOKED 用 SESSION_REVOKED 自身 schema；reason 映射第 13.5 节
  唯一矩阵，不再附加 24 字节 error body。SESSION_REVOKED 没有 retry_policy；客户端后继、
  REPAIR 与是否可自动重新加入只取该矩阵，不得从同名 error code 或实现默认值推断。
- 除上述“旧 ID/终态已淘汰”的 REPLAY_DETECTED 与“同 ID/异 hash”的
  REQUEST_ID_CONFLICT 两个协议层例外外，0x1000–0x1013 玩家命令的 admission SERVER_BUSY
  以及领域/业务错误始终都用 COMMAND_REJECTED；HTTP 同名错误用 JSON error envelope。
- SESSION_MISMATCH 数值仅保留给本地诊断和未来协议；v1 不在旧 session 或 session_id=0
  上回送它，而按第 14.2 节静默丢弃并由发送端重建链路。

未配对来源、认证失败、方向错误和无法安全解析/归属的帧静默丢弃或关闭所属 session。
只有服务端对满足上段全部门禁的圆屏可解析错误才返回 PROTOCOL_ERROR，防止无线放大和
错误回环。

## 16. 玩家圆屏接口适配

当前主分支尚没有可依赖的 PlayerConsoleTransport 实现；玩家屏任务必须先创建并测试
PlayerConsoleTransport、EspNowTransport、协议 codec 和权威视图 reducer，再开始真实
联调。其他开发分支中的同名草案不构成本规格的既有接口。实现必须满足：

- C++ enum 数值不得直接作为线协议消息号。
- targetName 改为稳定 tile_id，本地查名称，必要时使用有长度的 UTF-8 fallback。
- assetMask 改为 u16 资产 ID 列表。
- StateSnapshotApplied 扩充为完整玩家视图。
- request_id 与 sender_epoch 共同保证设备重启后的唯一性。
- 补丁必须严格连续；版本缺口冻结危险操作并请求快照。
- COMMAND_ACCEPTED 不能被映射为请求终态。
- 客户端断线时不得离线排队掷骰、购买、交易、付款或抵押。

玩家屏的 commissioning secret 与服务器来自同一个本地生成的 32 字节值。开发工具用
系统 CSPRNG 创建被 Git 忽略的 .local-secrets/commissioning-v1.bin，通过不回显内容的
串口 provisioning 流程分别写入两端 NVS；只有显式本地开发构建才允许使用各自被忽略的
secrets.local.h 回退。secret 不得作为命令行参数、测试向量、串口日志或编译宏值显示。
圆屏侧的 NVS namespace、key profile/version 和清除凭据流程必须在真实联调前完成测试。

服务器交付协议入口、字段映射、状态映射、信道扫描、配对和黄金向量文档。任务协调
时把入口路径发送给玩家圆屏开发会话，但会话 ID 不写入长期协议文件。

## 17. 网页测试台

### 17.1 HTTP API

使用 Arduino WebServer、普通 HTTP 和约 250ms 轮询。`/api/v1/*` 除回放二进制下载外都使用
UTF-8 application/json。POST body 最大 2,048 字节；未知字段、重复键、浮点金额、超范围整数
和无效 UTF-8 返回 400。平台层使用项目内有界 JsonWriter/WebCommandParser。

HTTP mutation 的语法/业务边界冻结：body>2,048 bytes 在 admission 前返回即时 413
INVALID_ARGUMENT；无效 UTF-8/JSON、重复键、schema 未知字段、错误 JSON 类型、非法 enum/字符串
格式，以及不依赖 live state 就能判定的静态范围/计数错误返回即时 400 INVALID_ARGUMENT。它们
都 non-cached、不预留 operation、不推进 request_high_watermark，网页修正 body 后复用同一
request_id。所有 mutation admission 对同时存在的故障使用以下唯一全序，禁止 handler 自选顺序。
步骤 1–2 可在锁外完成；进入步骤 3 前必须取得全局 `HttpAdmissionGate`，并连续持有到步骤 10
失败退出或原子 reserve/enqueue 完成，步骤 4 的 lookup 与步骤 10 绝不能分成两次临界区：

1. route/method 与注册 endpoint 匹配，然后检查原始 body cap；超 2,048 bytes 即 413；
2. 完成 UTF-8、JSON、top-level/envelope、endpoint 静态 schema、范围和 canonical hash；失败 400，
   只有恰好解析出一个合法 request_id 才在 envelope 回显它，否则回显 0；
3. 验证 bearer/admin namespace；失败 401；
4. 在该 admin namespace 查幂等键，优先级依次为 exact key+hash 重放、same key+different hash 的
   REQUEST_ID_CONFLICT、`id<=high` 但槽缺失的 OPERATION_EXPIRED、跳过 `high+1` 的
   INVALID_ARGUMENT；任一命中立即结束 admission；
5. 仅对真正的新 `high+1` ID 做 endpoint 特例：`/session/new` 当前不是 canonical NO_ROOM 即
   409 ACTIVE_ROOM；`/recovery/resume` 处于 reset_interrupted/revision_exhausted 或 NO_ROOM 即
   409 ACTION_NOT_ALLOWED；
6. 检查 health_overlay 对该 route 的门禁；禁止即 423 SERVER_RECOVERING；
7. 检查通用 expected_room_id/state_version fence；失败 409 STALE_STATE；
8. 检查已知 current/request mode 与 content status（包括 fault 非 test、session/new
   mode-policy/release_status）；失败 403 ROOM_MODE_FORBIDDEN；
9. 检查 device-wide next_operation_id；已达 exhausted sentinel 即 503 REVISION_EXHAUSTED；
10. 在同一 admission 临界区同时检查 generic operation 槽和 endpoint 专用槽容量；不足即 429
    OPERATION_CAPACITY。全部满足才原子预留槽、分配 operation_id、推进 request_high_watermark、
    保存首次 202 response snapshot 并 enqueue，然后返回 202；
11. dispatch 在任何候选或副作用前再次复核 room/state fence、health_overlay、mode/content，失败
    写入该 operation 的可重放 failed 终态；随后才做 controller、workflow、quote、ownership、
    deadline 等动态领域校验与副作用。

`POST /auth` 的 token/admin_session 轮换也必须取得同一 HttpAdmissionGate，并在锁内重验旧
namespace 是否还有 queued/running operation。mutation reserve 先赢时 auth 返回 409
AUTH_ROTATION_BUSY；rotation 先赢时先原子替换 token/admin generation、清理已允许清理的旧
namespace，随后取得 gate 的旧 mutation 在步骤 3 返回 401 且不推进旧 watermark。未来多连接/
树莓派实现不得用 Arduino 当前串行 handler 代替这条互斥。由此并发同 ID、相邻 n/n+1 或
auth-rotation 都只能有一个请求推进 high/分配 operation_id；后到者在同一 gate 内从步骤 3/4
重验并得到 exact replay、conflict、expired、gap 或 401 的唯一结果。

除第 4 步 exact hit（逐字节重放既有 cache）与成功的第 10 步外，步骤 1–10 都不新建 cache、
不预留 operation、不推进 request_high_watermark。修复条件后只有 request_id 仍恰等于该
namespace 当时的 `high+1` 才能复用：REQUEST_ID_CONFLICT 只能恢复原 body；
OPERATION_EXPIRED 永不重执行；gap ID 必须先提交缺失的 `high+1`；若 ACTIVE_ROOM 等条件是由
另一个 mutation 修复且它已消费原 `high+1`，原请求必须改用新的下一 ID。P0 HTTP contract test
必须覆盖单项 413、400、401、ACTIVE_ROOM、
ACTION_NOT_ALLOWED、423、STALE_STATE、403、503、429 与一个 202→failed，还必须覆盖
invalid bearer+malformed body、overlay+stale fence、mode+stale fence、operation-id exhausted+
capacity 四个组合，逐项证明固定全序，不得在不同 build 间漂移。

只有整份 body 已按 endpoint schema canonicalize、上述门禁与通用 fence
通过且固定槽预留成功后才推进 watermark/返回 202；随后依赖 Authority/runtime 的 controller、
workflow、quote、ownership、deadline 等动态/领域校验必须形成该 operation 的可重放 failed
终态并消耗 ID。

内置网页静态入口与 API 同源，v1 路由冻结为：`GET /` 与 `GET /index.html` 公开返回同一份
不含状态/secret/token 的静态 shell，`Content-Type: text/html; charset=utf-8`、
`Cache-Control: no-cache`、`ETag: "sha256-<index完整64位小写hex>"`；匹配 If-None-Match 返回
304 空 body。index 只能引用构建 manifest 中的
`/assets/<logical-name>.<内容完整64位小写sha256>.<ext>`，logical-name 只含小写 ASCII
字母/数字/连字符，ext 只允许 js/css/svg/png/woff2。asset MIME 固定为 JavaScript
`text/javascript; charset=utf-8`、CSS `text/css; charset=utf-8`、SVG `image/svg+xml`、PNG
`image/png`、WOFF2 `font/woff2`；v1 不做内容协商、不发送 Content-Encoding，
`Cache-Control: public, max-age=31536000, immutable`，ETag 使用同一
`"sha256-<URL中hash>"`，hash/嵌入 bytes 不符是构建失败。

未知静态路径返回 404 `text/plain; charset=utf-8` 与 `Cache-Control: no-store`，不做 SPA fallback；
静态路由与 `/api/v1/health`、`/api/v1/auth` 可在无 bearer 时访问，其他 `/api/v1/*` 必须当前
bearer。前端 API base 固定为相对同源 `/api/v1`，不得写死 IP/hostname，也不得把 PIN/token 注入
index 或 immutable asset。静态响应不使用 JSON error envelope；该 envelope 只属于 API。

mDNS hostname 固定为 gridopoly- 加 server_id 低 32 位的 8 位小写十六进制，例如
gridopoly-01ab23cd.local；串口和 /health 只显示该 hostname 与 IP，不显示 SSID。

JSON 中 u64 ID 使用固定 16 位小写十六进制字符串，128 位 token 使用 32 位小写十六
进制字符串，不使用 JavaScript Number。u32、金额和格子索引使用 JSON 整数。内容 ID
只允许 1–32 字节小写 ASCII 字母、数字和连字符。

启动时产生六位 PIN，只在串口打印一次。POST /api/v1/auth 唯一请求字段为 pin，必须是
六位十进制字符串；成功返回 token 和 JSON u16 整数 admin_session_id（1–65535，不是
hex 字符串）。仅 GET /api/v1/health 与 POST /api/v1/auth 公开；其余所有 endpoint 必须携带：

~~~text
Authorization: Bearer <32 lowercase hex characters>
~~~

token 使用常量时间比较，仅存 RAM，重启失效，同一时间只保留一个。连续五次 PIN 失败
后 auth 冷却 60 秒；日志不打印 PIN、token 或 Authorization。现有 token 还有 queued/running
operation 时，再次成功的 auth 也返回 409 AUTH_ROTATION_BUSY，不创建新 token。只有所有
operation 终态后才能替换 token 并清空旧幂等命名空间。设备重启时 RAM 队列和 token
一起消失；已 flush 的 COMMIT 由恢复流程裁决，不伪造运行中 operation。

`web_actor_ready` 是设备级 RAM bool，定义为“当前唯一 admin bearer token 已成功签发且当前
RuntimeHealthOverlay 允许网页领域命令”；不要求先完成某个隐藏 GET、WebSocket 或浏览器注册。
auth 成功、token 失效/轮换、启动恢复、health overlay 改变时都在 ProjectionGate 内重算；真正的
false↔true 变化增加 projection_revision。重启/显式 token 失效先清 false；合法 auth 轮换可在同一
临界区旧 token→新 token 而保持 true。所有 `/game/action` 仍必须逐请求携带当前 bearer，旧 token
即使曾令 WEB ready 也不能提交；不存在“ready 即永久授权”的旁路。

除 auth 外，所有有副作用 POST 固定为：

~~~json
{"request_id":73,"expected_room_id":"1020304050607080","expected_state_version":184,"payload":{}}
~~~

request_id 为 1–0xFFFFFFFF。HTTP 幂等键是 boot_id + admin_session_id + request_id；
幂等与 operation 共用 16 个固定槽，每槽 640 字节，其中规范化终态 result/error 最大 512 字节。
这里 boot_id 明确就是第 7.3 节 ingress_boot_id。另有 device-wide、RAM-only
`next_operation_id u32`，每 boot 从 1 开始且不因 auth 轮换重置；成功预留槽时在同一临界区
分配当前非零值后加一。0/0xFFFFFFFF 保留，同一 boot 绝不复用已淘汰 ID；达到 sentinel 时在
推进 request high watermark 前返回 503 REVISION_EXHAUSTED，等所有 operation 终态后安全重启，
不回绕。槽的完整身份是 `(ingress_boot_id,admin_session_id,operation_id)`，GET operation 还必须
由同一 bearer/admin namespace 访问。auth 轮换会使旧 namespace/URL 全部 401/404，但本 boot 的
ID counter 继续；重启使 token、槽和 boot namespace 同时失效，客户端必须丢弃旧 operation URL。
每个 admin_session_id 另保存 u32 request_high_watermark，初值 0。网页必须串行提交 HTTP 请求：
收到上一个 HTTP 响应（不必等 operation 终态）后，新变更 POST 才使用恰好
`request_high_watermark+1` 的 ID。服务端在完整解析信封并成功预留固定槽后先
推进 high watermark，再做 payload 业务校验；因此可解析的业务拒绝也会成为可重放终态。

相同键和规范化 body hash 在 queued/running/终态任一阶段都返回原 status/响应；相同键但
不同 body 返回 409 REQUEST_ID_CONFLICT。`request_id<=request_high_watermark` 但对应槽已被 LRU
淘汰时返回 410 OPERATION_EXPIRED，绝不重新执行；跳过 high_watermark+1 返回 409
INVALID_ARGUMENT。high watermark 接近 0xFFFFFFFF 时，必须等所有 operation 终态后重新
auth，不定义回绕。token 安全失效时才清空该命名空间。

已认证 mutation 的处理顺序必须是：完成静态解析/canonical hash 后，先按
`(ingress_boot_id,admin_session_id,request_id)` 查幂等槽，再检查当前 room/version fence。命中槽且
hash 相同，逐字节重放首次 admission 的 HTTP status 与 envelope 中
`state_version/server_time_ms/demo/result.operation_id/result.status`；该 response snapshot 在首次
202 时与槽一起保存，之后即使 operation 已终态、房间已 reset/new、mode 改变也不重采样。命中
但 hash 不同先返回 REQUEST_ID_CONFLICT；无槽但 ID 已在 watermark 后返回 OPERATION_EXPIRED；
只有真正的新 ID 才检查 current expected_room_id/state_version 和容量。这样 session/new/reset 的
丢失 202 不会因自身提交改变版本而挡住幂等重放。GET operation/status 等新 GET 仍按请求时采样
外层 envelope，并从固定 operation 槽读取内层状态。

`expected_room_id` 是 16 位小写 hex u64；canonical NO_ROOM 必须为 `0000000000000000`。
它是所有变更 POST 的跨局 ABA fence：admission 在预留槽/推进 high watermark 前检查一次，
dispatch 在产生任何候选或副作用前再检查一次，二者都必须逐字节等于当前 Authority room_id；
否则返回 STALE_STATE。唯一的前置错误优先级不是 fence 例外：idempotency miss 的
`/session/new` 在活动房间先按上表返回 ACTIVE_ROOM，`/recovery/resume` 在明确不可恢复 overlay/
NO_ROOM 先返回 ACTION_NOT_ALLOWED；若这些 endpoint 通过前置门禁，仍必须完整执行两次 fence。
即使 pairing/open 等操作主要改变设备级 runtime，也不允许忽略或固定为任意值；session/new 必须
期待 NO_ROOM，reset/recovery/export 必须期待其 payload 所指同一 room。
这样 reset 保留同一 admin token 时，旧网页延迟的 game/action、board/observe、seat、peer、fault
或其他 POST 也不可能在新局版本恰好相同时生效。

`expected_state_version` 同样适用于 auth 之外的每一个变更 POST，canonical NO_ROOM 时必须为 0；
不存在 runtime-only 隐式忽略例外。admission 在预留/推进 high watermark 前与当前 Authority
state_version 精确匹配，dispatch 在任何 RAM、NVS、文件、无线或候选副作用前再次精确匹配；
不符均为 STALE_STATE。pairing/open、fault、peer 管理和 replay/export 即使最终主要改变 adapter
runtime，也必须携带并通过这两次 fence。admission 后版本漂移会让已接受 operation 以可重放
STALE_STATE 终态失败，不能在新版本继续执行。

规范化 body 使用 UTF-8、对象键按 UTF-8 字节序、无无意义空白、整数最短十进制表示和
数组原顺序；hash 为完整 SHA-256，并覆盖外层 request_id、expected_room_id、
expected_state_version 与 payload。GET、POST /auth，以及无法从输入中唯一解析出合法 mutation
request_id 的即时错误，其 envelope request_id 固定为 0；一旦完整 top-level parser 已证明恰有
一个范围合法的 request_id，该 mutation 的 202、任何即时错误和幂等 POST 外层 request_id 都
逐值回显它，operation 槽内 origin_request_id 也固定为该值；GET /operations 外层仍为 0。不能
因错误发生在 payload/auth/fence 的不同阶段改成 0 或猜测部分 token。

所有 API JSON envelope 固定包含 api_version="1.0"、request_id、state_version、server_time_ms、
demo，并且恰有 result 或 error 之一，另一键不得出现。成功时使用 result；`demo` 是 JSON bool，仅当响应采样时当前 Authority room_lifecycle=ACTIVE 且冻结
room mode=demo 才为 true，NO_ROOM/CREATING/RESET_PREPARED 及 test/standard 都为 false，
health/auth/operation/status 也使用同一公式；已命中 mutation 幂等槽的重放例外使用上段首次
admission 已缓存值。失败时用互斥的 error；`error` 的键和 JSON 类型固定为
`{code:u16,detail_code:u16,severity:u8,retry_policy:u8,retry_after_ms:u32,
current_state_version:u32}`，code 是第 15.2 或 15.3 节适用的已注册数值而不是名称字符串；两表
的 u16 值全局不得重名或分配不同语义，severity/retry_policy 使用
第 15.3 节数值。只有 EVENT_HISTORY_GAP 额外且必须包含固定
`context={journal_base_event_id,current_last_event_id}`，二者均为 16 位小写 hex 字符串。其他
错误不得携带 context。

除 auth 外的变更 POST 只做有界解析和入队：接受时返回 202，以及 operation_id u32 和
status="queued"，不得在 HTTP handler 内等待规则、Flash 或无线终态。幂等重放返回同一
operation_id。浏览器通过 GET /api/v1/operations/{operation_id} 取得 queued/running/
succeeded/failed 及最终 result/error。queued/running 槽不可淘汰，终态槽按 LRU 淘汰；无空槽且无可
淘汰终态时，新的变更 POST 返回 429 OPERATION_CAPACITY，不预留、不推进
request_high_watermark、不入队；浏览器必须在发送更高 ID 前用相同 ID/body 延迟重试。已淘汰的 operation 返回
404 OPERATION_EXPIRED。下表的 result 指 operation 成功终态。

GET operation 的内层 result shape 始终是
`{status,origin_request_id,created_ms,completed_ms,result,error}`，六键不得省略；status 只取
`queued|running|succeeded|failed`。queued/running 要求 completed_ms=0 且 result/error 均为
null；succeeded 要求 result 为 endpoint 固定 object、error=null；failed 要求 result=null、error
为本节固定错误 object。终态 completed_ms 取实际本 boot u32 单调毫秒，允许在开机第0毫秒为0，
是否完成只由 status 判别，不能用数值0反推 pending；completed 不早于 created（按小于2^31
窗口比较）。

`/board/observe` 和 `/fault` 还要在上述同一 HTTP admission 临界区预留各自的固定池槽；
operation 槽、专用槽任一不足都在推进 request_high_watermark 前返回 429
OPERATION_CAPACITY。相同幂等请求复用原 reservation/operation_id；dispatch 业务拒绝时释放
reservation。board pool 为八个 runtime reservation，对应第 9 节八个持久 ACTIVE 槽；成功
COMMIT 后 reservation 转为该 Authority 槽，重启从 Authority 重建占用。

`/replay/export` 也必须在这一个 admission 临界区原子预留唯一 export registry 槽为内部
RESERVED，并绑定 `(ingress_boot_id,admin_session_id,request_id,operation_id,room_id,
state_version)`；已有 RESERVED/CAPTURING/PREPARING/READY 时在预留/推进 high watermark 前返回
429 OPERATION_CAPACITY。未过期 FAILED 可在同一临界区按第 19.3 节淘汰后预留。相同幂等键/hash
只复用原 reservation/operation_id；dispatch 才把 RESERVED 变 CAPTURING。room/version 复核、
LineageBarrier 或 pin 安装前任一失败都把 operation 写入可重放 failed 终态并释放 reservation，
不能留下幽灵槽；捕获成功则原槽转 PREPARING。两个并发 POST 绝不能都先收到 202 再竞争后台槽。

fault pool 固定八槽，槽态 EMPTY/RESERVED/ACTIVE/DRAINING，只存在 RAM 且重启、room reset
或恢复入口全部清空；不得淘汰 ACTIVE/DRAINING fault。`next_fault_id u32` 每个 boot 从 1 开始，激活时分配并加一，
0 不分配、0xFFFFFFFF 为 exhausted sentinel，达到后拒绝并要求安全重启，绝不回绕。duration_ms
大于 0 的 fault 到 expires_ms 停止新匹配；duration_ms=0 表示只抽样下一次匹配的帧/存储操作。
八个 RESERVED/ACTIVE/DRAINING 全满同样在 admission 返回 429 OPERATION_CAPACITY，而不是先
返回 202 再无界排队。

Endpoint 契约：

| 方法与路径 | 请求或 query，未列字段一律拒绝 | result |
| --- | --- | --- |
| GET /api/v1/health | 无；公开 | build_id≤40、uptime_ms、flash_bytes、free_heap、wifi.connected/channel/ip、espnow.peer_count/encrypted_capacity、storage.state/free_bytes、admin_projection_schema_hash、http_events_schema_hash；不得含 SSID 或凭据 |
| POST /api/v1/auth | pin | token、admin_session_id |
| GET /api/v1/state | projection=admin 或 seat；seat_id 仅 seat 时 1–6；section；cursor u16；limit 1–64；首请求可带成对的 since_version u32 与 since_projection_epoch 16 位 hex；续页必须带成对的 at_version u32 与 projection_epoch 16 位 hex | at_version、projection_epoch、section、items、next_cursor 或 null、unchanged |
| GET /api/v1/events | after 为 16 位 hex event_id，缺省 0；limit 1–128，缺省 64 | items、next_after、has_more；history gap 规则见下文 |
| GET /api/v1/operations/{operation_id} | operation_id u32 非零 | 固定六键 status、origin_request_id、created_ms、completed_ms、result、error |
| POST /api/v1/session/new | map_id、map_revision u16、pace=fast/normal/slow、player_count 1–6；demo 必须 1，test/standard 必须 2–6；mode=test/demo/standard，按第 10.2 节校验 content release_status；seed 为 16 位 hex、disconnect_policy | room_id、state_version=1、content_manifest_sha256、map_id、map_revision；完整 manifest 通过分页 GET state/content 取得 |
| POST /api/v1/session/start | room_id、ready_player_mask u8；外层 expected_state_version 必须精确匹配当前房间 | session_phase=IN_GAME、first_player_id u8、state_version u32 |
| POST /api/v1/session/reset | operation=prepare/commit、room_id；commit 另带 reset_token | prepare 仅返回 reset_token/expires_ms；commit 仅返回 room_id=0、storage_generation、state_version=0 |
| POST /api/v1/recovery/resume | room_id、recovery_generation u32、acknowledge_rollback bool、lost_range_known bool、lost_from_version u32、lost_to_version u32 | room_id、storage_generation、state_version、resumed_deadline_count u8 |
| POST /api/v1/game/action | seat_id 1–6、expected_control_epoch u32、command 为 0x1000–0x1013 的规范名称、command_payload 为第 12.3 节对应 JSON 字段 | terminal_message_type u16、transaction_id u32、committed_state_version u32、outcome_code u16、primary_id u16、secondary_id u16、amount i32；详细状态改用 GET |
| POST /api/v1/board/observe | transaction_id u32 非零、observed_position u8、result=correct/wrong/timeout/no_response、delay_ms 0–60000 | 接受的 observation_id |
| POST /api/v1/pairing/open | duration_ms 1000–60000 | pairing_window_id、expires_ms |
| POST /api/v1/peers/assign | binding_id、expected_slot_generation u32、seat_id 1–6；必须已有同 binding 的 PendingJoin | assignment_version、correlation_request_id |
| POST /api/v1/seats/controller | seat_id 1–6；controller=web/bot/empty；bot_policy=conservative/balanced/aggressive 仅 bot 必填 | seat_id、controller_type、bot_policy_id、state_version |
| POST /api/v1/seats/takeover | seat_id 1–6、assignment_version u32、action=web/bot；bot_policy 仅 bot 必填 | seat_id、controller_type、control_epoch、state_version |
| POST /api/v1/peers/unpair | binding_id、expected_slot_generation u32、confirm_binding_id，二者必须完全相同 | unpair_id、local_release_state=committed |
| POST /api/v1/peers/force-forget | operation=prepare/commit、slot_id 0–5、expected_slot_generation u32、confirm_binding_id 16 位 hex、confirm_device_id 16 位 hex；commit 另带 forget_token | prepare 返回 forget_token/expires_ms；commit 返回 slot_id、slot_generation、state=empty、assignment_version |
| POST /api/v1/fault | fault_type=drop/delay/duplicate/reorder/disconnect/storage_fail、target=server 或 binding_id、duration_ms 0–60000、rate_permille 0–1000、action_delay_ms 0–5000；组合约束见下文 | fault_id、expires_ms；仅 test room |
| POST /api/v1/replay/export | room_id | operation 终态才返回 16 位 hex export_id、status=preparing；初始 HTTP 只按通用 202 operation admission 返回 |
| GET /api/v1/replay/export/{export_id}/status | export_id 必须为 16 位 hex | §19.3 固定的完整 status result |
| GET /api/v1/replay/export/{export_id} | export_id 必须为 16 位 hex；offset u32、limit 1–4096 | READY 时 application/octet-stream 分块 |

GET events 只读取当前 active journal，不跨读 inactive bank；长期历史只能走 replay export。
令 B=active journal.header.base_event_id、L=Authority 当前 last_event_id。`after<B`（包括轮换后
默认 after=0）返回 410 EVENT_HISTORY_GAP，context 精确给 B 与 L，客户端以 after=B 重开分页；
该错误固定 severity=2、retry_policy=4 REPAIR、retry_after_ms=0、detail_code=0。
`after>L` 返回 400 INVALID_ARGUMENT；`B≤after≤L` 才返回 event_id>after 的当前 journal 项。
若 B=L，after=B 合法返回空 items、next_after=B、has_more=false。轮换在 NVS meta 激活后原子
改变 B，已开始但尚未完成的旧分页下一请求会明确得到 gap，绝不能静默从新 base 拼接历史。

每个 GET events 响应在 StorageActor 锁内原子采样
`(active_bank,storage_generation,B,L,prefix_end_offset)`，并在同一临界区为该 bank 安装有界
JournalReadLease；只读取截至 prefix_end_offset 的完整记录前缀，采样后的 append 不进入该响应，
has_more 也只相对采样时的 L 计算。轮换可激活另一 bank，但轮换、reset、session/new、recovery
promotion 和 provisioning repair 在把被租用 bank 作为 destination 并覆盖、截断或 rename 前必须等待
lease 释放。lease 直到响应最后一字节发送完、主动 abort 或 socket timeout 才释放；下一次分页
重新采样当前 active bank，因此可能按上段明确返回 EVENT_HISTORY_GAP，绝不能跨响应偷拼两个前缀。

HTTP 投影的唯一生成源必须是 `GameData/schemas/admin-projection-v1.json` 与
`GameData/schemas/http-events-v1.json`；二者使用本规格 canonical JSON 规则，SHA-256 由 /health
公开，C++ codec、TypeScript 类型和网页 reducer 必须由同一文件生成。GET state 的 result 固定为
`{at_version,projection_epoch,section,unchanged,items,next_cursor}`。

服务端维护 RAM-only `projection_revision u32`：启动恢复并构造首个可读投影后从 1 开始；每次
Authority live pointer 发布，以及 ready_player_mask、health_overlay、online、command_enabled、
session_state、RSSI、retry_count、channel 或任何其他 GET state 可见的 runtime 值/数组形状变化时，
都必须在同一 ProjectionGate 发布临界区加一。0 和回绕均禁止；即将溢出时停止新写、令旧 admin
token 失效并安全重启。`projection_epoch` 固定为 16 位小写 hex
`hex8(ingress_boot_id) || hex8(projection_revision)`；重启后的新 boot_id 使旧 epoch 无效。

首请求必须 `cursor=0` 且省略 at_version/projection_epoch；二者的“缺省”而非数值 0 是首请求
sentinel，所以 canonical NO_ROOM 的合法 `state_version=0` 可无歧义返回。服务端在一次
ProjectionLease 中采样真实 state_version 为 at_version、当前 projection_epoch 和不可变
Authority 指针，并复制有界 RuntimeProjectionOverlay。续页必须同时携带上一页的 at_version 与
projection_epoch；任一缺失、多余或格式错误返回 400，任一不等于当前 live 值返回 409
STALE_STATE，本 API 不保留历史 state/projection。cursor 是所选 section 稳定排序数组的零基
ordinal，next_cursor 是下一 ordinal 或 null；cursor 大于 item_count 返回 INVALID_ARGUMENT。

since_version/since_projection_epoch 也只能在首请求中成对出现；只有两者都精确等于当前采样值
才返回 unchanged=true/items=[]/next_cursor=null。只比较 state_version 永远不足以声明 unchanged；
省略这对字段时返回当前完整页。ProjectionLease 在一次响应的计数与编码两遍期间钉住 Authority
只读 buffer，并使用同一份 runtime overlay copy；CommandBus 只在下一次复用该旧候选 buffer 前
等待 lease 释放，不能改写被钉住的数据。这样响应内两遍和响应间 continuation 都不会混合两个
运行时视图。

各 section 的 sort key 与始终存在的基础字段冻结为：

| section | sort key | item 基础字段；不适用值按 schema 用 null/0，不得省略 |
| --- | --- | --- |
| summary | 单项 | room_id、room_lifecycle、phase、mode、map_id、map_revision、pace、state_version、round_number、active_player_id、turn_phase、blocking_transaction_id、disconnect_policy、ready_player_mask、ready_mask_scope、last_event_id、content_manifest_hash、storage_generation、health_overlay、recovery、current_roll |
| players | seat_id | seat_id、player_id、name、color_id、avatar_id、controller_type、online、command_enabled、cash、position、in_hold、bankrupt、rank、assignment_version、control_epoch、binding_id、player_revision、held_card_count |
| assets | asset_id | asset_id、tile_position、asset_kind、group_id、owner_player_id、price、mortgage_value、mortgaged、level、rent_l0、rent_l1、rent_l2、rent_l3、rent_l4、rent_landmark、asset_revision |
| transactions | transaction_id | transaction_id、kind、stage、workflow_revision、owner_player_id、counterparty_player_id、subject_id、amount、deadline_ms、allowed_actions、visibility、details |
| actions | seat_id | seat_id、player_id、control_epoch、available_actions、asset_actions、private_cards |
| peers | slot_id | slot_id、slot_generation、binding_id、device_id、binding_state、seat_id、player_id、session_state、rssi、retry_count、protocol_major、protocol_minor、channel |
| content | (content_kind, content_id UTF-8 bytes, revision) | content_kind、content_id、revision、sha256、status、display_name |

u64、SHA-256、金额、枚举和 bool 分别使用 16 个 hex 字符、64 个 hex 字符、JSON integer、schema 小写 ASCII
名称和 JSON true/false 规则。projection=seat 时 schema 只把 transactions.details 私有字段及非
本人 peer item 按固定 redaction mask 置 null，数组形状/sort key 不变；admin 不脱敏但仍不得含
token/密钥/SSID。section=all 要求 cursor=0，将 items 固定为一项
`{summary,players,assets,transactions,actions,peers,content}`，每个值都是上述已排序数组，next_cursor=null，
并继续受 16KiB 计数预运行上限。

actions.available_actions 是第 13.7 节 0x06 的 JSON 等价数组，每项固定
`{command_type:u16,transaction_id:u32,subject_id:u16,minimum_amount:i32,
maximum_amount:i32,deadline_ms:u32,action_flags:u16}`；asset_actions 只列该 seat 当前 mask 非零的
资产，按 asset_id 排序，每项固定
`{asset_id:u16,self_action_mask:u8,mortgage_proceeds:i32,unmortgage_cost:i32,
next_build_target_level:u8,next_build_cost:i32,sell_options:[{target_level:u8,
quoted_proceeds:i32}]}`，sell_options 按 target_level 排序。private_cards 按 instance ID 排序，每项
固定 `{card_instance_id:u16,card_catalog_id:u16,deck_id:u8,tradable:bool}`。三数组均由 Core 对
该 seat 构造 ConsoleView 后直接投影，网页不得重算规则；`/game/action` 只允许当前 token 控制的
WEB seat，并继续以同一 control_epoch/room/version 复核。

projection=admin 可见六座完整 actions；projection=seat 仍保留全部配置座 item/sort shape，但非
本人 item 的三个数组固定为 null，本人 item 完整。summary.current_roll 固定为
`{valid:bool,roll_kind:u8,die1:u8,die2:u8,total:u8,is_doubles:bool,
transaction_id:u32,state_version:u32}`，语义和清零时机与第 13.7 节 CORE 完全相同，不得从可能已
轮换的 events 猜测。actions/current_roll 的任一 runtime-visible 改变遵守同一 ProjectionLease 和
projection_revision 规则。

summary.recovery 是始终存在的固定对象
`{generation:u32,rollback_required:bool,lost_range_known:bool,lost_from_version:u32,
lost_to_version:u32,resume_allowed:bool}`。正常 ACTIVE、NO_ROOM 时六字段固定为 0/false；
RECOVERING 时逐值来自第 19.2/19.4 节当前恢复 episode。resume_allowed 只在普通
RECOVERING 或 RECOVERING_ROLLBACK_REQUIRED 且全部 resume preflight 可完成时为 true；
RESET_INTERRUPTED、REVISION_EXHAUSTED、NO_ROOM 一律为 false。该对象属于
RuntimeProjectionOverlay，任一字段变化都在同一 ProjectionGate 增加 projection_revision，故
`/recovery/resume` 所需 generation、回滚确认与丢失范围都来自同一个不可混页的 projection_epoch。

v1 不提供独立 `GET /api/v1/peers`；网页必须使用
`GET /api/v1/state?section=peers&projection=admin`，从而继承同一 ProjectionLease、
at_version/projection_epoch、稳定排序和脱敏规则。

summary.health_overlay 是固定小写 JSON enum：`none`、`recovering`、
`recovering_rollback_required`、`reset_interrupted`、`revision_exhausted`。正常值只能是 none；
none 仅表示 health gate 不额外阻塞，实际写仍须通过 room_lifecycle/session_phase/endpoint 门禁。
非 none 时全部玩家、机器人和领域写关闭；四种 non-none 都允许 public static/health、auth、只读
state/events/operations、replay status/download、replay/export POST，以及同一 room 的两阶段 reset。
recovering 两值在 exhaustion preflight 通过时另允许 recovery/resume；reset_interrupted 与
revision_exhausted 固定禁止 recovery/resume，只能走上述诊断/export/reset 集合。不得用空串、
null 或大写内部枚举投影。

GET events 的每个 item 固定为
`{event_id,state_version,transaction_id,event_type,event_name,event_flags,actor_kind,actor_id,received_at_ms,fields,unknown_field_ids}`，
按 event_id 升序。fields 的 key 是 domain-events-v1.json 中的稳定小写 field_name，顺序按
field_id；FieldTLV U8/U16/U32/I32→JSON integer，U64→16 位 hex，BYTES→小写 hex，UTF8→string，
RECORD_LIST→按其 item schema 的 object array。next_after 是最后返回 event_id（空页回显输入
after），has_more 只表示当前 active journal 尚有更大 event_id；未知 optional field 跳过且在
`unknown_field_ids` 数组列出，未知 critical 已在读取层拒绝 lineage，不能生成半项 JSON。

fault 只作用于 adapter/test harness，不消费规则 RNG，也不写权威 game event。drop/delay/
duplicate/reorder 的 eligible unit 是经过 codec 生成或验证、尚未交付下一层的单个 ESP-NOW
物理帧；target=binding_id 匹配该 binding 的双向帧，target=server 匹配全部 peer。storage_fail
只允许 target=server，其 eligible unit 是单线程 StorageActor 的一次 open/write/flush/rename 或
NVS commit。fault 激活时固定 `fault_boot_id=ingress_boot_id`，并把 match_sequence 初始化为 0；
每遇一个 eligible unit 先严格加一，因此首个 unit 使用 1，再计算
`SHA-256(ASCII "fault-v1" || fault_boot_id u32 LE || fault_id u32 LE || match_sequence u32 LE)`
前四字节小端值 `r`，当 `r % 1000 < rate_permille` 才命中。0 永不命中，1000 全命中；同一
unit 按 fault_id 升序只由第一个命中的 fault 处理，之后 fault 不再观察该 unit。sequence 已为
0xFFFFFFFF 时在观察下一 unit 前停止该 fault、记录 REVISION_EXHAUSTED 并按已有 held/copy 决定
EMPTY/DRAINING，绝不回绕。

fault 黄金向量固定 `fault_boot_id=0x11223344、fault_id=1、match_sequence=1`：preimage hex 为
`6661756c742d7631443322110100000001000000`，SHA-256 为
`30dfff9c15352fa8684a4ddf7f601232bab52565944786909a9aa0cca748f085`，前四字节小端
`r=0x9cffdf30=2634014512`、`r%1000=512`；rate_permille=513 命中而 512 不命中。

动作冻结为：drop 丢弃一次，action_delay_ms 必须为 0；delay 用每 fault 一个 250-byte 固定槽
保存帧并在 action_delay_ms=1–5000 后原样释放，槽忙时后续 unit 旁路；duplicate 先正常交付，
再在 action_delay_ms=0–5000 后额外交付恰好一个逐字节副本；reorder 的 action_delay_ms 必须
1–5000，先持有命中帧 A，若同 target/方向下一 eligible 帧 B 在期限前到达，则 B 旁路所有
fault 先交付、下一 RadioActor slot 再交付 A；否则期限到只释放 A。延迟/副本释放的稳定顺序为
`(release_ms,fault_id,match_sequence)`，不递归再过 fault 链。

disconnect 不按帧抽样，要求 rate_permille=1000、action_delay_ms=0：激活即关闭 binding 的
current session 并在 duration_ms 内拒绝其 LINK；target=server 则模拟 STA/ESP-NOW 失联，所有
session 失效，期限后按第 14.2 节生成新 radio_epoch 并重连。storage_fail 要求
action_delay_ms=0，按上述确定性抽样使命中的 storage primitive 返回注入失败。其他类型
target/rate/delay 组合不符返回 INVALID_ARGUMENT。duration_ms>0 是 fault 活跃寿命；为 0 时
drop/delay/duplicate/reorder/storage_fail 只对下一 eligible unit 做一次上述抽样，完成抽样后
立即停止新匹配（rate=0 因而是确定性无效果），disconnect 是立即断开且可立即开始重连。所有
hold/copy 槽都计入八槽 fault 固定预算。

“释放”必须服从排定动作生命周期：ACTIVE 到 expires_ms 或 one-shot 完成抽样时只停止新
匹配；若已有 delay/reorder held frame 或 duplicate copy 等待 release_ms，槽转 DRAINING，按原
`(release_ms,fault_id,match_sequence)` 完成释放后才变 EMPTY。不得因 fault 活跃期先结束而
丢帧、提前发送或让新 fault 复用 buffer。明确 room reset、进入 RECOVERING 或服务端重启是
测试 harness 的取消边界：它们丢弃所有尚未释放的 held/copy、清空八槽并记录脱敏诊断，之后
任何旧 release timer 都因 fault_id/slot generation 不匹配而无效。

上表每个 POST 的 result 列就是允许的完整紧凑 schema，不得附加玩家数组、交易列表、
卡牌文案或完整 content manifest。这些详情只通过分页 GET state/events/content 取得。规范化
result/error 的字节上限冻结为：

| POST 类别 | result 最大 UTF-8 字节 |
| --- | ---: |
| auth | 160 |
| session/new、session/reset、recovery/resume | 256 |
| session/start、game/action、fault、replay/export | 256 |
| board/observe、pairing/open | 192 |
| peers/assign、peers/unpair、peers/force-forget、seats/controller、seats/takeover | 224 |
| 所有 POST error 对象 | 384 |

JsonWriter 在入槽前使用最坏输入长度做计数预运行；超过本表上限是服务器 schema 错误，
操作失败为 INTERNAL_LIMIT，不截断 JSON。固定槽仍预留 512 字节，因此每个合法 POST 终态都可
在单槽内表示。GET /operations 的外层元数据在 8KiB HTTP 工作缓冲中现生成，不复制进槽。

/game/action 的 actor 由管理员 token 与 seat_id 共同推导，command_payload 不得自报
player_id；但管理员身份不绕过 effective controller。入口与 dispatch 都必须原子复核该座
controller_type=WEB、expected_control_epoch 精确等于当前非零 control_epoch 且 runtime WEB
gate ready。epoch 不符返回 STALE_STATE，座位不是 WEB 返回 WRONG_PLAYER，均不入 CommandBus。
BOT 只能由内部 BotScheduler 提交，REAL_CONSOLE 只能由其已绑定 ESP-NOW session 提交，
UNASSIGNED 不能提交。命令 JSON schema 由同一语义 schema 生成，字段名、范围和可选性必须与
第 12.3 节逐项一致。/fault 在非 test room 返回 403。

/session/new 只创建 LOBBY 和 `player_count` 个 UNASSIGNED 座位槽；API 的 `empty` 是该枚举
的显示名。房间创建时冻结 disconnect_policy；若为 BOT_TAKEOVER，v1 的
`takeover_bot_policy_id` 固定为 balanced-v1 并进入 ROOM_CONFIG、content manifest 和回放，
不从未声明的默认值推断。

真实屏完成配对并发送 PendingJoin 后才能由 `/peers/assign` 入座。LOBBY 中目标可为
UNASSIGNED/WEB/BOT；替换 WEB/BOT 时同一候选清除旧 bot pending/PRNG 和 runtime ready。
IN_GAME 只允许绑定由 unpair/force release 留下的 UNASSIGNED 座位，保留该玩家的现金、
位置、资产和回合身份，不得覆盖仍有 REAL_CONSOLE/WEB/BOT 的座位。一次座位 COMMIT 原子
消费 PendingJoin identity、写入 binding_id、递增 assignment_version、设置
REAL_CONSOLE/online=false、清 runtime gate，生成 SEAT_BOUND，并把相同 join_request_id 的
SESSION_ASSIGNMENT 终态持久到 journal；HTTP operation 只观察该 COMMIT 后返回，不制造第二
个领域终态。还必须按第 7.3 节把该已 flush Authority occupant 写入 ACTIVE slot mirror 并
读回；两步都成功后才清 PendingJoin、完成 HTTP operation 并发送 SEAT_ASSIGNED(NEW_JOIN)。
同 binding/room 已入座
时只走 REISSUE。

`/seats/controller` 只在 LOBBY 可用，可把未绑定真实屏的座位设为 WEB、BOT 或 UNASSIGNED；
BOT 必须选择冻结 policy，其他值不得携带 policy。目标已有 REAL_CONSOLE 时返回 409，不能
静默踢屏。切换作为普通 Authority COMMIT 增版并递增 control_epoch。

`ready_player_mask` 按第 13.5 节是运行时门禁：REAL_CONSOLE 要求 online 且已完成目标版本
同步；WEB 要求 controller_type=WEB、web_actor_ready=true 且该 seat 的 runtime command gate
没有被 recovery/reset 等 overlay 关闭；BOT 要求 policy/独立 PRNG 已初始化；UNASSIGNED 永不
置位。已有有效 token 时，controller COMMIT 新设为 WEB 的座位在 live swap 后即可按此公式置位，
不等待额外浏览器握手；token/overlay 变化会一次性重算所有 WEB seat 的 command_enabled 与 mask。
/session/start 要求外层 expected_state_version、room_id 和请求 mask 在 dispatch 点与
当前门禁完全相同，并且所有配置座位都有可用 controller。操作只建立一个候选副本：在副本中进入 SETUP，按第 7.3 节唯一 RNG
顺序完成两套洗牌、初始资金/供应、开局骰子和先手，生成 ROOM_SETUP_STARTED 与
GAME_STARTED 事件，最终设为 IN_GAME；全部不变量通过后，两事件、RNG 状态和完整
state_delta 作为一条 COMMIT，只增加一个 state_version。flush 失败、掉电或超限时 live state
仍为 LOBBY，且权威 RNG 未消耗。成功 swap 后才可触发快照轮换；快照失败不能
伪造未提交状态，已 flush 的 start COMMIT 仍可恢复。

graceful `/peers/unpair` 或远端合法 UNPAIR REQUEST 都先把原 BindingSlot 原子改为
UNPAIR_PENDING，并保存 `authority_release_state=PREPARED`、room/seat/player、旧
assignment_version 和 unpair_id；这是不可回退的 NVS revocation fence，写成后立即拒绝旧
binding 的全部游戏/session 命令。随后以 `(binding_id, old_assignment_version, unpair_id)`
为幂等身份经 CommandBus 做 SEAT_UNBOUND：清 binding/online/runtime ready、递增
assignment_version 使旧 token 失效，并在同一候选关闭/清零该 assignment 的 presence
instance、presence_transaction_id 与 workflow；LOBBY 置 UNASSIGNED，IN_GAME 保留经济状态并按第 18
节进入暂停或接管。COMMIT flush 后把 NVS release state 改为 COMMITTED，HTTP operation 此时
即可 succeeded；远端 ACKNOWLEDGED/FINAL 继续后台收敛。远端发起时也必须先完成本地座位
release COMMIT 才发业务 ACKNOWLEDGED，失败则不谎报完成。

启动、回滚恢复和 ROOM_RESUMED 前逐座比对 Authority 与六个 BindingSlot；权威座位只有在
存在完全匹配 binding_id/device_id/slot_generation 的 ACTIVE slot 时才可保留绑定，其他
UNPAIR/FORCE/TOMBSTONE/EMPTY/PENDING 或代际不符一律以 NVS 撤销为准。正常运行中的原
HTTP/UNPAIR ingress 以其既有 request 幂等重试 SEAT_UNBOUND，不使用 SYSTEM trigger_kind。
重启/回滚时不得向旧或残尾 lineage 先追加一条 release；第 19.4 节新 generation K 的首条
ROOM_RESUMED COMMIT 必须同时应用全部 NVS-wins seat repairs，并包含至多六个 SEAT_UNBOUND
加一个 ROOM_RESUMED event。K 激活后才把对应 NVS release state 提升 COMMITTED；若座位在
base 已解绑/assignment_version 已推进，只更新 NVS。修复完成前禁止写游戏命令。这里不宣称
NVS 与 journal 跨介质原子，而是用持久 intent 和统一 promotion 恢复。

`/peers/force-forget` 允许清 ACTIVE、PAIR_PENDING、UNPAIR_PENDING 或 tombstone。prepare
精确校验 slot_id/generation；有 binding 时 confirm_binding_id 必须精确匹配，无 binding 时为
16 个零，confirm_device_id 始终必须匹配当前 device_id，返回绑定 boot/admin session/slot/
generation/occupant/room version 的 128 位 token，30 秒失效。commit 再次回显全部字段和
token；成功验证后先把 slot_generation 加一并原子写 FORCE_REVOKE_PENDING、旧 occupant 和
authority_release_state=PREPARED，再执行同一幂等 SEAT_UNBOUND（若未入座则跳过）。座位
COMMIT flush 后标记 release COMMITTED；persistent LMK 继续留在 FORCE_REVOKE_PENDING，
先删除易失 runtime peer，再用一次原子 NVS 写同时清 persistent LMK/occupant 并置 EMPTY、
保留新 generation。若在两步间掉电，启动可从仍在 NVS 的 LMK 临时重装 peer、依据 revoke
fence 拒绝全部消息并继续清理，不会卡在诊断。HTTP 只在 EMPTY 读回验证后成功；旧 occupant
未入座时 assignment_version 返回 0，已入座则返回 SEAT_UNBOUND 后的新值，无 active room 时
token 绑定的 room/version 都取 0。任一点掉电都从 FORCE_REVOKE_PENDING 继续，
绝不能恢复旧授权。该显式操作放弃远端三阶段收敛；默认 graceful unpair 永不自动升级为它。

`/seats/takeover` 只在 IN_GAME 接受，且目标必须是 offline REAL_CONSOLE 或 UNASSIGNED；
精确校验 expected_state_version、assignment_version，且该座没有已入队实体命令。
`web`/`bot` 是本局永久接管：同一 COMMIT 从座位解除旧
binding、关闭并清零 presence instance/transaction/workflow、递增 assignment_version 使 token
失效、递增 control_epoch、设置 WEB/BOT；bot 同时
初始化所选 policy PRNG。旧 BindingSlot 保持 ACTIVE，但必须在 Authority COMMIT flush 后按
第 7.3 节清零 occupant mirror 并读回，才变为 PAIRED_UNSEATED；若设备重连，只能从
SERVER_HELLO 得到 PAIRED_UNSEATED。提交边界若仍有尚无 drain 的旧 current session，则按
第 13.5 节为它建立 SESSION_REVOKED(ADMIN_UNSEATED) drain；已有 drain 必须逐字节保留并继续
自身收敛，已无 session 则不补造帧；任一情况
都不能自动抢回。想恢复实体控制的唯一 v1 流程是先
由管理员把该座明确释放为 UNASSIGNED（游戏结束/reset，或后续协议修订），再由新
PendingJoin `/peers/assign`；本局接管不可逆，避免定义隐藏 token 复活路径。

/recovery/resume 只允许 room_lifecycle=ACTIVE 且通过 exhaustion preflight 的 RECOVERING；
RESET_INTERRUPTED、REVISION_EXHAUSTED 与 NO_ROOM 对 idempotency miss 的新 ID 都按第 17.1 节
优先于通用 room/version fence 固定即时返回 HTTP 409 ACTION_NOT_ALLOWED，non-cached、
no-watermark。普通重启恢复要求 acknowledge_rollback=false、
lost_range_known=false 且丢失范围均为 0；RECOVERING_ROLLBACK_REQUIRED 必须
acknowledge_rollback=true，并精确回显网页恢复摘要中的 lost_range_known；为 true 时还须
逐值回显 lost_from_version/lost_to_version，为 false 时二者必须为 0，否则 409。成功操作
按第 19.4 节在同一候选状态内
重建 deadline 并提交 ROOM_RESUMED。管理员若不接受回滚，只能导出诊断或走两阶段
reset。

state 的 section 为 summary、players、assets、transactions、actions、peers、content 或 all，默认 summary。
数组分页首响应返回 at_version 与 projection_epoch，后续页必须携带同一对；任一权威状态或
投影可见 runtime 变化都返回 409 STALE_STATE。只有 since_version 与 since_projection_epoch
同时匹配才返回 unchanged=true。section=all 仅在编码不超过 16KiB 时
成功，否则返回固定六键 error 的 413 PROJECTION_TOO_LARGE；客户端使用本节已冻结的 section
列表分别查询，不另加 context；不得先分配超 16KiB 缓冲。
这里 16KiB 是逻辑 JSON 上限，不是 RAM 缓冲：JsonWriter 先对同一不可变 AdminProjection
做无输出计数，再用最大 1KiB 分块流式输出；两遍的 at_version/projection_epoch 必须一致。整个过程包含在
8KiB HTTP 工作预算内，不缓存完整 16KiB 字符串。
所有 JSON 响应同样以 16KiB 为硬上限；events 在下一项会越界时提前停止并设置
has_more=true，而不是截断单个事件。

回放导出每个主循环 tick 最多处理 2KiB，只允许一个任务，最大 256KiB，十分钟后删除。
下载响应带 Content-Range、X-Gridopoly-Export-SHA256 和完整长度，不能同步装入内存；sha256
是第 19.3 节整个 container（包括首尾长度与 file CRC）的 SHA-256。

HTTP 状态：200 成功 JSON/静态 GET；202 已入队；206 READY export chunk；304 静态 ETag 命中；
400 字段/范围/编码；401 认证；403 当前模式禁止；404 不存在；409 状态、
request_id、reset token 或事务冲突；410 已淘汰且绝不重执行的 operation/request；413 body/
投影/导出超限；423 RECOVERING；429 速率、
队列或任务限制；500 持久化/不变量；503 网络、存储或后台任务暂不可用。

API 响应 Cache-Control:no-store；静态资源使用内容哈希、ETag 和长期缓存。单来源每秒
20 个请求，auth 更严格；handler 必须有界且不能等待机器人、持久化、导出或无线终态。
网站是本地测试工具，不宣称普通 HTTP 是生产安全边界。

v1 服务端也不提供 `/simulation/mode`：auto/paused/step/speed 是网页 runner 的本地显示与提交
节奏，不改变服务器时钟、bot policy delay、业务 deadline、presence 或 pending due_ms。paused
只停止 runner 发新 HTTP 请求；step 按已收到的权威事件只提交一个下一动作并等待其 operation
终态；auto 重复该过程，遇移动时也必须显式调用 board/observe；speed_permille=100–10000 只
缩放 runner 自己两次请求间的等待。服务器端第 7.3 节 timer tuple 始终照常推进。

v1 服务端不提供 `/scenario/load`，也不创建 trigger_kind=16 continuation。固定场景是仓库主机
测试/网页静态资源中的脚本夹具，只能按本节公开 endpoint 逐个提交 session/new、controller、
session/start、game/action、board/observe 与 fault，并在每步校验 operation 终态、state_version
和事件轨迹；浏览器刷新只终止 runner，不回滚服务器。这样“场景直达/老化”复用真实入口，
不会引入一套未审计的权威状态注入协议。若未来需要服务端原子场景加载，必须升级 API、
注册 trigger 16、冻结持久 schema/事件轨迹后再实现。

### 17.2 页面功能

- 五种地图和正式/候选状态。
- 二至六座位及真实圆屏、网页、机器人类型。
- 环形棋盘、棋子、余额、产权、抵押、建筑和地标。
- 当前回合、阶段、骰子、合法动作和倒计时。
- 网页 runner 本地自动、暂停、单步、提交速度和固定种子展示；不伪造服务器暂停。
- 正确格、错误格、延迟和无响应。
- 配对、信道、RSSI、重试、丢包和版本状态。
- 事件日志、回放导出和浏览器端固定测试脚本入口。
- 帧丢失、重复、乱序、旧版本和存储错误注入。

## 18. 机器人和断线策略

保守、均衡和激进机器人均使用公开信息和自己的私有视图，通过 CommandBus 提交请求。
权威策略源为 GameData/bots/gridopoly-bot-policies-v1.json。每个 policy_id、revision、
整数权重、现金储备、竞价倍率、交易阈值和响应延迟都进入房间 content hash、快照和
回放；修改任一值必须产生新 revision，不能改变旧回放含义。

v1 参数冻结为：

| policy_id | reserve_cash_permille | max_bid_price_permille | trade_offer_permille | delay_ms | 净值/租金/成组/流动性/对手收益/破产风险权重 |
| --- | ---: | ---: | ---: | --- | --- |
| conservative-v1 | 250 | 700 | 800 | 700–1300 | 1000 / 800 / 500 / 1200 / -900 / -1500 |
| balanced-v1 | 150 | 1000 | 1000 | 450–950 | 1000 / 1000 / 800 / 700 / -1000 / -1000 |
| aggressive-v1 | 80 | 1250 | 1200 | 250–700 | 1000 / 1200 / 1200 / 300 / -1100 / -500 |

每个配置座位在 AuthorityState bot section 中持久保存 `next_decision_index u32`，新 room 一律
从 1 开始。每写一条 BOT_DECISION——包括 NO_ACTION——都分配当前值作为 decision_index，
并在同一 COMMIT 严格加一；controller/policy/revision 改变、断线接管和 bot 暂停都不重置，
只有新 room 重置。0 不分配，0xFFFFFFFF 仅作 exhausted sentinel；分配 0xFFFFFFFE 后写入
sentinel，下一次需要决策时进入 REVISION_EXHAUSTED/RECOVERING，绝不回绕。该值同时是
BotScheduler 的 request_id 和回放顺序依据，planner continuation 也必须逐事件消费，不能预留
一批后留下空洞。

候选唯一来源是 `Core::enumerateLegalActions(ConsoleView)`。每项线记录固定为
`command_type u16、transaction_id u32、subject_id u16、target_id u16、amount i32`；按
`(action_priority, command_type, transaction_id, subject_id, target_id, amount)` 升序。
action_priority 依次为：强制债务/付款、收租、限制区、掷骰/移动、购买/拍卖、建设/赎回/
交易、结束可选操作。枚举只产生协议的最小充分组合：拍卖为 minimum_next_bid 与合法 PASS；
建设/出售/抵押/赎回逐个使用视图明确列出的 asset/target；交易只含一个资产与现金并按
counterparty、asset_id 升序，报价为 resolved_price×trade_offer_permille/1000 向零截断；
不枚举任意资产子集。竞价若 minimum_next_bid 大于
`min(resolved_price×max_bid_price_permille/1000, cash-reserve_cash)`，只保留 PASS。

DEBT_RESOLUTION 的筹资枚举是独立的有界阶段机，每次只暴露首个非空阶段，不能把所有组合
合并：D1 为每个可售地产恰好一个“当前 level→下一个合法 level”的 SELL_BUILDING（≤28）；
D2 为每个未抵押、可转让资产一个“bot 给资产、收 policy 规范现金”的单资产
TRADE_CREATE（≤28）；D3 为每个可抵押资产一个 MORTGAGE_ASSET（≤28）；D4 只有
DECLARE_BANKRUPTCY。只有前一
阶段在当前状态没有合法动作才进入下一阶段；因此任一决策 candidate_count≤28，最终破产为 1。
各阶段仍按“每取得一元现金造成的 R 损失”及 canonical key 排序，成功出售、抵押或交易结算后
重算 shortfall 和阶段；义务已足额时由核心转入 PAY_NOW/结算，不再枚举筹资。

D2 的 counterparty 对每个 asset 固定选择 seat_id 最小、未破产、可合法接受且尚未被本债务
attempt fence 排除者，现金 quote 使用该 policy 的 resolved_price×trade_offer_permille/1000
向零截断且必须 >0。债务 workflow 持久一个共享
`debt_attempt_economic_hash[32]`、`debt_trade_attempt_count u8` 和至多 28 项固定数组
`{counterparty_id u8,asset_id u16,cash_quote i32}`。规范报价 REJECTED 或 EXPIRED 时，在恢复原
DEBT_RESOLUTION 的同一 COMMIT 追加该 tuple；相同 economic hash 下不得再次枚举它，数组满后
D2 视为空并进入 D3。material economic hash 只覆盖现金、资产 owner/mortgage/building、可交易
持有卡、当前义务、policy/content hash，不含 state/event/revision、时钟、presence 或 RSSI；只有
这些实质字段改变才清空 attempt array 并换新 hash。由此即使所有对手拒绝，单个不变经济状态
最多产生 28 次筹资报价，然后进入逐资产抵押，最终才到破产，不会无限重建相同交易。

自愿 TRADE_CREATE 的 envelope.transaction_id=0；债务直接筹资 CREATE 必须携带当前父 debt D，
用于 dispatch 精确复核 debtor/workflow revision。成功时另分配 trade transaction U；这是明确的
父子 ID 特例：成功 COMMIT、TRADE_OFFER 终态与 HTTP terminal transaction_id 都是新 U，同时把 D
从 DEBT_RESOLUTION 原子推进为 DEBT_WAIT_FUNDING_TRADE，持久
`funding_trade_transaction_id=U`；报价只允许债务人给出未抵押、可转让的单一资产并收现金，
避免在这个嵌套窗口再产生抵押接管 workflow。D 仍是唯一 blocking transaction，U 是
TRADES/DEADLINES 中唯一未关闭的非阻塞交易，二者不得互换或同时宣称 blocking。等待阶段 debtor
的 AVAILABLE_ACTIONS/allowed_actions 均为空，BotPlanner 不运行、不写 NO_ACTION、也绝不暴露
DECLARE_BANKRUPTCY。funding CREATE 的同一 COMMIT 自动设置 proposer/debtor confirmed bit；为避免
UPDATE 清确认后需要已冻结 debtor 重答，funding U 禁止双方 TRADE_UPDATE，返回
ACTION_NOT_ALLOWED。counterparty 只能对 U 发送 TRADE_CONFIRM（立即双方确认并 SETTLED）或
TRADE_REJECT，未响应则由 U 的 60 秒 deadline 置 EXPIRED。

U SETTLED 时同一 COMMIT 只完成交易、关闭 U 并把 D 切回 DEBT_RESOLUTION 后重算 cash/shortfall；
即使现金已足，也由随后 D 上的 PAY_NOW/付款裁决结算，不能在 U 中偷关债务。U REJECTED 或
EXPIRED 时同一 COMMIT 先按上段追加 attempt tuple，再关闭 U、清 funding ID 并把 D 切回
DEBT_RESOLUTION。快照的 BLOCKING_TRANSACTION_AND_CONTINUATION、TRADES、DEADLINES 与 bot
attempt section 必须共同保存 D↔U link；恢复不变量要求 stage 3 恰有一个匹配的 OPEN U/deadline，
stage 1/2 则没有 funding link。任何不完整组合使 lineage 无效，不能恢复后误选 D4。

后续 CONFIRM/REJECT/timeout 的 envelope 与 COMMIT transaction_id 都是 U；U 关闭并回到 debt 后，
PAY_NOW、SELL_BUILDING、MORTGAGE_ASSET 与 DECLARE_BANKRUPTCY 再继续使用 D。human debtor+bot
counterparty 成交、funding UPDATE 被拒、以及在 proposer confirmed/U deadline 持久后重启的三条
黄金轨迹必须断言 confirmed mask、D↔U link、终态 ID 与恢复后的唯一下一动作逐值一致。

无需选择的强制阶段，Core 必须只暴露唯一强制动作；多于一个是核心不变量失败，不评分。
BOT 控制地主遇到 RENT_CLAIM 时，候选必须恰好只有一个 CLAIM_RENT，作为 mandatory bot
action 以 score=0 直接选择并按策略延迟发送；不追加 NO_ACTION。人类玩家仍可沉默 20 秒触发
RENT_WAIVED，这个 bot 策略决定不改变人类规则。
有 deadline 且协议具有显式否定/默认命令的响应集合绝不追加 NO_ACTION：PURCHASE 使用
DECLINE、AUCTION 使用 PASS、收到 TRADE 使用 REJECT、MORTGAGE_TAKEOVER 使用
KEEP_MORTGAGED 作为真实默认候选。强制债务/筹资集合也不含 NO_ACTION，必须在出售、抵押、
交易和最终 DECLARE_BANKRUPTCY 中选一个可发送动作。只有不阻塞工作流的自发建设/出售/
抵押/赎回/创建交易等纯自愿集合，才最多保留 canonical 顺序前 63 个真实动作并追加一个
NO_ACTION。所有类别 candidate_count 都不超过 64，禁止按分数或随机数剪枝。
`legal_action_list_sha256`
的 preimage 精确为 candidate_count u8，随后按最终 canonical 顺序连接每项上述 14 字节线
记录，不含 score。事件显示的 synthetic `NO_ACTION` 使用 command_type=0xFFFF、其余字段
全零；它不是可发送协议命令。

五字段到完整命令的 v1 映射如下；未列的 payload 字段只能从该 transaction 当前公开状态
唯一回填（例如 auction_kind/version），若不能唯一回填则不得枚举。唯一权威生成表是
`GameData/schemas/bot-actions-v1.json`，其 hash 进入 policy/manifest：

| command | subject_id | target_id | amount |
| --- | --- | --- | --- |
| ROLL_REQUEST | 0 | 0 | 0 |
| PURCHASE_ACCEPT / DECLINE | asset_id | 0 | quoted_price / 0 |
| CLAIM_RENT | asset_id | debtor_player_id | quoted_amount |
| PAY_NOW | 0 | 0 | quoted_amount |
| AUCTION_BID / PASS | lot_id | target_asset_id / 0 | bid_amount / 0 |
| TRADE_CREATE / UPDATE | asset_id | `(direction u8 << 8) | counterparty_id` | cash_quote |
| TRADE_REJECT / CONFIRM | expected_trade_version | counterparty_id | 0 |
| MORTGAGE_ASSET | asset_id | 0 | 0 |
| MORTGAGE_BATCH_REQUEST | v1 bot 不枚举 | 0 | 0 |
| UNMORTGAGE_ASSET | asset_id | 0 | quoted_cost |
| BUILD_LEVEL / SELL_BUILDING | asset_id | target_level | quoted_cost / quoted_proceeds |
| HOLD_DECISION | decision | card_instance_id（不用卡为 0） | quoted_amount |
| DECLARE_BANKRUPTCY | 0 | 0 | 0 |
| MOVE_MANUAL_CONFIRM_REQUEST | target_position | reason | 0 |
| MORTGAGE_TAKEOVER_DECISION | asset_id | decision | quoted_amount |

trade direction：1 BOT_GIVES_ASSET_RECEIVES_CASH、2 BOT_RECEIVES_ASSET_GIVES_CASH；现金和资产
必须恰好各出现一边，cash_quote>0。transaction_id 仍直接取命令 envelope 的 transaction。
payload 回填后的完整命令还要通过第 12.3 节同一 encoder/parser round-trip；失败是策略 schema
错误，不得猜字段。

对状态 S 和玩家 p 定义六个整数函数，所有除法向零截断：

- `L(S,p)=cash + Σowned(resolved_price-(mortgaged?mortgage_value:0)) + 当前建筑/地标按规则出售所得`。
- `R(S,p)`：对棋盘每个 position 各做一次无随机公开租金估值；p 自己落在他人资产的支出
  取负，每名其他未破产玩家各落在 p 资产的收入取正，求和后除 tile_count。
- `G(S,p)`：p 已完整、无抵押颜色组中，每个当前合法单级建设的
  `(rent_after-rent_before)` 之和。
- `Q(S,p)=cash-reserve_cash`，其中 reserve 为 resolved_initial_cash×permille/1000。
- `O(S,p)=Σ其他未破产玩家(L+R+G)`。
- `B(S,p)=max(0, 当前公开强制义务最大金额-cash)`；无义务为 0。

仅无随机经济动作可由 Core 的固定容量 preview 得到 S'；六个 feature 依次是
`ΔL, ΔR, ΔG, ΔQ, ΔO, ΔB`。使用完整 int64 计算 `score=Σ(weight_i×feature_i)`，比较不缩放；
写 BOT_DECISION 时才把超出 i32 的值饱和到 INT32_MIN/MAX。ROLL_REQUEST、TRY_DOUBLES 等
含权威随机的唯一强制动作固定 score=0 并直接选择；同时出现两个强制动作是核心不变量
失败，不能让 bot 评分选择。无随机候选按 score 降序、再按 canonical key 升序选择。

交易是唯一不按“本条命令立即落盘状态”做经济 preview 的动作。对 TRADE_CREATE、TRADE_UPDATE
和 TRADE_CONFIRM，Core 必须在只读固定容量候选上把当前规范报价假设为双方已经确认并立即
SETTLED：原子应用现金、资产和可交易卡片转移，并建立规则必需的抵押接管/强制付款 workflow，
但不猜测 REDEEM_NOW 等后续自愿选择；由这个 full-settlement S' 计算六个 feature。实际 bot
仍只发送 create/update/confirm，不能借 preview 提前转移资产。报价若在该假设下所有权、现金、
卡片、上限、抵押接管或 int64 运算不合法，就不进入候选。TRADE_CONFIRM 无论本次是第一确认
还是会真实结算，都使用同一 full-settlement preview；REJECT 始终是零基线。CREATE/UPDATE 属于
纯自愿集合，只有相对 NO_ACTION 的完整结算 score 严格大于 0 才可选择。该 preview 不消费规则
或 bot RNG，并由 bot-decisions 黄金向量分别覆盖提议方和接收方。

普通（非债务）bot 发起交易还有持久 liveness fence：每 bot 保存最多五个
`{counterparty_id,economic_legal_hash[32],outcome}`，当该 bot-originated offer 被 REJECTED 或
EXPIRED 时写入或覆盖对应 counterparty 项；同一 material economic hash 下，所有面向该
counterparty 的新 TRADE_CREATE 都不再枚举，而不是只屏蔽恰好相同的报价。hash 使用上段债务
fence 的同一实质字段定义；单纯进入新 round、版本增加、时间经过或 online 变化都不能清 fence，
只有实质经济字段改变后才允许重新提议。每个 trade workflow 还持久 `bot_updated_mask u8`：
每个 bot 对该 transaction 最多发送一次 TRADE_UPDATE；已用 bit 后只能 CONFIRM 或 REJECT，
不能靠版本递增互相更新。这样两个确定性 bot 相互拒绝或改价也有有限终态。

两类延迟结算动作也使用 evaluation-only settlement proxy。对 UNOWNED_ASSET 的 AUCTION_BID，
假设该 bid 立即以 bid_amount 成交给本 bot、扣款并转移资产；对 BUILDING_STOCK bid，假设立即
扣款并把一个对应单位放到 target_asset_id；两者都据此 S' 评分，实际命令仍只更新 auction
ledger，不能提前结算。PASS 使用未改变经济状态的零基线。对会开启 BUILDING_DEMAND 或在
OPEN demand 中登记的 BUILD_LEVEL，proxy 假设按该地产牌价立即完成请求的单级建设并消耗一
单位库存；实际命令仍只打开窗口/登记，之后若进入拍卖仍按真实拍卖价。若任一 proxy 的现金、
所有权、均匀建设、目标或库存不合法，该候选不得枚举，不能把非法假设评分。

活动 BUILDING_DEMAND 对每个 eligible bot 的合法响应集固定为按 canonical asset key 排序的
全部单级登记 BUILD_LEVEL 候选，再追加一个 synthetic NO_ACTION；它是无显式协议拒绝命令的
唯一 deadline 例外。未登记者的 NO_ACTION 表示本窗口不登记；已登记者的 NO_ACTION 表示保留
当前 intent，其评分 baseline 必须用“当前 target 按牌价立即建成”的同类 proxy，替换候选只按
新 target proxy 相对该 baseline 的增量选择。NO_ACTION 只写一次 BOT_DECISION、不建 pending，
legal hash 不变时不空转。初次可能触发 demand 的自愿 BUILD_LEVEL 同样与 NO_ACTION 比较。
上述 auction/build proxy 与正常即时 BUILD_LEVEL 分别进入
`GameData/tests/bot-decisions-v1.json`，断言 preview 只影响 score，不改变实际 command/event 轨迹。

MORTGAGE_TAKEOVER_DECISION 也按付款完成后的 evaluation proxy 评分：REDEEM_NOW 只有当前现金
足够 redeem_now_cost 才枚举，proxy 扣款并清除该资产 mortgage；KEEP_MORTGAGED 在现金足够时
扣 interest_due 且保持 mortgage，现金不足时不制造负现金，而是建立与真实规则相同的
DEBT_RESOLUTION obligation，使 B/流动性特征反映 shortfall。实际命令仍只建立 PAYMENT_REQUIRED，
不能由 preview 跳过付款 workflow。REDEEM 相对 KEEP baseline 比较；PAY_NOW、HOLD_DECISION 等
唯一强制动作不做此类选择评分。

响应集合先以其真实默认候选的 preview 为零基线：PURCHASE_ACCEPT、AUCTION_BID、
TRADE_CONFIRM 或 REDEEM_NOW 相对各自 DECLINE、PASS、REJECT、KEEP_MORTGAGED 的增量
score 严格大于 0 才选积极动作，否则选择对应默认命令，平分也默认。强制债务/筹资集合选择
最高 score 的真实动作，即使为负，并用 canonical key 破平；DECLARE_BANKRUPTCY 只是没有
其他合法筹资动作时的最终候选。无需选择的强制随机动作固定 score=0 直接发送。

只有纯自愿集合使用 score=0 的 NO_ACTION；最高真实动作必须严格大于 0 才发送，否则选择
NO_ACTION。NO_ACTION 只提交一次 BOT_DECISION 并保持无 pending，直到 legal hash 改变；
禁止空转反复增版。HOLD_DECISION 的 v1 bot 默认真实命令为 TRY_DOUBLES；前两次不消费卡或
主动付费，第三次失败按 Core 规则转强制付款，因此该工作流也不会靠沉默等 deadline。

机器人 PRNG 固定为 PCG-XSH-RR 64/32，所有运算按无符号整数模 2^64/2^32。种子材料的
唯一字节序列为：16 字节 ASCII `GRIDOPOLY-BOT-V1`，紧接 room_seed u64 LE、seat_id u8、
policy_id_length u8、policy_id 的小写 ASCII 字节（长度 1–32）、revision u16 LE。计算
SHA-256 digest，取 digest[0..7] 小端为 initstate，digest[8..15] 小端为 initseq。

PCG 初始化和每次输出冻结为：

~~~text
state = 0
increment = (initseq << 1) | 1
pcg32()                         // 丢弃输出
state = state + initstate
pcg32()                         // 丢弃输出

pcg32():
  oldstate = state
  state = oldstate * 6364136223846793005 + increment
  xorshifted = u32(((oldstate >> 18) ^ oldstate) >> 27)
  rot = u32(oldstate >> 59)
  return (xorshifted >> rot) | (xorshifted << ((-rot) & 31))
~~~

将一次输出无偏映射到闭区间 `[min,max]` 时，`span=u32(max-min+1)`，
`threshold=u32(-span)%span`；反复取 r，直到 `r>=threshold`，返回 `min+(r%span)`。v1 exploration
为 0；随机数只决定表中响应延迟，不能影响规则随机结果。

黄金向量：room_seed=`0123456789ABCDEF`（数值，序列化为 `EFCDAB8967452301`）、seat_id=3、
policy_id=`balanced-v1`、revision=1 时，digest 为
`0558c7c474c5b5ca3c8a6713dc9cfef7f8aa30d8c8434dcfee9b29976eec3fe5`，initstate=
`CAB5C574C4C75805`，initseq=`F7FE9CDC13678A3C`，increment=`EFFD39B826CF1479`；前五个
未映射输出为 `C3D89D6E 75033271 7CB8E81D 0262C7C5 97F73F8F`，balanced-v1 的首个
延迟为 759ms。固定 seed、内容哈希和输入事件必须生成逐字节相同的机器人命令序列。

每个机器人决策随产生该命令的同一 COMMIT 写入 0x0290 BOT_DECISION domain event，而不是
串口旁路日志。其 FieldTLV 必须包含 policy_id（1–32 字节）、revision u16、policy_sha256、
decision_index u32、legal_action_list_sha256、candidate_count u8、候选固定记录数组、
selected_index u8、PRNG state/increment/draw_index 的前后值和 planned_delay_ms u32。候选记录
按 canonical action key 升序，每项固定为 command_type u16、transaction_id u32、subject_id
u16、target_id u16、amount i32、score i32；candidate_count 最大 64，selected_index 必须小于
count。候选必须先按上文固定枚举/63+NO_ACTION 规则缩界，再评分；
legal_action_list_sha256 严格使用上文不含 score 的 1+14×count 字节。该事件最坏
小于 2KiB，
计入单 COMMIT 4KiB event_payload 上限，并由 domain-events-v1.json 冻结字段 ID/type。这样
snapshot+COMMIT 回放可逐字节验证选择，不依赖未编码的内存诊断。

决策黄金向量：balanced-v1、transaction_id=7、asset_id=2、price/amount=100 时，最终候选
依次为 PURCHASE_ACCEPT 和 PURCHASE_DECLINE；其线 preimage hex 为
`0201100700000002000000640000000210070000000200000000000000`，SHA-256 为
`cc95d8806690c358936b6a6842a5b0ba5a64d2986cdb894ae91cd30aa0ed0e2e`。以 DECLINE 为零基线，
Core preview 必须给 PURCHASE_ACCEPT 特征 `[0,200,0,-100,0,0]`，balanced 权重得完整
score=130000；PURCHASE_DECLINE 为 0，因此 selected_index=0。该向量连同一个拍卖 PASS 和一个交易 REJECT 向量进入
`GameData/tests/bot-decisions-v1.json`，生成器逐字节锁定，不能只测试 PRNG。

host liveness fixture 还必须构造 40 格/6 人、bot 拥有全部 28 个可经济操作资产的上界状态：
逐个阶段断言一次枚举分别不超过 28、最终 D4 恰好 1，任何 legal list 都≤64；让五个对手依次
拒绝所有债务筹资报价，断言同一 material hash 最多写 28 个 attempt 后 D2 穷尽并进入 D3；
随后逐资产抵押并每次重算 shortfall，只有余额仍不足且 D3 也穷尽时才暴露唯一 D4，绝不能跳过
抵押直接破产。另加“全部抵押所得不足但对手可购买一个未抵押资产”的轨迹，必须在第一次
抵押前至少发出一笔 funding trade。另以两个
balanced-v1 bot 互相拒绝普通报价并各使用一次 UPDATE，断言相同 economic hash 下不会再次创建
同 counterparty 交易；仅改变现金/产权/抵押/建筑/可交易卡/义务之一后才重新开放。

BotPlanner 只在一次候选 COMMIT 使本座 legal hash 改变、成为当前行动者、收到交易/拍卖/
强制义务，或旧 pending 被同一候选关闭时评估；按 seat_id 升序，并把决策事件、更新后的
PRNG、decision_index 和至多一个 pending command 纳入正在形成的同一 COMMIT。pending 固定
保存 planned_at_state_version（仅供审计）、legal hash、command_type 与不含传输前缀/
expected_state_version 的规范 command body、源 transaction/instance/workflow revision、
deadline_instance_id、due_ms、original_window_ms、saved_remaining_ms 和 decision_index。delay 是
策略闭区间唯一一次 PCG 无偏抽样；创建时 original_window_ms=saved_remaining_ms=delay。

BotScheduler 严格进入第 7.3 节全局 timer-ready tuple，其中 owner_sort_id=seat_id、
request_sort_id=decision_index，并以 actor_kind=BOT/trigger_kind=10 裁决。每个候选 COMMIT 在序列化前都对至多六个既有 pending
重新投影 source identity 与 legal hash：相同则保留原 due/decision，哪怕全局 state_version 因
无关座位变化而增加；不同则必须在该候选内关闭旧 pending 并由 BotPlanner 重评。scheduler
到期时再次复核 source 与 legal hash，匹配后才用 dispatch 时的当前 state_version 包装已保存
command body 并入 CommandBus；planned_at_state_version 绝不作为有效性条件。若到期复核发现
不匹配，说明前一 COMMIT/恢复漏做重评，是不变量失败，进入 RECOVERING，不能静默丢弃后让
座位永久无动作。
一个 bot 命令成功 COMMIT 后，如新状态仍需该 bot 行动，BotPlanner 只能在该新 COMMIT 尾部
产生下一 decision/pending，不能在同一次 dispatch 内循环执行。每座最多一个 pending 和一个
未终态 request；虚拟时钟同毫秒也遵守第 7.3 节完整 tuple。

只有 selected 是真实可发送动作时才恰好消费一次 `uniform(delay_min,delay_max)`，包括唯一
强制动作；planned_delay_ms>0 并建立 pending。NO_ACTION 不消费 PRNG、planned_delay_ms=0、
不建 pending。每座持久保存 `last_evaluated_legal_hash[32]` 与
`last_decision_was_no_action bool`；controller/policy/assignment 改变时清零，重启后 hash 未变
不得重复提交 NO_ACTION decision。

BotPlanner 在追加每一整条 BOT_DECISION 前必须同时预序列化检查三项：本 COMMIT 全部
BOT_DECISION 的 `Σcandidate_count≤128`、总 `event_count≤32`、总 `event_payload≤4096`。
按 seat_id 处理；若下一整条事件会使任一上限越界，则当前候选只关闭已经因 legal hash
变化而失效的旧 pending，不追加半条 decision，并在 AuthorityState 保存 next_seat_id、trigger_kind=17、
从全局 next_transaction_id 分配的 planner_transaction_id、新 deadline_instance 和
`due_ms=当前 COMMIT.received_at_ms` 的 BOT_PLANNER_CONTINUATION，并停止接收外部写命令。
该 SYSTEM continuation 以紧随的 ingress_sequence、优先于新外部写 dispatch；若仍需下一片，
保留 transaction_id 并原子换新 instance，完成时关闭。任何一条 BOT_DECISION 都不拆分；六座最多三条
连续 planner COMMIT，完成后关闭 source 再开放外部命令。每条仍须 event_payload≤4KiB，
生成器用六个 63+NO_ACTION 最坏夹具验证。

除上述容量触发的 defer 外，`/session/start` 和第 19.4 节固定事件上限的 ROOM_RESUMED 对
“确有待评估 bot”还要强制 planner-defer：候选在序列化前对至多六座做无副作用预判，只有
BOT controller 在新状态成为可行动/响应者、legal hash 需首次或重新评估，或已有仍需完成的
planner source 时才创建/保留 continuation；没有这类座位就不创建，已有但已无工作的 source
在该固定 COMMIT 的既有事件内关闭。固定 COMMIT 本身不追加 BOT_DECISION，从而 start 仍
恰好只有两条 domain event、ROOM_RESUMED 仍保持其 ≤13 条上限。

需要 defer 时，两者在同一 state_delta 中创建或复用唯一全局 BOT_PLANNER_CONTINUATION；
若已有未完成 planner_transaction_id 就保留它并原子换新 instance，否则从
next_transaction_id 分配，trigger_kind=17，due_ms=本 COMMIT.received_at_ms，
original_window_ms=saved_remaining_ms=0。因为预判后的 command gate 已关闭且其间没有权威
写入，该 continuation dispatch 必须至少生成一条 BOT_DECISION；若复核为零工作则是
不变量失败，不能提交 event_count=0 的 COMMIT。成功 flush 后 continuation 优先于任何新外部
写 dispatch。这样固定事件计数、COMMIT 至少一事件和机器人可推进性都不依赖易丢 RAM 回调。

机器人覆盖购买、拒绝、拍卖、收租、付款、建设、出售、抵押、赎回、限制区、筹款、
简单交易和破产。机器人同一时间最多一个未完成请求，不能读取牌堆或对手私有交易。

房间创建时锁定 disconnect_policy：

- PAUSE：默认正式行为。真实玩家离线后保持 REAL_CONSOLE/online=false 并暂停其自愿输入；
  重连 RESUME 可恢复，管理员也可显式 web/bot 接管。
- BOT_TAKEOVER：测试可选。OFFLINE presence COMMIT 同时按第 17.1 节永久解除旧座位 token，
  使用房间冻结的 balanced-v1 接管并初始化确定性 PRNG；不会接受已存在交易。
- WEB_ADMIN：测试模式。离线本身不切换，只有管理员显式 `/seats/takeover action=web` 才接管。

已成立强制债务和付款倒计时继续裁决；购买窗口断线超时视为拒绝并进入拍卖。机器人
或管理员不得替真实玩家静默确认自愿购买、交易或抵押。

session reset 必须同时清除旧 room 的 ESP-NOW session request/terminal cache、定时器、机器人
待执行动作、模拟棋盘输入和临时链路故障；不得清除第 17.1/19.5 节保留的 HTTP admin
幂等 namespace、request high_watermark 或 operation 槽。

## 19. 持久化、日志轮换与房间生命周期

### 19.1 NVS 与 LittleFS 边界

NVS 保存设备配置、Wi-Fi 配置来源、server_id、peer binding 和派生密钥材料、seat_token_key、
storage generation、active bank、minimum_valid_generation、最近一次
room tombstone 和恢复摘要。它不保存玩家屏提供的 seat_token 明文。

storage 元数据不用三个独立 key“同时更新”，而使用 meta-A/meta-B 两个 40 字节 blob：

~~~text
magic                       4 bytes = 47 50 4D 31
schema                      u16 LE = 1
length                      u16 LE = 40
sequence                    u32 LE
active_bank                 u8，0=A / 1=B
reserved                    3 bytes = 0
active_generation           u32 LE
minimum_valid_generation    u32 LE
tombstone_room_id           u64 LE
tombstone_generation        u32 LE
crc32                       u32 LE
~~~

CRC 覆盖前 36 字节。更新时写 sequence 较新的非活动 blob、NVS commit、读回验证；启动只
接受 CRC/length/schema 有效且 sequence 最新者，不得跨回绕比较。正常 meta sequence 与
active/minimum/tombstone generation 只能推进到 0xFFFFFFFE；达到后停止新房间、轮换和回滚提升，
只允许诊断/export/reset。0xFFFFFFFF 是一次 final reset 专用终值：若当前 room 非零，reset 可用
最后一次 meta sequence（如需）与 generation=当前+1，把 canonical NO_ROOM、minimum 与 tombstone
原子激活；此后只允许诊断导出和显式物理凭据/存储维护，不能新建房间。final reset 已完成或
任一计数已为 0xFFFFFFFF 时不得再次递增。这样 state/event 与 storage counter 都保留清除游戏的
最后逃生边界，而绝不定义回绕比较。

首次安装不是“meta 无效就自动格式化”。只有同时满足以下条件才标记
FACTORY_UNINITIALIZED：meta-A/meta-B 与 provision-intent key 都不存在（不是存在但 CRC 错）、
六个 BindingSlot/room tombstone/seat_token_key/server_id 均不存在，且整个 LittleFS partition
只读扫描均为 0xFF。此状态只启动串口诊断，生成一次性六位 provision token；用户必须在
60 秒内通过当前物理串口输入 `GRIDOPOLY PROVISION <token>`。任一状态文件/key/非 0xFF 字节
存在但无法验证时都进入安全诊断，绝不能猜成新机或自动格式化。

确认后先原子写带 CRC 的 NVS `provision-intent(schema=1, nonce[16], stage)`；stage 依次为
PREPARED、FS_FORMATTED、BANK_VERIFIED、META_VERIFIED。intent 一旦 PREPARED 即证明本次物理
确认，掉电后只能继续同一初始化，不再次扩大删除范围。流程固定为：格式化仅 LittleFS
partition；生成并读回 server_id/seat_token_key；构造 generation=1、room_id=0、
room_lifecycle=NO_ROOM、state_version=0、last_event_id=0、content hash 全零的 canonical NO_ROOM Authority snapshot，
以及同代 base_event_id/base_state_version 都为 0 的空 journal；写入 A bank、flush/重开并按
第 19.2 节完整验证；最后分别写两份除 sequence 外语义相同的 meta（slot A sequence=1、slot B sequence=2，
active_bank=A、active_generation=minimum_valid_generation=1、tombstone 全零）并读回，清除
intent。只有 META_VERIFIED 后才按正常启动开放网络。intent 中途恢复允许重建这套空 lineage，
但不得擦除 NVS 的 Wi-Fi/commissioning 配置；已有任何 binding/tombstone 时 provisioning 必须
拒绝并要求另立的物理 factory-reset 流程。

LittleFS 使用两个完整存储 bank：

~~~text
state-A.snapshot     最大 64KiB
state-A.journal      最大 128KiB
state-B.snapshot     最大 64KiB
state-B.journal      最大 128KiB
replay-export.bin    最大 256KiB
storage-scratch      最大 64KiB
~~~

只有 active bank 接受追加；另一 bank 是上一个完整状态或正在生成的新 bank。不得覆盖
active snapshot 或截断 active journal。

### 19.2 快照和轮换

每个 journal 以固定 72 字节 header 开始：

~~~text
magic               8 bytes = 47 50 4A 4E 4C 31 00 00
journal_schema      u16 LE = 1
header_length       u16 LE = 72
storage_generation  u32 LE
room_id             u64 LE
base_event_id       u64 LE
base_state_version  u32 LE
content_manifest_hash 32 bytes
header_crc32        u32 LE
~~~

header_crc32 覆盖前 68 字节。之后只能是第 7.3 节 COMMIT；不允许其他未定义 record type。

snapshot 文件布局固定为：

~~~text
file_length            u32 LE，包含整个文件
magic                   8 bytes = 47 50 53 4E 41 50 31 00
snapshot_schema         u16 LE = 1
header_length           u16 LE = 112
storage_generation      u32 LE
room_id                 u64 LE
state_version           u32 LE
last_event_id           u64 LE
content_manifest_hash   32 bytes
authority_blob_schema   u16 LE = 1
reserved                u16 LE = 0
authority_blob_length   u32 LE
authority_blob_sha256   32 bytes
authority_blob          authority_blob_length bytes
file_crc32              u32 LE
file_length_copy        u32 LE
~~~

header_length 从 file_length 起计算到 authority_blob 前，共 112 字节。file_crc32 覆盖 magic
至 authority_blob 末尾，不覆盖首个 file_length 和尾部两个字段。首尾长度、SHA、CRC、
generation、room 和 schema 必须同时有效。

文件总长恒为 `112 + authority_blob_length + 8`，必须不超过 65,536；因此格式理论上限是
65,416 字节，而 4MiB/no-PSRAM profile 采用更严格的 24KiB AuthorityStateBlob 运行门禁。
主机测试必须同时断言 blob≤24,576 和 file_length≤24,696；文件格式较大的理论容量不授权
固件接受更大运行状态。

AuthorityStateBlob 是排序 TLV，不是 C++ 内存转储：blob_schema u16=1、
state_field_schema_hash[32]、section_count u16=12，随后每节为 section_id u16、
section_schema u16、section_length u32、FieldTLV body。节按 ID 递增且全部 critical：

1. ROOM_CONFIG_AND_CONTENT
2. PLAYERS_AND_SEATS
3. ASSETS_AND_BUILDING_SUPPLY
4. DECK_ORDER_HELD_CARDS_AND_DRAW_CURSOR
5. RULE_RNG_AND_ACTIVE_DICE
6. TURN_AND_ROUND
7. BLOCKING_TRANSACTION_AND_CONTINUATION
8. DEADLINES_ORIGINAL_AND_REMAINING
9. MULTI_PLAYER_CARD_PROGRESS
10. BOT_POLICY_PRNG_AND_PENDING_ACTION
11. BOARD_POSITION_ADAPTER
12. COUNTERS_AND_ACTIVE_SOURCE_IDS

FieldTLV 与第 7.3 节定义相同；节内 field_id 升序。唯一字段 ID/type/required 注册表是
GameData/schemas/game-state-v1.json，其 SHA-256 必须等于 state_field_schema_hash 并进入
content manifest。未知 critical 节/字段或 hash 不符不可恢复。该 blob 同时是 state_delta
的 base/result 字节串，因此断电黄金测试不依赖再次执行规则。
`room_lifecycle` 与 `room_seed` 分别是 ROOM_CONFIG_AND_CONTENT 的 required critical U8/U64
字段；schema=1 必须冻结第 7.1 节四个 lifecycle 值，缺失或与 room_id/active-meta/replay 末端
组合不合法时整个 lineage 无效。NO_ROOM 的 seed 必须为 0；非零 room 的 seed 允许任意 u64。

`PLAYERS_AND_SEATS` 每个配置座位除经济/公开玩家字段外，固定保存 assignment_version、
binding_id、binding_device_id u64、binding_slot_generation u32、derivation_counter、
controller_type、control_epoch、bot_policy 引用，以及 presence_transaction_id、
presence_generation、presence_state、presence_instance_id。无 binding 时前三个 binding 字段
全零；非 REAL_CONSOLE 时 presence 四字段全零。SEAT_BOUND 必须在同一 COMMIT 从
PendingJoin/BindingSlot 复制 device_id 与 slot_generation，SEAT_UNBOUND/永久接管同一
COMMIT 清零，不能只保存 binding_id。这样回滚后的 NVS-wins 比对不依赖 RAM。

snapshot 不得包含 Wi-Fi、PMK、LMK、seat_token、seat_token_key、管理 token 或
commissioning secret；座位只保存 room/binding/device/slot generation/assignment/seat/player/
derivation_counter 等
非秘密派生输入，不保存凭据本身。

`RULE_RNG_AND_ACTIVE_DICE` 只保存 PCG state、increment、draw_index，以及当前未完成
continuation 已冻结且仍会参与后续裁决的骰子，固定最多 8 个 u8；历史骰子只存在于已提交
DICE_ROLLED/event records 和诊断回放，不复制进权威快照。超过 8 个才能继续的规则流程在
内容校验期拒绝。`COUNTERS_AND_ACTIVE_SOURCE_IDS` 必须包含 next_transaction_id、
next_deadline_instance_id、next_event_id 和 next_observation_id；每座 next_decision_index 位于
BOT_POLICY_PRNG_AND_PENDING_ACTION。每个活动 source 的 transaction/instance 只在
其所属 deadline、continuation、presence、bot 或 board 固定记录中出现，不复制 consumed
集合。任何 section 或完整
AuthorityStateBlob 超过 24KiB（24,576 字节）都是候选
提交失败，不能截断、改为无界容器或丢弃恢复所需项。

第 13.7 节 current_roll 显示上下文属于 `TURN_AND_ROUND` 的 required 字段，按该节时机持久写入/
清零，重启后可直接投影；它不是 RULE_RNG_AND_ACTIVE_DICE 的可裁决 frozen dice，也不允许规则
从中再次消费。这样“当前回合最近一骰”可恢复，而更早回合仍只在 journal/replay，不会形成
无界骰子历史。

自上次快照后 32 个事务、journal 达 64KiB、进入/离开阻塞事务、游戏结束、管理员请求
导出或安全关闭时触发轮换。轮换期间 CommandBus 暂停新写命令，但继续 ACK、心跳和只读
HTTP：

1. 选择 inactive bank，generation=active+1；CommandBus 已暂停并排空既有 dispatch 后采样
   checkpoint_now_ms。若任一 active source 的原完整 timer tuple 满足 `due_ms<=checkpoint_now_ms`，
   本次不得构造 C* 或把它压成 remaining=0：保持外部写 gate 关闭，释放 LineageBarrier，只按
   第 7.3 节原 tuple dispatch 恰好下一项到期 source，等待其 COMMIT/拒绝终态，再重新取得 barrier
   和采样；反复直到某次采样没有 overdue source。随后才以该唯一采样点从 live blob 构造
   checkpoint-normalized 候选 C*，只允许按第 19.4 节刷新所有 active time source 的
   saved_remaining_ms，state_version/last_event_id 与全部其他字段逐字节不变。
2. 用 C* 写临时 snapshot，flush、关闭、重开并验证长度、SHA-256、CRC、规范化差异白名单和
   全部不变量。
3. 写同 generation 的空 journal 临时文件，header 固定 base_event_id/base_state_version；其
   隐含 base blob 就是 C*，首条后续 COMMIT 的 delta 必须以 C* 的长度/字节计算。
4. flush、关闭并验证两文件同代，再原子替换 inactive bank 正式文件。
5. 更新 NVS active bank/generation；该原子 meta 提交是新 bank 的唯一激活边界。meta 成功后、
   重新开放 CommandBus 前，以预分配 arena 做不可失败 swap，把 live blob 同步为逐字节相同的
   C*；后续提交只进入新 journal。若 meta 前任一步失败，既不激活 bank 也不 swap live。
6. 旧 bank 保持原来的非规范化 blob/journal 自洽不变，直到下一次完整成功轮换。

checkpoint normalization 是唯一允许在不增加 state_version/event_id 时改变 AuthorityStateBlob
字节的存储边界。旧 journal 最后一条 COMMIT.post_state_hash 只验证该旧 lineage 的原始 result
blob，不要求等于新 snapshot 的 C* hash；新 snapshot.authority_blob_sha256 验证 C*，而新
journal 第一条 delta/post_state_hash 从 C* 继续。生成器必须逐字段证明两者只差允许刷新的
saved_remaining_ms；若有其他差异就中止轮换。由此 checkpoint 前后各自的 hash 链完整，不能
拿新 snapshot 套用旧 delta。

checkpoint 断电夹具必须放入两个 due_ms 已过期但 seat_id 顺序与 due/instance 原 tuple 顺序相反的
BOT_PENDING（另覆盖一个同毫秒不同 trigger_kind 的组合）：请求轮换后断言先按原 tuple 产生两条
COMMIT，再冻结 C*；在每个交接点掉电恢复，事件、bot decision_index 与最终 hash 都必须与未轮换
轨迹相同，不能因两个 remaining 都变 0 而改按 seat/kind 重排。

任一步失败都保留原 active bank 并进入 RECOVERING。启动必须先取得至少一份有效 NVS
meta；两份都无效时不猜测 bank，只进入安全诊断。文件资格冻结为：

- storage_generation 必须不大于 NVS active_generation，因此已写完但 meta 尚未提交的
  inactive bank 永远不会被提前加载。
- storage_generation 必须不小于 minimum_valid_generation。
- room_id 等于 tombstone_room_id 且 storage_generation 小于 tombstone_generation 的 bank
  永远无资格。
- snapshot 与 journal 的 generation、room_id、content_manifest_hash 必须逐字节相同；
  空大厅唯一例外值为 room_id=0、content_manifest_hash=32 个零、state_version=0。

在有资格的 lineage 中选择 generation 最高且文件完整有效者。NVS active_bank 用于
定位同 generation 的首选槽，但不替代文件 CRC/哈希校验。本规格不使用未序列化的
`requires_tombstone_generation`；storage_generation 与已冻结 NVS meta 字段就是唯一先验门禁。

恢复器只重放 event_id 大于 snapshot.last_event_id、previous_version 等于当前版本、
committed_version=previous+1、room_id/generation 匹配的完整 COMMIT。最新代损坏而旧代有效
时进入 RECOVERING_ROLLBACK_REQUIRED，显示可能丢失版本范围；不得静默用旧余额继续。
恢复摘要固定包含 rollback_required、lost_range_known、lost_from_version 和 lost_to_version。
只有两个边界都来自 CRC/哈希有效的 snapshot header 或完整 COMMIT 时才允许 known=true；
损坏文件无法证明最后版本时必须 known=false 且 from/to=0，网页显示“丢失范围未知”，不能
根据文件长度或残缺字节猜测。

### 19.3 空间和回放

replay-export 是按需生成的诊断副本，不是恢复权威源；v1 只导出请求时的 current active
lineage，不跨读 inactive bank，也不维护第二套持续追加的历史。admission 要求请求 room_id
精确等于 active room（canonical NO_ROOM 时为 0），且全设备只允许一个 RESERVED/CAPTURING/
PREPARING/READY export；RESERVED 的原子语义见第 17.1 节。

捕获必须是与轮换/reset/session-new/recovery/provisioning 共用的 `LineageBarrier`，而不是若干松散
读取。operation 进入 running 后关闭后续 CommandBus dispatch，等待当前 dispatch 完成并等待
StorageActor 中此前的 lineage 写任务终态；上述其他 lineage 操作不得与 barrier 重叠。在同一个
StorageActor 独占临界区内，依次 flush active journal、重读 NVS active meta 与匹配文件 header、
复核 live state_version/last_event_id 等于 journal 已 flush 末端，然后原子冻结
`(bank,generation,snapshot_length,journal_prefix_length,end_state_version,end_event_id,room_id,
content_manifest_hash)` 并在任何覆盖入口重新开放前安装该 bank 的 export pin。只有 pin 安装成功，
HTTP operation 才一次性变为 succeeded 并返回 `{export_id,status:"preparing"}`，随后重新开放
dispatch；barrier/pin 前失败则该 operation 正常 failed、释放 CAPTURING 槽且不产生可查询 export。
后台只复制两个已冻结的精确 byte range，后续 journal append 不进入本次文件。已 succeeded 的
operation 不因后台 export 失败而反改终态，admin token 轮换也不得取消 copy/验证任务。

任何选择 destination/inactive bank 的路径——轮换、reset、session/new、recovery promotion 与
provisioning repair——都必须先由 StorageActor 检查 export pin；可以写未 pin 的另一 bank，但在
copy 与完整验证结束前绝不能 open-for-write、truncate、rename 或覆盖被 pin 的 bank。若唯一目标
因 pin 等待，则关闭新写并优先完成最多 2KiB/tick 的 export copy。失败时删除 partial export、释放 pin、
把 export 标为 failed，不改变权威 lineage；成功 flush、close、reopen 并完整验证后释放 pin。active snapshot 最大
64KiB、captured journal 最大 128KiB，所以下述 container 必定小于 256KiB。

export registry 是一个固定 RAM 槽，public 状态只有 PREPARING、READY、FAILED；RESERVED/CAPTURING 仅由
原 HTTP operation 观察。每 boot 的 `next_export_sequence u32` 从 1 开始，0/0xFFFFFFFF 保留，
分配 0xFFFFFFFE 后进入 exhausted 且本 boot 拒绝新导出，不回绕。`export_id` 是 16 位小写 hex
`hex8(ingress_boot_id)||hex8(sequence)`，路径参数和 JSON 都使用该字符串。成功捕获时固定
`expires_ms=capture_completed_ms+600000`（u32 模时钟、窗口小于 2^31）；到期时取消未完 copy、
删除无 reader 的 partial/ready 文件、释放 pin，并把 registry 对新 status/download 隐藏，此后都返回
404 EXPORT_NOT_FOUND；若已有 download lease，则先置 delete_pending，待最后一个 lease 释放才删
ready 文件和清空内部槽。FAILED 会立即删除 partial 并释放 pin，保留诊断到 expires_ms；下一次合法
POST 可以原子淘汰 FAILED 后建立新任务。PREPARING/READY 未到期时新 POST 返回 429
OPERATION_CAPACITY。

status result 的键和类型固定为
`{export_id,status,length,sha256,expires_ms,history_truncated,checkpoint_state_version,
end_state_version,checkpoint_event_id,end_event_id,failure_code}`。从 PREPARING 起 length 就是推导的
完整 container 字节数，checkpoint/end 元数据不再改变；event ID 是 16 位 hex。PREPARING 的
sha256/failure_code 为 null，READY 的 sha256 为 64 位小写 hex且 failure_code=null，FAILED 的
sha256=null 且 failure_code 只能是 COPY_IO、VALIDATION_FAILED、STORAGE_LOST 或 INTERNAL_LIMIT。
download 只允许 READY；PREPARING 返回 409 EXPORT_NOT_READY，FAILED 返回 409 EXPORT_FAILED，
未知、过期或旧 boot ID 返回 404 EXPORT_NOT_FOUND。三者固定 JSON error 映射为：NOT_FOUND
severity=2/retry_policy=0/detail_code=0；NOT_READY severity=1/retry_policy=1/
retry_after_ms=250/detail_code=0；FAILED severity=2/retry_policy=0/retry_after_ms=0，detail_code
1 COPY_IO、2 VALIDATION_FAILED、3 STORAGE_LOST、4 INTERNAL_LIMIT。此处 RETRY_SAME_REQUEST 是
side-effect-free GET 的明确例外：重复完全相同 method/path/query，不使用 request_id、不推进任何
watermark；其他 GET/错误不得据此自动重试。

READY download admission 必须在 registry 锁内验证 export_id 未过期、offset<length、limit 1–4096，
并为固定 ready 文件/status/length/sha256 增加一个 DownloadReadLease。成功查询总是 HTTP 206；
使用无溢出运算 `end=offset+min(limit,length-offset)`，精确发送 `[offset,end)`，响应头固定为：

~~~http
Content-Type: application/octet-stream
Accept-Ranges: bytes
Content-Range: bytes <offset>-<end-1>/<length>
Content-Length: <end-offset>
X-Gridopoly-Export-SHA256: <64位小写hex>
Cache-Control: no-store
~~~

到期发生在已 admission 的 chunk 中途时只设置 delete_pending；该响应可正常发送完，随后请求
立即 404。lease 仅在最后一字节发送、主动 abort 或 socket timeout 后释放，最后一个 reader 负责
执行延迟删除。启动在开放 HTTP 前一律删除 `replay-export.partial` 与 `replay-export.bin`，清空
registry/pin/sequence/read lease；旧 boot 的 export_id 永远 404，ready 文件不跨重启承诺保留。

v1 container 所有整数均为小端、没有隐式 padding，固定 header_length=132。精确布局为：

| offset | 长度 | 字段与约束 |
| ---: | ---: | --- |
| 0 | 4 | file_length u32；必须等于 `140 + snapshot_length + journal_length` |
| 4 | 8 | magic bytes：`47 50 52 50 4c 59 31 00`，即 `GPRPLY1\0` |
| 12 | 2 | export_schema u16，固定 1 |
| 14 | 2 | header_length u16，固定 132 |
| 16 | 4 | flags u32；bit0=history_truncated，其余位必须为 0 |
| 20 | 8 | room_id u64 |
| 28 | 4 | storage_generation u32 |
| 32 | 4 | checkpoint_state_version u32 |
| 36 | 4 | end_state_version u32 |
| 40 | 8 | checkpoint_event_id u64 |
| 48 | 8 | end_event_id u64 |
| 56 | 32 | content_manifest_hash 原始 32 字节；NO_ROOM 时全零 |
| 88 | 4 | snapshot_length u32，1–65536 |
| 92 | 4 | journal_length u32，必须包含 journal header，且≤131072 |
| 96 | 32 | payload_sha256：精确 `snapshot_bytes || journal_bytes` 的 SHA-256 |
| 128 | 4 | header_crc32：offset 4..127 的 CRC-32/ISO-HDLC |
| 132 | snapshot_length | active snapshot 文件的逐字节副本 |
| 132+snapshot_length | journal_length | active journal 的逐字节 header+完整 COMMIT prefix |
| 132+snapshot_length+journal_length | 4 | file_crc32：offset 4 至 journal_bytes 最后一字节的 CRC-32/ISO-HDLC |
| 136+snapshot_length+journal_length | 4 | file_length_copy u32，必须与 offset 0 相同 |

`history_truncated` 当且仅当 checkpoint_event_id 非零；它表示更早事件已经由 snapshot 汇总且
未包含在文件中，不表示本 container 自身截断。checkpoint 两字段必须等于 snapshot header 的
state_version/last_event_id；end 两字段必须等于逐条重放 captured journal prefix 后的末端。journal
只能在完整 COMMIT record 边界结束，其 header 的 room/generation/content hash/base version/event
必须与 snapshot 和 container 一致。snapshot_bytes 与 journal_bytes 保留各自原生格式、schema、
CRC 与 record framing；container 不重新编码它们。

解析器先读取固定 132 字节，并在不溢出的 u64 算术中检查 magic/schema/header_length、flags、
两个 payload 长度、推导 file_length≤262144；再从文件末尾检查 file_length_copy 和实际文件长度，
验证 header_crc32、file_crc32、payload_sha256，最后才解析 snapshot/journal 的原生 CRC、lineage、
完整 record 边界并重放不变量。任一步失败整个 export 无效，不能跳过未知 flag 或部分恢复。
status 的 sha256 是通过上述全部校验后整个 file_length 字节的 SHA-256；只有此后状态才可从
preparing 变为 ready。`GameData/tests/replay-export-v1.json` 必须冻结至少一个完整 container hex、
header CRC、file CRC、整文件 SHA-256、解析字段与重放末端，并由 C++/TypeScript parser round-trip。

LittleFS 可用空间低于 128KiB 时，先删除过期 export，再尝试 bank 轮换；仍不足则停止
写命令并进入 RECOVERING。禁止删除唯一 active bank、截断未快照 journal 或继续纯内存
运行。

### 19.4 重启与 deadline

每 boot 初始化 RAM-only `next_recovery_generation=1`、`current_recovery_generation=0`。
每次从非恢复态进入一个新的 RECOVERING/RECOVERING_ROLLBACK_REQUIRED episode，或当前 episode
的 reason、rollback/lost-range 事实被新的恢复判定替换时，分配当前 next 的 1..0xFFFFFFFE 并在
同一 ProjectionGate 递增 next；重复发布逐字段相同的 overlay 不分配。离开恢复进入正常
ACTIVE/NO_ROOM 时 current 清零。0 与 0xFFFFFFFF 不分配；分配 0xFFFFFFFE 后 next 置 sentinel，
再需要新 episode 时只能进入 resume_allowed=false 的 REVISION_EXHAUSTED/reset-only overlay，
current=0，绝不回绕。每个已提交 ROOM_RECOVERING 事件的 recovery_generation 必须等于当时
current 非零值；summary.recovery.generation 也逐值相同。

`POST /recovery/resume` 的 payload recovery_generation 必须非零，并在 HTTP admission 预留前及
dispatch 产生候选前分别精确等于 current；任一不符返回 STALE_STATE。episode 替换、分配、清零
都属于 runtime projection 变化并增加 projection_revision，因此网页必须从同一 summary
projection_epoch 取得 generation 与 lost-range 确认字段，不能沿用旧 episode 的按钮请求。

重启先验证 tombstone 和两 bank，选择最高有效 lineage，逐条重放并检查版本和不变量，
再使所有旧 session_id 失效。对 room_lifecycle=ACTIVE，必须先用与真实 builder 相同的固定容量
预运行做 exhaustion preflight：至少验证 state_version 可加一且仍≤0xFFFFFFFE、next_event_id
能容纳最坏 13 个 ROOM_RESUMED 事件仍不侵占 ROOM_CLOSED 保留项，并验证本次可能用到的
next_transaction/deadline instance、assignment_version、player_revision、presence_generation 与
bot-planner source 均能递增且完整候选可序列化；还必须验证
`active_generation+1<0xFFFFFFFE` 且当前有效 meta `sequence+1<0xFFFFFFFE`，保证第 19.4 节
recovery promotion 的 K=G+1 与 meta 切换都能完成。任一当前值为 0xFFFFFFFE 时普通 resume
或 0xFFFFFFFD 时普通 resume preflight 必须失败：落到 0xFFFFFFFE 按第 19.1 节已经是
diagnostic/reset-only，不得伪装为可恢复写状态；0xFFFFFFFF 继续保留给 final reset 逃生。不能
先进入一个永远无法 promotion 或 promotion 后立即禁写的 RECOVERING。通过才进入暂停 RECOVERING；每个设备完成新
LINK 后获得独立新 session_id，但 RECOVERING 中只允许 LINK/HELLO/诊断，不接受 SESSION_RESUME
presence 写入或发送游戏快照。管理员恢复必须提交 ROOM_RESUMED，之后才恢复写命令。

任一 preflight 项失败时跳过 ROOM_RESUMED，进入 REVISION_EXHAUSTED RuntimeHealthOverlay；
HTTP 允许集合只以第 17.1 节 health_overlay 固定表为唯一准则，不在此另列漂移白名单；玩家
LINK/机器人/领域写全部关闭。reset prepare/commit 在该 overlay 明确允许，
并使用第 11 节保留的单个 state_version、第 7.3 节保留的 event_id、transaction_id=0
ROOM_CLOSED fence 逃生；
`/recovery/resume` 固定 ACTION_NOT_ALLOWED。不得先尝试一个注定无法分配的恢复 COMMIT。

若恢复后的 room_lifecycle=RESET_PREPARED，则它优先映射为 RuntimeHealthOverlay
RESET_INTERRUPTED，绝不能走普通/回滚 ROOM_RESUMED，也不能重新开放玩家 LINK、presence、
机器人或任何领域命令。HTTP 同样只引用第 17.1 节 health_overlay 允许集合，其中 reset 必须是
同一旧 room 的 `/session/reset operation=prepare/commit`；新 prepare 绑定当前已关闭版本并签发
新 RAM token。commit 识别持久 fence 后跳过第二条 ROOM_CLOSED，直接从第 19.5 节创建 canonical
NO_ROOM 代继续。若选中的最高有效 lineage 为 NO_ROOM，则直接进入 canonical NO_ROOM 服务态，
不生成 ROOM_RESUMED。

RECOVERING/RESET_INTERRUPTED/REVISION_EXHAUSTED 都是 RuntimeHealthOverlay：底层 GameState 保持最后已提交
session_phase、room_lifecycle 和 state_version，投影时 effective phase 显示对应 overlay。overlay
保存在 RAM 并可由启动校验重新推导，不伪造 journal 记录。

millis() 重启归零，旧绝对 deadline/due_ms 不直接恢复。AuthorityState 中每一个权威 time
source——移动、购买、拍卖、RENT_DEADLINE、付款、交易、限制区、抵押接管、建筑竞争、
CARD_CONTINUATION、BANKRUPTCY_CONTINUATION、BOT_PENDING、BOARD_OBSERVATION 和
BOT_PLANNER_CONTINUATION——都必须同时序列化 original_window_ms 与
saved_remaining_ms；创建记录时两者等于原窗口。每个普通 COMMIT 候选以其 received_at_ms
把所有 active source 的 saved_remaining_ms 刷新为
`max(0, int32(due_ms-received_at_ms))`，作为该
版本 state_delta 的一部分；无领域 COMMIT 的 bank 轮换则只允许按第 19.2 节同时规范化 C* 与
激活后的 live blob，并用单次 checkpoint_now_ms 计算
`max(0, int32(due_ms-checkpoint_now_ms))`，不另增 state_version。不得逐项重采时钟，也不得只改
snapshot 副本而让 live 仍保留旧值。
恢复时全部暂停。
PRESENCE_OFFLINE 是明确例外：启动不恢复旧离线倒计时；第 19.4 节 ROOM_RESUMED 候选按
seat_id 关闭每个旧 presence deadline_instance，并随 PLAYER_CONNECTION_CHANGED 持久化
online=false/WAIT_ONLINE。只有之后的新 session presence 建立才能分配新的 instance/due_ms。
`/recovery/resume` 的同一候选事务必须在序列化前将每个 deadline/due_ms 设为
`now + min(saved_remaining_ms, original_window_ms)`，并与 ROOM_RESUMED 在同一 COMMIT/state_delta
中落盘。全部到期源仍使用第 7.3 节全局 tuple；按第 18 节预判确有待评估 bot 时，已存在或
本次固定提交要求的新 BOT_PLANNER_CONTINUATION 是立即源，remaining=0、
due_ms=ROOM_RESUMED.received_at_ms，并按
第 18 节优先于外部写运行。禁止在 COMMIT flush 成功后再修改任何 deadline/due_ms。该流程
不依赖 NTP。

AWAIT_MOVE_CONFIRM 的 `r=min(saved_remaining_ms,60000)` 还必须在同一候选设置
`movement_deadline_ms=now+r` 与
`manual_available_at_ms=now+max(0,r-48000)`；因此崩溃前已经越过 +12s 的移动在恢复后立即
允许手动确认，而未越过者只等待精确剩余差值。已处于 WAIT_MANUAL_CONFIRM 的 workflow 不再
有 movement deadline source，恢复后 manual_available_at_ms=now 并保持立即可用。

普通恢复和回滚恢复都禁止越过 active journal 的残缺尾部追加。二者统一执行“提升后恢复”：

1. 校验请求回显；回滚另要求 acknowledge_rollback=true 和上一段的 known/范围精确匹配。
2. 以选中且逐条验证完成的 lineage 状态 V/E 为只读 base，令
   `K=NVS.active_generation+1`；目的槽必须不是 base 所在槽，在 NVS 激活前不得改写唯一 base。
3. 在目的槽写 generation=K、state_version=V、last_event_id=E 的完整 base snapshot 和匹配
   空 journal header。普通恢复通常写 inactive 槽；回滚通常覆盖损坏高代所在槽。
4. 在 BindingRegistry 锁内最终重读六槽并构造唯一候选，顺序固定为：先按 seat_id 升序应用
   全部 NVS-wins 解绑修复；再把其余 REAL_CONSOLE 中仍为 online/ONLINE_WATCH 的座位置
   online=false/WAIT_ONLINE、关闭 instance、递增 presence_generation/player_revision
   （SERVER_RESTART 不
   自动触发 BOT_TAKEOVER）；再重建 deadlines；最后生成 ROOM_RESUMED。事件顺序是至多六个
   SEAT_UNBOUND、至多六个 PLAYER_CONNECTION_CHANGED、一个 ROOM_RESUMED，共≤13，全部
   位于同一 state_delta。以 previous_version=V、committed_version=V+1 的这条首个 COMMIT
   追加到 generation=K journal；不得先写独立 release/presence COMMIT。
5. 对 snapshot/journal/COMMIT 做 flush、重开、CRC、哈希、版本和不变量验证，再原子更新
   NVS active_bank/active_generation=K；保持 BindingRegistry 锁到 live swap。最后换入 V+1
   候选状态、把相关 NVS release state 提升 COMMITTED并清除 overlay。随后对所有以旧
   NO_ROOM/RECOVERING/room context 建链且尚无 drain 的 current session，按第 13.5 节建立
   reason=ROOM_CONTEXT_CHANGED 的 RevocationDrain；已有 drain 逐字节保留并继续自身收敛，
   不补第二帧。旧 session 必须销毁并重新 LINK/HELLO 后才可 SESSION_RESUME。

这也是普通重启必须轮换一代的原因：失败 append 留下的尾部不完整记录可以被读取器逻辑
忽略，但永远不会在其后再追加有效 COMMIT、不会变成“中部损坏”，且本规格仍不需要截断
active journal。

因为 K 高于原损坏代，下次启动会直接选中新完整 lineage，不会重复要求同一回滚
确认。两 bank 均损坏、NVS meta 均无效或 schema/内容不兼容时保留诊断并进入安全
大厅，不能猜测余额。

### 19.5 新房间和两阶段安全重置

POST /api/v1/session/new 只允许 canonical room_id=0/NO_ROOM。对已完成静态解析、认证且幂等槽
miss 的新 ID，若当前存在 active room，必须在通用 expected_room_id/state_version fence 前即时
返回 409 ACTIVE_ROOM，non-cached、no-watermark；不能因请求按本 endpoint 正确填写 NO_ROOM/0
而先退化为 STALE_STATE。必须先 reset。只有当前确为 canonical NO_ROOM 时才继续要求外层
expected_room_id=0、expected_state_version=0 并执行两次通用 fence。reset prepare 校验 admin、room_id 和 state_version，返回绑定
这三者及当前 room_lifecycle 的 128 位 reset_token，30 秒失效；任一权威状态变化立即使其
失效。prepare 只允许同一非零 room 的 ACTIVE 或 RESET_PREPARED；后一状态用于断电后继续。
commit 再次携带完全相同 room_id、expected_state_version 和 token。

reset commit admission 先把当前 reset HTTP operation 槽钉住为不可淘汰的 RUNNING 槽，关闭
新写 gate，并等待除自身外所有已接受、排队或运行中的 HTTP 写 operation 到达终态；不得取消
任何已接受 operation。随后暂停 CommandBus/机器人；room_lifecycle=ACTIVE 时先在旧 lineage 用
一条 ROOM_CLOSED COMMIT 原子写入 RESET_PREPARED，已是 RESET_PREPARED 时必须复核 room/version
后跳过该 COMMIT，绝不能产生第二条 ROOM_CLOSED。fence 一旦 flush，当前 operation 在其后的
存储错误上必须保持 running、gate 关闭并显示 RESET_INTERRUPTED，不能返回普通 failed。该中间
COMMIT 使用 actor_kind=ADMIN 但 `terminal_format=NONE`；它绝不能把整体 reset operation 提前标为
succeeded/failed，最终 HTTP 终态只在 canonical NO_ROOM 已激活、镜像读回和 live swap 后写入 RAM 槽。
令新代为 G+1，在 inactive bank 创建 room_id=0、room_seed=0、content hash 全零、state_version=0 的 canonical
NO_ROOM snapshot，以及同样 room/generation、base_event_id=0、base_state_version=0 的空 journal，
flush/重开/完整读回。随后一次原子更新 NVS：active_bank=inactive、active_generation=G+1、
minimum_valid_generation=G+1、tombstone_room_id=旧 room、tombstone_generation=G+1。

NVS 提交成功后，在 BindingRegistry 锁内把六个 ACTIVE BindingSlot 的 occupant mirror 清零并
原子写回 NVS、读回验证，使其统一回到 PAIRED_UNSEATED；不得删除 binding、LMK/PMK 或 peer
identity。读回成功后以无分配、不可失败的 swap 把 canonical NO_ROOM 换为 live；mirror 写入或
读回失败则按第 7.3 节保持 gate 关闭、operation=running 并进入 RECOVERING，不能把已经激活的
NO_ROOM 回滚或返回可重试 failed。live swap 后必须先在 SessionRegistry 串行域关闭每个旧
current session 的 business gate。尚无 RevocationDrain 的 session 才按第 13.5 节冻结其旧 identity
tuple，并安装 reason=ROOM_ENDED、correlation_request_id=0、current_state_version=0 的
RevocationDrain；已有 direct 或异步 drain 的 session 必须逐字节保留原 reason/body/terminal 槽，
视为已经安装，绝不能被 reset 的 reason 2 替换或再排第二帧。
此时不得先清它的加密 session、可靠 TX、request/terminal cache 或 peer identity。drain 记录全部
安装成功后才可清 deadline、机器人动作、模拟棋盘、fault 和座位派生输入；无线发送在后台按
旧可靠序号链收敛。每个 peer 收到覆盖撤销帧的 ACK、接受合法新 LINK 或重试耗尽时，才逐 peer
销毁旧 session 并清该 peer 的旧 room request/terminal cache；本来没有旧 session 的 peer 可直接
清 cache，不补造帧。reset HTTP operation 不等待离线 peer 的无线重试，只须在返回 succeeded 前
证明所有现存旧 session 的 drain 已安装，之后的有界 drain 是后台清理；但不得清除当前 admin_session/token、
该 namespace 的 request_id high_watermark 或任何 HTTP operation 槽。当前 reset operation 必须在
原槽内写入 `succeeded` 与 canonical NO_ROOM 结果并变成可重放终态，完成读回后才重新打开写
gate。这样 commit 响应丢失时，同一 admin_session/request_id 必定重放同一成功结果，旧 request_id
也不能因 reset 被重新使用。Wi-Fi、server_id、seat_token_key、commissioning secret 和 peer
binding 全部保留。本次启动内由保留的 high_watermark 防止旧 request_id 重用；重启后
boot_id/admin_session/token/operation/high_watermark 按一般规则建立全新 RAM namespace，reset 的
唯一持久完成证据是 canonical NO_ROOM lineage、active meta 和旧 room tombstone，不得把 HTTP
high_watermark 另行写入 NVS。

NVS 更新前掉电时，G+1 无资格；旧 lineage 重放后持久显示 RESET_PREPARED/RESET_INTERRUPTED，
只允许新 admin namespace 重新 prepare 并从跳过 ROOM_CLOSED 的步骤继续。更新后，恢复器禁止
加载低于 minimum_valid_generation 的旧房间；canonical NO_ROOM 的 room_lifecycle=NO_ROOM。

创建新房间 admission/dispatch 还必须预运行验证 `active_generation+1<0xFFFFFFFE` 且当前有效
meta `sequence+1<0xFFFFFFFE`；任一当前值≥0xFFFFFFFD 返回 REVISION_EXHAUSTED，不得创建一个
落在 reset-only 代的新 room。创建新房间绝不向 room_id=0 的 canonical NO_ROOM journal 追加不同 room/hash 的 COMMIT。从当前
NO_ROOM generation=G 开始，服务端生成新 room_id，冻结地图/经济/卡组/机器人策略 hash，
并把请求 seed 解析成持久 room_seed；在内存构造一个尚未激活的 lineage base：room/hash/seed
已冻结、room_lifecycle=CREATING、
state_version=0、last_event_id=0。这个 0 版本只能作为紧接有效 ROOM_CREATED 首 COMMIT 的
新 room lineage base；恢复器必须先重放到版本 1 才能对外投影，绝不单独加载它。

在 inactive bank 完整写入 generation=G+1、新 room/hash、state_version=0 的 base snapshot，
以及同 room/hash、base_event_id=0、base_state_version=0 的 journal header。然后在候选副本把
room_lifecycle 改为 ACTIVE，生成 ROOM_CREATED，并把 previous_version=0、committed_version=1、
event_id=1 的完整 COMMIT 追加到该 journal。snapshot、journal header 和首条 COMMIT 全部
flush/重开/验证后，才原子更新 NVS active_bank/active_generation=G+1，
minimum_valid_generation 保持不变；最后以无分配 swap 换入 state_version=1 的 live state 并返回。

因此 ROOM_CREATED 仍是新 lineage 中可审计、可回放的首条 COMMIT，但其 base 位于同一
新 room/hash/generation，绝不跨空大厅 lineage。如果在 NVS 激活前掉电，G+1 被忽略；
激活后掉电，新 snapshot/journal/ROOM_CREATED 已完整可恢复。

“清除游戏”和“清除设备凭据”是独立动作；前者不得删除 Wi-Fi 或 peer binding，后者必须
另行确认并清除 commissioning secret、LMK/PMK 和全部 binding。

## 20. 秘密隔离

必须先提交以下忽略规则，再创建本地文件：

~~~gitignore
/.local-secrets/
/Firmware/TestGameServer/config/secrets.local.h
/Firmware/TestGameServer/build/
/Firmware/TestGameServer/runtime/
/Firmware/PlayerConsole/config/secrets.local.h
/Firmware/PlayerConsole/build/
/Firmware/PlayerConsole/runtime/
~~~

仓库只包含 secrets.example.h 和假测试密钥。凭据来源优先级：

1. 已明确 provision 的 NVS；服务器和玩家屏分别使用固定 namespace 与 key version。
2. 编译时 secrets.local.h。
3. 两者都缺失则启动失败并给出脱敏说明。

构建和烧录前执行 git check-ignore、暂存区检查和秘密扫描。Wi-Fi 密码、commissioning
secret、PMK、LMK、seat_token 和管理 token 分开管理，不出现在命令行、网页响应、
普通串口日志、回放或测试向量中。

Git 忽略不能防止从固件二进制提取凭据，因此 build 目录和固件镜像也不得公开。家庭
Wi-Fi 失败时仅 STA 重试并输出脱敏诊断，不创建 SoftAP。

## 21. 错误处理策略

| 类别 | 默认动作 |
| --- | --- |
| 配置或秘密缺失 | 不启动网络；串口输出脱敏修复步骤 |
| Wi-Fi 认证/DHCP失败 | 指数退避重试；不启动 SoftAP |
| Wi-Fi 换信道 | 暂停发送；重连、刷新信道和 session |
| peer 满或加密槽不足 | 拒绝新配对，不降级明文 |
| RX/TX 队列满 | 丢弃非关键补丁，标记 peer 需要完整重同步 |
| 非法、未认证或 CRC 错误帧 | 静默丢弃并增加脱敏计数 |
| 协议主版本不符 | 拒绝会话并显示升级要求 |
| 状态过期 | 返回 STALE_STATE；危险操作不自动重放 |
| 地图校验失败 | 地图不进入正式列表，显示具体字段错误 |
| 规则不变量失败 | 中止提交并进入 RECOVERING |
| Flash 写失败或空间不足 | 中止提交并进入 RECOVERING |
| 快照损坏 | 尝试另一槽；两槽失败则安全大厅 |
| HTTP 输入超限 | 400/413，不分配无界内存 |
| 机器人无合法动作 | 暂停该座位并记录诊断，不直接改状态 |
| 长时间无进展 | 看门狗前记录阶段和队列，安全重启恢复 |

只有成功持久化的领域 COMMIT 增加 state_version；被拒请求和坏帧不增加。进入
RECOVERING 是不持久化、也不增版的 RuntimeHealthOverlay，因为触发原因可能正是 Flash
不可写。ROOM_RECOVERING 使用最后已提交版本作因果标签。存储恢复后，管理员成功提交
ROOM_RESUMED 才增加一版并清除 overlay；若连该 COMMIT 也失败则继续 RECOVERING。

## 22. 测试策略

### 22.1 主机测试

- 地图 schema、五个格数、角落、数量、组和引用。
- 全部产权金额、节奏取整和内容哈希。
- 双骰、第三次双骰、起点、购买、拍卖和租金。
- 付款、筹资、均匀建设、出售、抵押、接管利息和赎回；PAY_NOW 余额不足必须以同 request_id
  成功进入 DEBT_RESOLUTION，重复请求只重放同一 DEBT_RESOLUTION_REQUIRED。
- 交易版本失效、限制区、卡牌二次落点、破产和排名。
- 多人卡牌在每个 child PAYMENT_OPEN/DEBT/PAID/BANKRUPT、parent trigger 交接点断电后，都按
  item status/child transaction/instance 唯一继续且不重复收付。
- 单人 demo 可进入 IN_GAME 且不会开局即胜；唯一玩家破产以 winner=0/
  SOLE_DEMO_PLAYER_BANKRUPT 确定结束，二至六人终局轨迹不变。
- 固定种子回放和所有核心不变量。
- 协议截断、超长、未知版本、错误 TLV、CRC、乱序和重复；当前 logical message 内同
  frame_seq/hash 冲突必须 PEER_DESYNC，已推进 closed_message_last_seq 的旧 seq 只能累计 ACK/drop；
  六 peer 各 64 项 identity 的最坏夹具必须在固定 15KiB 表内通过且下一 message 可回收前表。
- 六玩家隐私视图、VERSION_ADVANCE 和队列公平。
- 快照 A/B、日志截断、CRC、旧 schema 和断电注入；reset 在 ROOM_CLOSED fence、新 bank flush、
  meta 激活、occupant mirror 与 live swap 各边界掉电都只能收敛为 RESET_INTERRUPTED 或 NO_ROOM，
  且任何重试轨迹最多一条 ROOM_CLOSED。
- 网页输入范围、HTML/JSON 转义、认证和状态冲突。

### 22.2 黄金向量

向量包含语义输入、方向、假 MAC、信道、是否要求加密、完整 frame hex、解码结果、
ACK 行为和预期状态影响。至少覆盖 ACK、PING/PONG、ROLL_REQUEST、STALE_STATE、
UTF-8、250 字节边界、多片快照、乱序、缺片、CRC、超 8KiB、未知 TLV、重复请求、
请求冲突、旧会话、六玩家隐私、VERSION_ADVANCE 和未加密游戏消息。

第一个固定 ROLL_REQUEST 向量：

~~~text
47 50 01 00 28 01 00 10
44 33 22 11
88 77 66 55
01 00 00 00
00 00 00 00
00 00 00 00
01 00 00 00
2A 00 00 00
0D 00
00 01
01
04 03 02 01
00 00 00 00
0D 0C 0B 0A
~~~

测试密钥必须是固定假值。

回放黄金向量还必须包含 `GameData/tests/replay-export-v1.json`：以最小合法 snapshot 与至少一条
完整 COMMIT journal prefix 生成 schema=1 container，逐字节断言 132-byte header offsets、
history_truncated、payload SHA-256、两个 CRC、首尾 file_length、整文件 SHA-256、解析后 lineage
字段和 replay end_state_version/end_event_id。每个负向量必须声明 `expected_first_error`，并为要
命中的目标层重新计算所有更早的外层长度/CRC/SHA；journal 半条记录向量尤其要重算首尾
file_length、payload SHA、header CRC 与 file CRC，使 container 外层全部通过后固定在原生 journal
record-boundary 阶段拒绝。不得用一个任意翻转字节的样例冒充所有校验层覆盖。

### 22.3 4MB 最低 Flash 配置与资源预算

4MB 是受支持最低值，不使用 OTA。Firmware/TestGameServer/partitions.csv 冻结为：

~~~csv
# Name,      Type, SubType, Offset,   Size
nvs,         data, nvs,     0x9000,   0x5000
phy_init,    data, phy,     0xF000,   0x1000
factory,     app,  factory, 0x10000,  0x2F0000
littlefs,    data, littlefs,0x300000, 0x100000
~~~

最终 app binary 不得超过 0x2B0000，至少留 256KiB app 余量。LittleFS 1MiB 中两份
snapshot 128KiB、两份 journal 256KiB、单个 replay export 256KiB、scratch 64KiB，剩余
至少 320KiB 供文件系统元数据、临时文件和安全余量。网页 raw PROGMEM、地图生成表和协议常量
默认编入 app，不占 LittleFS。

默认 profile 名为 gridopoly_4m_no_ota。若 COM5 探测确认通用 ESP32-S3 Dev Module，
Arduino-ESP32 3.3.11 的可执行 FQBN 冻结为：

~~~text
esp32:esp32:esp32s3:FlashSize=4M,PartitionScheme=huge_app,PSRAM=disabled,USBMode=hwcdc,CDCOnBoot=cdc
~~~

huge_app 只用于让 Arduino 上传 size-check 上限达到 3MiB；sketch 目录中的 partitions.csv
必须覆盖实际分区。构建后用 3.3.11 自带 gen_esp32part.py 反解析生成的 partition-table.bin，
逐项比对本节 CSV，并另行断言 app binary≤0x2B0000。若 VID/PID、Flash 或 USB 模式不符合
通用目标，必须建立另一个显式 profile，不能偷用玩家圆屏配置。

CSV 的文件系统标签为 littlefs，因此挂载必须显式调用
LittleFS.begin(false, "/littlefs", 10, "littlefs")；禁止依赖默认的 "spiffs" 标签，也
禁止 mount 失败时自动格式化。首次格式化只能在确认两个 bank 均不存在的 provisioning
流程中显式执行。

启动自检确认实际 Flash≥4MiB、LittleFS totalBytes≥1MiB、挂载/写/flush/重开成功、空闲
≥128KiB、无 PSRAM 也能运行，以及加密 peer 配额≥6。任一失败不启动游戏。8MiB 以上
profile 可以扩大容量，但不得缩小上述 app、FS 或安全余量。

无 PSRAM 固定工作集上限为 180KiB：RX ring 8KiB、TX frame pool 12KiB、单个快照
重组 8KiB、六 peer 连续补丁共享池 24KiB、ESP-NOW 终态缓存 64×264B=16.5KiB、
未完成请求槽 24×64B=1.5KiB、HTTP 幂等/operation 槽 16×640B=10KiB、HTTP 工作缓冲
8KiB、GameState/live+候选+事务 64KiB、存储流式 scratch 4KiB、可靠当前消息 identity 表
`6×64×40=15,360B`（15KiB）、其他固定队列与诊断 9KiB；最后一项明确包含八个 fault 控制块及其总计≤2KiB 的物理帧 hold/copy buffer、八个
board admission reservation 和 timer-ready 排序 scratch。上述池在网络对外可用前一次分配
完毕，运行期不得按 peer 数或响应数无界增长。

identity entry 必须 `static_assert(sizeof(ReliableFrameIdentity)==40)`，二维表必须
`static_assert(sizeof==15360)`；其他固定队列/诊断的链接 map 总和必须≤9KiB。若平台 ABI 使任一
断言或整体 180KiB 链接预算失败，构建失败，不能暗中从 heap 补足。

其中 64KiB 不是模糊共享池，而是两个固定 `GameStateArena` 各 24KiB（live/candidate）加
一个 16KiB `TransactionWorkspace`；编译期分别 `static_assert(sizeof<=24576/16384)`。
候选从 live 完整复制到另一 arena 后原地修改，成功 swap 只交换 arena 索引；所有状态容器
是固定数组且这三块运行期不再 heap allocate。AuthorityStateBlob、state_delta 和 COMMIT
不完整装入 arena/workspace，而只经独立 4KiB 流缓冲写 storage-scratch 并读回校验。
生成器必须构造六人、40 格/28 资产、最大牌/交易/auction ledger、全部 continuation、八个
board pending、八个 active fault、32 事件和三段 bot planner 的最坏夹具，断言两个 arena、workspace、24KiB blob、56KiB COMMIT
及 180KiB 总高水位；任一预运行超限必须在写 Flash 前 INTERNAL_LIMIT，不能靠 PSRAM 才正确。

Wi-Fi、ESP-NOW、HTTP、LittleFS 和上述池全部初始化后，启动门禁要求 internal free heap
至少 64KiB、largest free block 至少 32KiB；不足则仅启动诊断，不开始游戏。PSRAM 只能
优化性能，不能成为正确运行条件。

### 22.4 COM5 硬件验收

烧录前用 esptool/Arduino CLI 确认 COM5 的芯片、Flash、PSRAM、VID/PID 和 USB 模式。
不得复用玩家圆屏的 16MB/OPI PSRAM FQBN。目标最低 4MB Flash；PSRAM 可选。端口必须
显式传入，重枚举后按 VID/PID 重新定位，禁止选择“第一个 COM”。

启动自检还必须确认 ESP-NOW 加密 peer 配额至少为六；不足时拒绝开启多人配对，不能
改用明文或减少而不提示。

验收项目：

1. 串口显示版本、构建哈希、复位原因和自检，不显示秘密。
2. Wi-Fi、DHCP、信道、mDNS 和 HTTP health 正常。
3. 五地图均可开局并校验。
4. 机器人完整推进且版本单调。
5. 一台真实圆屏完成配对、快照、掷骰、移动、购买、租金和付款。
6. 重复请求、短断线、换信道和完整重同步正确。
7. 服务端重启按本规格暂停恢复。
8. 六个逻辑 peer 的调度和隐私测试通过。
9. 两小时老化后空闲堆相对预热稳定点下降不超过 2KiB，largest block 下降不超过
   4KiB，队列无净增长。
10. git status、git check-ignore 和扫描确认无真实秘密。

老化预热稳定点是自动场景运行满 10 分钟后的首次采样；之后每 60 秒记录 internal free
heap、largest block、各队列长度和 LittleFS 使用量。验收比较第 10 与第 120 分钟附近
各五次采样的中位数，避免单次抖动误判。

## 23. 文档和玩家屏交接

实施阶段必须新增或更新：

- Docs/game/map-economy-spec.md：五种完整地图和经济表。
- Docs/game/game-rules.md：规则缺口裁决。
- Docs/game/game-content-catalog.md：第六角色和地图引用。
- Docs/firmware/test-game-server-espnow-protocol.md：新增 A 级测试服务端/圆屏规范入口。
- Docs/firmware/espnow-transport-v1.md：由同一 schema 生成的逐字节线协议与黄金向量。
- Docs/firmware/main-controller-protocol.md：保留树莓派 WPA2/WebSocket 适配器边界，只补共享
  语义 schema 的交叉链接，不把它改写成 ESP-NOW 传输文档。
- Docs/firmware/test-game-server.md：构建、烧录和网站入口。
- Docs/player-console/espnow-integration.md：圆屏接入入口。
- Docs/game/balance-reports/：模拟配置、结果和版本决定。
- Docs/README.md 与根 README.md：导航入口。

当前 design-record 在既有文档优先级中不能静默覆盖 A 级主控协议。实施计划的第一批文档
提交必须把 `test-game-server-espnow-protocol.md` 登记为 A 级并更新 Docs/README 的优先级表和
阅读路径，明确“树莓派主控 + WebSocket”与“ESP32 测试服务端 + ESP-NOW”是共享纯 C++
语义核心的两个并列 adapter；发生语义公共字段冲突时先修订共享 schema 和两份 A 级入口，
不得让开发者凭文档日期猜优先级。

玩家屏交接包包含：

- 新建 PlayerConsoleTransport/EspNowTransport 到消息号的逐项映射。
- 配对、信道扫描和恢复状态机。
- 快照 TLV 和内容哈希。
- 全部错误码、deadline 校时和 pending 规则。
- 黄金十六进制向量。
- 固定端到端场景轨迹。

## 24. 树莓派迁移

- 树莓派通过 add_subdirectory 直接链接 gridopoly_core 和 gridopoly_protocol。
- 正式主控并发请求先进入单线程 actor 队列，核心仍串行裁决。
- WebSocket JSON 和 ESP-NOW 二进制映射相同语义类型。
- 树莓派直接 WebSocket 适配器可复用语义 schema，但不属于本轮 ESP32 测试固件交付。
- ESP32-S3 网关模式不属于本规格保证；若采用，Pi↔ESP32 网关协议必须另立规格，不能
  隐含复用 HTTP 管理 API。
- 地图、协议、状态和事件 schema 各自版本化。
- ESP32 固定容量和 Flash 优化不得改变领域接口。
- TestGameServer 保留为参考适配器，不能演化成第二套规则分支。

## 25. 分阶段交付与独立计划门禁

本文件是总体语义设计，不冒充当前即可由两个团队独立生成同一字节的 sole byte spec。
截至本设计记录提交时，仓库还没有 `GameData/schemas/domain-events-v1.json`、
`game-state-v1.json`、`admin-projection-v1.json` 和 `http-events-v1.json`；正文虽已冻结语义、
容量、状态机与大量 wire 字段，但没有枚举全部 domain FieldTLV 和 12-section AuthorityState
FieldTLV。因此用户批准本语义设计后，writing-plan 的第一个且严格串行交付必须是下述
P0-Contract；在它独立复核通过前，禁止并行实现 server writer、replay/recovery reader、网页
reducer、ESP-NOW 圆屏 codec、树莓派 adapter 或 COM5 固件，不能让各团队自行补字段号。

P0-Contract 只交付规范合同、机械生成器和合同测试，不实现游戏/无线/网页功能，必须同时包含：

1. domain-events-v1：全部 event_schema/flags，以及每字段 id、stable name、wire type、
   CRITICAL/PRIVATE/required、cardinality/range/enum/sort；所有 RECORD_LIST item layout。
2. game-state-v1：12 个 section 的全部 field/default/max、嵌套 record/array，以及
   blocking/continuation、card multi-payment、debt D↔U、bot fence/pending、deadline/source 布局。
3. admin-projection-v1/http-events-v1：第 17 节每个 JSON key/type/null/redaction/sort/error mapping；
   system-triggers-v1/bot-actions-v1 也由同一编号表机械生成并锁 hash。
4. exact-byte goldens：一条简单 event batch，以及至少一条同时含 PRIVATE、unknown optional 与
   RECORD_LIST 的最大 batch，逐字节 hex/length/CRC；canonical NO_ROOM AuthorityState blob
   hex/SHA、一个最大 active-workflow blob、一次 delta→post_state_hash→snapshot CRC round trip。
5. content identity goldens：按第 10 节生成 16/40 格完整 asset ID 清单、CE/CF 两牌堆 ID 清单与
   canonical hash/vector；resolved-deck、map release_status 和 manifest 六字段 entry 交叉校验。
6. `Docs/design-records/schema-closure-report.md`：逐项证明正文引用都有真实 schema/向量、无 TODO/
   placeholder，并由独立合同审计 PASS。未来 JSON 是这些冻结合同的规范载体，不得反向改语义。

每个后续阶段必须有独立实施计划、测试门禁和复核，不得合并为一次无检查点的大实现：

1. P0-Contract 字节合同闭包：仅完成上述 schema、generator、golden 与独立复核；门禁是
   schema-closure-report PASS，且其他实现仍为零。
2. P0A 仓库与秘密安全：ignore、example/local 边界、扫描；门禁是所有真实秘密路径
   git check-ignore 通过。
3. P0B COM5 与构建基线：芯片/Flash/USB 探测、4MB partition、最小固件；门禁是显式
   COM5 编译、上传、自检和 LittleFS 验证。
4. P1 CMake 与 GameData 生成器：主机 targets、schema、manifest、哈希；门禁是主机测试
   与 generator check。
5. P2 五地图、经济和卡组：全部数据和 MapValidator；门禁是结构、引用、金额全通过。
6. P3 核心回合与移动：骰子、双骰、起点、限制区入口、移动确认、二次落点；门禁是固定
   事件轨迹和不变量。
7. P4 购买、租金、付款和债务：窗口、拍卖入口、收租、筹资、continuation；门禁是
   deadline、重复请求、现金不足和候选状态测试。
8. P5 高级经济事务：完整拍卖、建设、出售、抵押、赎回、交易；门禁是所有权、均匀
   建设、库存和回滚测试。
9. P6 限制区、破产和游戏结束：多人收付、转移和排名；门禁是二至六人 host 游戏可
   确定性结束。
10. P7 语义协议与 codec：全部 body、错误码、信封、分片和黄金向量；门禁是截断、边界、
   乱序、重复和跨平台向量。
11. P8 可靠层、密码学和六 peer 模拟：ACK、调度、TEST_PSK_V1、协商；门禁是无硬件的
    六 peer 故障注入。
12. P9 网页 API、机器人和场景：有界 JSON、认证、分页、导出、BoardPositionAdapter；
    门禁是 API 契约和固定机器人回放逐字节一致。
13. P10 LittleFS/NVS 与恢复：安全 WAL、双 bank、轮换、reset 和断电；门禁是每个写边界
    只恢复提交前或提交后状态。
14. P11 Arduino Wi-Fi 与 ESP-NOW：STA、信道、加密 peer 和 COM5 实物服务器；门禁是
    丢失、重复、换信道和重同步。
15. P12 玩家圆屏适配：创建 transport、codec、reducer 和 secret provisioning；门禁是
    实屏完成配对、认领、掷骰、移动、购买、租金、付款和重连。
16. P13 端到端验收：一实屏加五逻辑玩家、恢复、老化和脱敏；门禁是第 4、22 节全部
    成功标准。
17. P14 平衡发布：全部地图/人数/节奏/策略模拟和报告；未通过组合保持 candidate。

每阶段实施计划必须列出新增/修改文件、先写的失败测试、执行命令、预期失败、最小实现、
回归命令、实物可观察结果和进入下一阶段的唯一门槛。

## 26. 参考

- Hasbro Monopoly Classic Game 官方说明：
  https://instructions.hasbro.com/en-us/instruction/monopoly-board-game-classic-game-with-storage-tray-and-larger-tokens-family-games-8
- Espressif ESP-NOW API：
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/network/esp_now.html
- Espressif ESP-NOW FAQ：
  https://docs.espressif.com/projects/esp-faq/en/latest/application-solution/esp-now.html
- 项目现有规则、状态机、内容目录和主控协议位于 Docs/game 与 Docs/firmware。
