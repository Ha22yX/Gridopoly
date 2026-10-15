# 全部在线模块闭环自动分配：服务器进度（2026-10-15）

本轮按主任务转达的最新要求实现：全部在线模块经 ORDER 证明属于同一个完整环，且模块数匹配当前游戏格子数时，自动沿 OUT 顺序分配；corner 对应地图角落，long 对应长边。旧 16/32 实现的逐 component 投影不满足“全部在线”，本轮修正这一条件，并覆盖实际目录中的 16/24/32/40 格地图。

主任务完成复审后统一提交源码检查点 **`0fed0ce`**（后端/测试四文件、网页三文件）。冻结源快照仍为提交前的 `20261015-065439`，其 129 项内容与检查点之后工作区一致；Git HEAD 变化不等于源码漂移，没有因此重建或改写原构建日志。本报告另由主任务归档。

## 实现范围

- `TileOrderProjection.h` 的物理结构判定不再硬锁 16/32；4～64 节点、四等边、四角且其余为 long 的环可被识别。地图支持范围仍取实际 BoardCatalog，不据此新增 4/8/12/48/64 等游戏地图。
- `TileDebugAssignments.cpp` 以 `expireLocked` 后的 `modules_` 为全部在线集合，即当前 15 秒 HTTP 租约内模块。闭环的成员集合必须与它严格相等。额外 legacy 无 ORDER 模块、unknown 角色孤岛、未知上游节点、第二个环均不能被忽略；历史离线记录不计入该集合。
- 再检查物理环长度等于当前地图格数，读取实际 `BoardDefinition.tiles` 验证 Start/Hold/Rest/GoToHold 各一个，位于等距四角且 START 为默认索引 0。当前四张地图全部满足；同格数但角落错位、角数错误或 START 错位的布局会被拒绝，不能只凭格数推断正确。
- 保留最小 corner moduleId 的确定性根、沿 OUT 方向、默认自动 START 和四种合法旋转；不要求用户先手动指定 START。手动锚点仍可重新指定整环相位，不一致锚点返回冲突且不改变既有映射。
- 全在线条件失效时撤销环的 `source=order` 派生，保留显式 manual 及锚点意图。地图/相位不兼容沿用整环撤销显示、保留意图的规则。开放链的人工分段锚点调试保持不变。
- 最后一条闭环证据、在线集合变化、证据/模块租约失效、重连和新局/地图切换均由现有 refresh 自动重新评估。assignment 撤销/恢复推进既有 revision；失去 assignment 的心跳返回 `assigned=false`、`movementCue.mode=none`。

物理闭环标记保留，和当前可分配状态分别表达。新增原因键：

| 原因键 | 含义 |
| --- | --- |
| `ring_online_set_mismatch` | 该物理环没有涵盖全部在线模块 |
| `ring_size_mismatch` | 物理环节点数不等于当前地图格数 |
| `ring_board_layout_mismatch` | 当前地图实际角落布局不兼容 |

原 `ring_or_tile_conflict` 继续表示环锚点相位或格子占位冲突。网页及其 gzip 由主任务负责，本端纳入冻结构建并核对 SHA，没有编辑网页或共享交接/设备/协议文档。

## 验证记录

首轮完整 `gridopoly_tile_debug_assignment_tests` 于 Pi 隔离目录 `/home/kicofy/gridopoly-all-online-ring-20261015.mVuKlV` 通过。源快照与日志：`C:/Users/kicof/AppData/Local/Temp/gridopoly-all-online-ring-20261015/topology-20261015-065344/`。

新增/扩展的回归包含四尺寸默认映射和全部合法锚点旋转、矛盾锚点、实际角落类型错误、额外在线四类节点在成环前后加入、第二个独立环、额外模块过期后自动恢复、环中单节点失联/旧证据拒绝/新证据恢复、分叉、手动意图保留、revision 和 cue 撤销。既有开放链、重复/退休 boot、序列回绕、租约和 JSON 解析回归继续运行。

最终冻结的三个完整目标与正常 Linux 程序构建全部通过：

- `gridopoly_tile_debug_assignment_tests`：服务、拓扑、锚点、租约和本轮四尺寸新增回归全部运行。
- `gridopoly_http_asset_integration_tests`：完整 HTTP/资源集成目标通过。
- `gridopoly_onboarding_network_tests`：真实回环 HTTP/UDP fixture；四尺寸倒序注册、最后闭环证据自动分配、角色错误撤销/恢复、锚点旋转/拒绝冲突、断环/重启、HTTP 新局切换地图、额外无 ORDER 在线模块阻止自动分配均通过。逐模块心跳检查撤销及 `movementCue=none`。游戏业务状态不因 ORDER 更新改变。
- 正常 `gridopoly_server` 通过 g++ 14.2、C++17、`-O2 -Wall -Wextra -Werror -pthread` 构建；测试未定义 NDEBUG，assert 启用。测试仅用回环临时端口和独立临时数据，不向生产端口注入测试。

