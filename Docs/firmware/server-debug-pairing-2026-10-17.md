# 服务器右键调试与开局配对修复 · 2026-10-17

本报告由服务器会话交付，主会话补记部署结果。源码、测试和原生最终候选构建均已完成；2026-10-17 21:03 EDT 主会话已部署 `81b79bec…`。现有房间、现金、归属和 Tag 保留，AP 配置未改。下文冻结阶段的“尚未部署”与旧 PID 为当时记录，最新状态以本段及末尾部署补记为准。

## 交付行为

- 右键玩家新增“调试：修改资金”；右键可交易的地产、交通、公共事业格新增“调试：分配所有权”。选择银行或未破产玩家，显示对象与旧值→新值，确认才提交，取消无写入。非资产格没有归属入口；模块硬件分配仍只提交 module/device/tile，不接受伪造 owner 覆盖。
- 专用 `POST /api/debug/player-cash`：`roomId`、`expectedVersion`、`playerId`、`cash`；`POST /api/debug/asset-owner`：前两项及 `tileId`、`ownerId`。四个参数必须完整，数值严格十进制全串解析，不接受负数、小数、指数、溢出、尾随字符、重复参数、额外参数或请求体。银行 ownerId=0。
- 现金范围 0–1,000,000；只允许 AwaitRoll 或 TurnEnd，且没有移动、购买、卡片、债务、拍卖和 Offered/Countered 交易。拒绝破产玩家、抵押资产和所在地产组仍有建筑的归属修改。修改不模拟购买，也不扣购买款。受限情况明确返回错误。
- Authority 在同一互斥区核对房间、版本和对象，复制候选引擎，先保存身份与候选游戏存档，成功后发布引擎。失败保持原内存状态、版本、事件序列和账本。Linux 存档 rename 失败直接返回，取消删除旧存档再重试的回退路径。相同值不增加版本或事件。
- 独立事件 `DebugCashChanged=31`、`DebugOwnerChanged=32`。现金事件 amount 为有符号差额，detail 为新余额；归属事件记录原所有者、新所有者和资产，amount=0。现金调整进入现行账本及旧存档事件投影。协议字段长度、存档 schema 不变，旧 wire decoder 接受新增事件值；玩家端明确标签由玩家会话负责。
- 前端保存期间停旧轮询、禁止重复提交；打开表单后的房间/版本变化会禁止保存并提示重新打开。服务器并发版本校验仍为最终依据。请求超时提示刷新确认，避免将未知结果误报为失败回滚。

## 配对根因与修复边界

已确定一条可复现的软件故障路径，但未捕获用户此前那一次现场原始轨迹：V0.37 格子实际 `publishTagInventory` / `publishTagStatus` 路径在未完成碰撞扫描或移除去抖时会保留库存，同时报告 `fresh=false`。旧服务器立即将 `freshUntil` 置零，随后 `tick` 进入 ReaderOffline；存在其他新鲜读卡格时立即更换 reader/session。玩家看到放棋子的目标换格，与用户症状吻合。

格子会话的生产函数回归进一步确认：同 UID 完整扫描→不完整扫描时库存 revision 可不变、scan sequence 前进、fresh=false；恢复完整扫描后同 UID age 刷新。第一次 miss 保留 UID，第二次去抖移除后仍可 nonfresh，第三次完整空扫描才 fresh。服务器未放松这些生产证据门槛。

本次保留当前 reader 最后一轮有效扫描的原有 5 秒租约。暂态不清该租约，也不延长它；暂态、缓存扫描和更换标签的旧扫描均不能绑定、清除拿回围栏或成为新 reader 的选择依据。真正到期、设备或 boot 变化仍使旧围栏失效；即便没有中间 tick，下一份报告处理前也先检查到期。overflow 保留原 reader 提示但进入 MultipleTags，必须重新获得新鲜空扫描。room/session/scan 序列与先空后放门槛保留。

`begin` 清读卡缓存、boot/device、scan fence 和 emptySeen；旧非零 room 的报告被忽略，防止新局使用旧读卡发现信息。已绑定玩家持续保留 SelfBound/CanEditIdentity；前一玩家待拿回的 durable removal cue 仍独立存在，不能被暂态空库存清掉。

主会话只读现场 `initial-live.json` 显示 room993580118/version91/phase6、16/16 模块在线、配对 complete；已完成快照不能证明历史故障的唯一原因。玩家屏重连旧提示的新鲜度问题由玩家会话另行处理。5 秒内无法获得完整可信扫描的电气/网络情况仍会正常超时换格，不保证所有换格均被消除。

## 验证证据

