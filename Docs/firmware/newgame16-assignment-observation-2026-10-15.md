# 16格重开局分配异常观察（2026-10-15）

本轮结论：用户最初报告同为16格重开局后部分模块显示格子、部分显示“Waiting for tail”；固件实际文案为 `WAITING FOR TILE`。开始采集时已恢复，用户再次选择16格重开局后确认正常，随后又确认异常没有再出现。当前功能恢复正常；首次异常缺少同步证据，**根因未确认，不能标作已修复**。

## 真实观察

- 主会话在用户操作前开始有界只读采集，每约2秒读取 `/api/tile-debug/assignments`、`/api/state`、`/health`。窗口为 2026-10-15T22:29:27.658659+00:00 至 2026-10-15T22:34:38.342319+00:00 UTC，共150组、约310.7秒。未向生产注入心跳、分配、配对或游戏操作。
- 用户操作使房间 `993580115` → `993580116`，地图保持 `grid-city-16-v1`。全部样本均16块在线、唯一16节点闭环 `ready`，完整映射0～15与操作前逐模块一致；四个corner对应0/4/8/12。ORDER epoch保持18，分配revision由34变36。
- 服务器会话独立抓取22:32:49.826～22:33:19.857 UTC的30.031秒真实模块HTTP流量：16模块、230请求和230完整响应，各模块14～15次。全部HTTP200、`assigned=true`、`source=order`、revision36，mapIndex与GET逐块一致；没有缺失/额外模块或TCP重组缺口。使用有界只读AF_PACKET采集，没有安装软件或改变网络。
- 用户对这次重开局的物理画面确认正常。服务器回包正确不能倒推首次故障时屏幕正常，也不能替代完整实体游戏验收。HTTP health的累计readTimeout计数在窗口中有增长，未观察到模块租约过期、映射丢失或请求采集失败；不将此窗口描述为所有网络计数零异常。

## 跨端审查

已部署服务端仍为PID721，SHA256 `ea6e1f517c597cc69f6f0724af0d501206cf3d57e457945c9cc138fc3f2d118d`。当前 `HttpServer.cpp`、`TileDebugAssignments.cpp`、`onboarding_network_tests.cpp` 与其发布清单匹配。已有换局流程保留尚有效的物理ORDER证据并重新投影；新房间清除manual锚点是已有契约，不能将该契约本身称为本次新增缺陷。既有回归覆盖顺序执行的16→32→16，本轮没有重新运行这些测试。

格子会话核对normal V0.34的网络、分配、连接页面、配对头文件与long/corner冻结清单共10/10哈希一致，相关main显示/消费函数也与部署基线一致。每次成功心跳重建分配快照，不会因新局stateVersion降低而拒收；`assigned=false` → `true`会触发重新绘制。配对页面退出也会恢复格子/连接页。本次没有找到能确定导致持续等待页的客户端缺陷。

服务器代码另有待复现的并发风险：`stateCopy()`与`roomId()`分别取锁，迟到上下文可能触发再次清理manual/Tag。**尚未做确定性并发复现；在同尺寸、闭环证据稳定时同一锁内会立即重新投影，不能据此认定它是本次等待页原因。** 将其保留为独立审查项，不以猜测驱动现场部署。

## 实际变更与后续

本轮只更新观察文档，未修改服务器/格子/玩家业务源码，未构建、部署、刷机、重启服务或改热点；所有采集有界结束，无后台轮询或自动化任务。现有固件和正常对局继续使用。

若再次出现，请先保留异常画面、供电及当前房间，记录屏幕上的module ID，再由主会话协调同时保存服务器ORDER/分配快照、真实心跳回包和该异常模块的有界串口状态。这样可区分实际撤回分配、通信故障或显示未刷新；不要先反复重开游戏抹去现场。主会话负责复发时联调，服务端负责上下文一致性独立审查，格子端负责实际异常板的接收/重绘证据。

私有证据保留在本机（包含对局/Tag网络内容，不纳入Git）：

- `C:/Users/kicof/AppData/Local/GridopolyNetworkAudit/newgame16-20261015-222851/`：150组原始观察和 `transition-summary.json`。
- `C:/Users/kicof/AppData/Local/GridopolyNetworkAudit/newgame-server-20261015-223248/`：`heartbeat-summary.json`、原始抓包、`live-binary.json`。
- `C:/Users/kicof/AppData/Local/GridopolyTileTools/room-switch-audit-20261015/`：格子会话独立快照和 `source-checks.json`。

[当前交接](../CURRENT-DEVELOPMENT-HANDOFF.md) · [全在线闭环实现](server-all-online-ring-progress-2026-10-15.md)