通过 `build-and-test-native.sh` 的临时副本选择上述三个完整目标后构建正常程序；没有将此结果称为全部仓库测试通过。无关的三个 Python 资源预检不在本次选择范围，网页完整回归由主任务完成。

最终候选归档：`C:/Users/kicof/GridopolyReleases/server-all-online-ring-20261015-065439/`；Pi 构建目录：`/home/kicofy/gridopoly-all-online-ring-20261015.mwIzye`。候选 ELF 为 ARM aarch64，637832 bytes，SHA256 **`ea6e1f517c597cc69f6f0724af0d501206cf3d57e457945c9cc138fc3f2d118d`**。

2026-10-15 06:56:55 UTC 核对 129 项输入的当前工作区、归档源包和 Pi 构建副本 SHA 全部一致，三个 drift 列表为空；下载候选 SHA 与 Pi 构建程序一致。归档包含 `source-manifest.json`、`sources.tar.gz`、`native.log`、`run-ring-tests.sh`、`verification.json`、`verification.log` 和 `gridopoly_server`。冻结的主任务网页文件如下：

| 文件 | SHA256 |
| --- | --- |
| `Firmware/TestGameServer/src/WebUi.h` | `c606bafaca242073a4892861ee5be026384c2de1897e416fbd9e3aa2c3882bb2` |
| `Firmware/TestGameServer/src/WebUiGzip.h` | `3b9b237c62b7deeeee3bda59fbc7e4bed46a8b77ff7db4d647584456fd6c0b3d` |
| `Firmware/TestGameServer/tools/test-web-ui-layout.mjs` | `a73399eeaacf67ecda999a55ec894fa7a8e852afa6a66d4239cb2f35bd78b905` |

候选交付阶段本端仅修改两份服务端文件、两份测试和本报告，范围内 `git diff --check` 通过。该阶段未运行生产接口写操作，未修改 AP、占用串口、部署或重启服务，未操作共享 Git 索引；保留已有 AP 历史 dirty。后续获主任务明确授权的生产部署见下文。

## 部署与实际验收边界

冻结交付时尚未部署：06:56:55 UTC 只读核对生产 PID **10677**，active/running，`/proc/10677/exe` SHA 为 **`ae01f6e5032d66c2e24aa47ba8b4d2e6a344b2c282575e4f0b11c3e1f2723af0`**，已有 16/32 功能。随后已完成下述最终生产部署；不以最初的源码候选检查代替实际进程验证。

尚未完成真实 16 板同时在线闭环、现场锚点重派和实体完整一局验收。格子端已向主任务确认 V0.34 可以消费现有 assignment/revision 协议，当前尺寸扩展不要求本端修改或重刷格子固件；这不替代实机验收。下一步由主任务协调现场供电、完整闭环与真实游戏验收。

## 第一次生产部署：核验触发回滚，原因已确证

主任务复核并授权一次部署后，于 07:03:23.548 UTC 正常 stop，完成一致存档快照、原子替换后，07:03:24.616 UTC 新程序就绪。候选 PID 58902 / SHA `ea6e1f51…`，新网页 gzip/ETag 正确；20 秒有界 GET 观察、完整 HTTP 业务和真人原设备席位恢复均通过。随后本端新增的存档逐字节断言触发 `state.bin business bytes changed`，流程按既定策略于 07:03:45.306 UTC 回滚旧程序和原一致快照，没有将这次标为部署成功。

证据目录：候选归档下 `deployment-20261015-065957/`；Pi 一致备份 `/var/backups/gridopoly/server-all-online-ring-20261015-065957`；失败时数据完整保留 `/var/lib/gridopoly.failed-20261015-065957`。私有 `recovery.tar.gz` 已下载，256 文件及旧程序逐 SHA 验证通过，包 SHA256 `302cafea2ba9dee65bf3c7a1d45b6ac595971a30c4e4d96e1c7a12014d48493a`，不进入普通分发包。

只读诊断和主任务独立复核确证：正常 stop 会先保存真人离线状态；运行中玩家重新连接。冻结 C++ 的真实结构偏移和当时文件哈希证明，除版本/CRC 外，差异只有：