- 模型最终 22/22 PASS；对生产 2efc59f4 对应旧 `TagPairingModel.h` 运行同一组最终测试为 18/22。四项失败明确覆盖暂态换格、无中间 tick 的租约到期、新局 reader 缓存以及 overflow 保留 reader 但要求重新清空。旧失败日志保留。
- 权威配对 16/16 PASS，核心规则、协议、财务投影、Authority 持久化和交易回归通过。最终快照上重跑 Authority 持久化、完整 HTTP 与增强 HTTP/UDP onboarding，均 PASS，发布版编译 PASS。之前通过组对应 005528 快照；最终 005842 与它仅 8 个文件不同，Core/Protocol/TagPairingModel 及模型测试逐项哈希相同，比较证据在 prior-test-input-comparison.json。最终 HTTP 内还直接执行四种地图核心调试约束。
- 真实 HTTP 集成新增四种地图的核心调试约束、合法设置/银行归属/no-op、错误数值/ID/重复参数/方法/请求体、旧版本与新局、立即存档恢复、账本 wire 往返、写盘失败保留原值、同版本双编辑仅一项成功。
- HTTP→UDP onboarding 新增第二个在线读卡格存在时 fresh=false→同 scan 改 fresh→新完整扫描序列：前两步不换 reader、不绑定，第三步原 session 成功。
- `test-web-ui-layout.mjs` PASS；生成 gzip 与 HTML 一致。原全局禁所有权入口的旧断言收窄到硬件分配接口，保留防伪造覆盖约束。
- `test-web-debug-edits.mjs` 在 Edge/Playwright 的临时 loopback fixture PASS：打开/取消零写入、现金/归属明确提交、非法值禁用、非资产格隐藏、旧版本/房间禁用、服务错误保留表单和旧值、Escape、1440×1050 与 390×844。截图已人工查看。浏览器 fixture 只证明 UI，真实后端语义由上述原生 HTTP 测试验证。
- 首轮模型测试中旧“从 disabled begin 保留 discovery”的选择用例与新局清缓存冲突，已改为当前会话真实 Retry 选择，保留空格优先和避免上一格断言。第二轮 HTTP 测试暴露重复 query 直接断连接，已修为路由级 400。失败日志保留，不把它们记为通过。

浏览器证据：`C:/Users/kicof/AppData/Local/GridopolyNetworkAudit/debug-edit-browser-20261017/`，含 desktop-tile-menu、desktop-owner-form、desktop-cash-form、mobile-tile-menu、mobile-owner-form、mobile-cash-form PNG 和 result.json。

正反模型及此前通过组：`C:/Users/kicof/GridopolyReleases/server-debug-pairing-20261018-005528/`。冻结目录按 UTC 命名，仍为纽约 10 月 17 日晚。

## 冻结结果与后续负责人

最终候选：`C:/Users/kicof/GridopolyReleases/server-debug-pairing-20261018-005842/`；Pi 隔离目录 `/home/kicofy/gridopoly-debug-pairing-20261017.EB2FKD`。

544 项 source-manifest.json SHA256：`e085bc9575a91a04c0566f69079da2d8d94cf9414c2ee0c1b1ad473566fd32f0`。本轮目录含冻结源归档、所用测试脚本、native.log、生产 PID/SHA 前后记录及远端/工作区核验。

最终 `gridopoly_server`：644832 bytes，SHA256 `81b79bec250b4cf3e52c9d315ea442c5cdabf4d6083005a344eadf9490de17cd`；本地下载、远端二进制一致。544 项远端与工作区来源均无漂移；g++ Debian 14.2.0、C++17/O2/Wall/Wextra/Werror/pthread，无 NDEBUG，发布版正常构建。候选可交主会话审核部署，未冒充已发布。

`verification-evidence-manifest.json` 记录 6 张浏览器截图、浏览器结果及旧模型正反证据的 SHA；`handoff-files.json` 列本会话 19 个源码、测试、报告路径及交付哈希，供主会话定向审核暂存。未暂存或提交共享索引。

线上仍为 `2efc59f48e303c5c8bd49334678a40663d901aee58d298d7c2a37f5245336485`，构建前后 PID771782、binary SHA 均相同。格子仍 V0.37；玩家固件由主/玩家会话台账负责，本服务器会话未更改任何设备版本。

服务器会话已完成候选冻结与报告；主会话负责审核后备份、部署、回读和 Git，并协调新局真实放置/拿回棋子的验收。当前房间不能被调试写入来制造通过证据。实体配对及网页真实人工修改的验收尚未完成。

## 主会话部署补记

2026-10-17 21:03 EDT，主会话独立重算 544 项源码与应用 SHA，冻结输入无漂移。仅替换游戏服务，实际运行 PID1877597、SHA256 `81b79bec250b4cf3e52c9d315ea442c5cdabf4d6083005a344eadf9490de17cd`。旧程序与停服时游戏目录保存在 `/var/lib/gridopoly-debug-pairing-deploy/gridopoly-debug-pairing-20261018-010317`。

前后 room993580118、游戏业务差异为空；402 项图片资源及 AP/服务配置哈希均不变。稍后只读回查 16 模块在线/分配、唯一 16 节点闭环 ORDER ready、3 台玩家屏在线、配对 complete/boundMask7；当前为 COM10 一块 V0.38、其余15块 V0.37。网页返回包含两项新调试菜单。读取网页首次忘记解压 gzip，仅本地解码失败，补解压后检查通过；没有因此重新部署或修改对局。

本机证据 `C:/Users/kicof/AppData/Local/GridopolyDebugPairingRuntime/server-deploy-20261018-010317/` 与 `server-post-deploy-live.json`。真实新局放置/拿回流程及用户主动修改资金/归属尚待现场操作，未用自动调试写入替代。