- `state.bin` offset 41：玩家 1 `connected`，0→1。版本 offset 1928：18→20；CRC 位于 1948～1951。
- `identity.bin` offset 45：玩家 1 flags，1→5，仅 connected bit 4；identity revision offset 24：21→22，CRC 位于 12～15。human/bot/ready 等其它位没有变化。

用失败停服文件恢复上述字段并重算 CRC 后，分别精确复现原 `data-after.json` SHA `b83824eca3ff69326722648a175abc08f0dfee3223e8a2649837bd721149fb76` 和 `23da15dd82d4956dc1aa1d9dbe13195f24fd772b8e934f2138ea61f6d678de20`，全部其余字节一致。详见 `presence-byte-proof.json`。这是部署验收脚本未计入正常 presence 恢复，未发现生产源码业务缺陷；未据此重建候选或修改游戏。

回滚后实际 PID 59045，旧 SHA `ae01f6e5…`，room `993580111` / version 20，原真人在线席位 1、三机器人、16 格和存档业务保持；AP/DHCP/watchdog PID 819/820/821 不变。主任务独立复核上述实际字节及 AuthorityService/FileIdentityStore 后，授权使用新快照和新备份执行第二次最终部署：仅对实际真人的 connected 字段/bit、版本及 CRC 归一化，继续要求其它存档业务逐字节一致，以及 20 秒最终 HTTP 完整 presence/设备席位恢复。第一次失败和回滚证据保留。

## 第二次最终生产部署：成功，存档和原席位保留

使用同一 `ea6e1f51…` 候选，没有修改生产源码或重新构建。新的紧邻前快照记录 room `993580111` / version 20、16 格、真人席位 1 在线与三个机器人。于 **07:09:56.050 UTC** 正常 stop 后取得新的一致数据备份，再原子替换程序并 start；**07:09:56.681 UTC** 就绪，停服窗口约 **0.631 秒**。此次没有回滚。

实际生产 **PID 61309**，`/proc/61309/exe` SHA256 **`ea6e1f517c597cc69f6f0724af0d501206cf3d57e457945c9cc138fc3f2d118d`**。服务 active/running，`NRestarts=0`；AP/DHCP/watchdog PID **819/820/821** 与部署前完全相同。没有修改 AP 密码/配置、串口、新局、锚点、Tag 或任何游戏输入。

部署后 20 秒、10 组 `/health`、`/api/sync`、`/api/player-devices`、`/api/tile-debug/assignments` 只读观察全部通过：

- 房间、地图、玩家、现金、位置、资产、身份设置、Tag、冻结设备与原席位恢复；最终完整业务 diff 为空。真人设备仍为原设备席位 1，四个玩家均连接。
- 版本 20→23 对应正常停服离线、恢复和重连；identity revision 22→24、epoch、tagPairing session 的变化单独保留在 `sync-diff-final.json`。本次 tagPairing revision 没有变化，也没有把这些元数据变化写成游戏推进。
- 256 存档文件全部存在，254 份完全同字节。`state.bin` 和 `identity.bin` CRC 验证通过，仅对版本/CRC 和上述实际真人 connected 字段/bit 归一化后，全部其它字节严格一致；其它身份 flags、机器人字段、私有财务历史、随机状态等均不豁免。
- 新网页 HTTP 200 / gzip，HTML **93169 bytes**、gzip **25696 bytes**，ETag **`gridopoly-ui-d3b3b4a4d2ea`**。解压 SHA256 **`d3b3b4a4d2ea4d1d080afabce543a8fafdeda81e98e7a7585d9e5f1ec13e912b`** 与冻结页一致，三个新增冲突原因的中文文本已核对存在。
- 实际在线格子仍为 0，因此此次部署并未证明真实 16 板完整闭环、现场锚点旋转或实体一局验收。

新 Pi 私有一致备份 `/var/backups/gridopoly/server-all-online-ring-20261015-070941`；本地完整证据位于候选目录下 `deployment-20261015-070941/`。离机 `recovery.tar.gz` 为 **7451371 bytes**，SHA256 **`3a745ad60ed7db084a630978d5ccf7002ae926125da8c19f6f2e530b0f9340ff`**，256 数据文件与旧程序逐 SHA 复核通过，`recoveryDataDrift=[]`。

普通 v3 包只使用候选目录顶层 **`deployment-summary.json`**（`deployed=true`，包含 `binarySha256` 和第一次回滚原因）；完整 JSON 快照、两次私有恢复包和证据目录不进入普通发布包。原冻结 `verification.json` 保留 06:56 的构建时事实，不回写原始日志。所有 SSH/采集进程已经结束，无后台轮询；源码、回归、正式部署阶段完成，剩余实体闭环和真实游戏验收继续由主任务协调。
