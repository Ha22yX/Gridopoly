# 2026-10-15 服务端 16 格闭环开发进度

范围：用户要求推进 4 角落 + 12 长边的 16 格可玩包，本端负责 ORDER 拓扑、锚点投影、服务端真实 HTTP/UDP 回归及 Linux 正常候选。主任务负责 Web、发布工具、Git 和部署协调；玩家屏、格子固件由对应会话负责。

## 实现

- `TileOrderTopology.h` 与 `TileOrderProjection.h` 共用明确的 16/32 节点尺寸检查。16 环要求四角位于 0/4/8/12、每段三个长边；32 环仍要求 0/8/16/24、每段七个长边。其它环大小不接受，不宣称 24/40 物理闭环受支持。
- 默认根仍是四个角落中 `moduleId` 最小者，沿实际 ORDER OUT 方向排序；长边 ID 更小、注册顺序相反和节点换 boot 都不会改变此规则。
- 任意模块的有效手动锚点确定整环相位。相位必须保持角落位置；多个锚点必须一致。重新设置原锚点后，全环派生位置随之旋转。关闭的环中新增矛盾锚点返回冲突且不改原映射；开放链中已有的矛盾锚点在闭环时暂停整个环，意图保留供修正。
- 修复回归发现的既有漏检：环内手动锚点与外部手动位置冲突时，原投影跳过手动点，可能漏报占位。现在闭环的手动点也参加冲突判断，整环暂停；开放链的手动优先级保持原语义。
- 错误角色、角落数量/间距、重复 boot、过期或旧序号证据、断开的已知环均不能产生错误的派生映射。已知环断开后不降级为可自行分段派生的开放链。明确手动意图仍可保留，不能把“派生已撤销”误写成任何手动项也都消失。
- 地图格数必须等于物理环节点数。新局清除手动锚点；仍新鲜的物理拓扑独立于房间，可以映射到匹配的新地图，过期证据不能由切换地图恢复。
- 不改 ORDER V1、心跳字段、Tag 配对或共享游戏核心；保留单板、开放链分段锚点及原 32 环行为。

本端修改文件：两个 ORDER 头文件、`tests/host/tile_order_tests.h`、`tests/host/onboarding_network_tests.cpp` 以及本报告。主任务的 Web/gzip 更新已在 `13e4941`，纳入候选快照；不把其它会话文件算作本端实现。

## 验证范围

`gridopoly_tile_debug_assignment_tests` 使用实际生产头文件和分配服务，保留原用例并运行 16/32 参数化回归：所有模块位置与四种整环旋转、一致/矛盾/越界锚点、外部占位、角色和间距错误、重复 boot、退休 boot、重复序号不续物理证据租约、过期/断边/修复、地图不匹配、房间重置以及开放链兼容。显式拒绝 15/17/24/40 节点环。

`gridopoly_http_asset_integration_tests` 完整保留并执行现有 HTTP 资产、身份、Tag、模块分配和 ORDER 边界用例。

`gridopoly_onboarding_network_tests` 使用真实 AuthorityService、HttpServer、UdpPlayerServer，临时数据目录和 loopback 临时端口。保留现有冻结玩家、认证、Tag 入局以及 16/24/32/40 单读卡器场景。扩展实际 HTTP 16/32 新局、逆序心跳注册、完整地图 tile_id/mapIndex、四角角色、错误角色撤销、旋转/重设锚点、矛盾锚点 409、断边/恢复/换 boot、物理环与新局地图不匹配和切回匹配地图。ORDER 操作还核对房间、对局版本及业务状态不变。

初始拓扑回归确实触发外部手动占位漏检，修复后通过。新增测试调试中修正了对既有契约的错误假设：新局保留新鲜物理拓扑、心跳分配字段为 `tile_id`、无上游证据必须用 JSON `null`。没有因此放宽生产解析或改变既有新局语义。失败快照与日志保留在本地临时证据目录。

最终冻结快照的上述三个目标全部 PASS，正常 `gridopoly_server` 构建 exit 0。Pi aarch64 / g++ 14.2.0，沿用项目 C++17、`-O2 -Wall -Wextra -Werror -pthread`，没有 NDEBUG，断言开启。仅上述三个 native 目标属于本轮验证，不将其记作所有 native/资产流水线均已重跑。

## 已归档候选（后续部署见下节）

- 本地目录：`C:/Users/kicof/GridopolyReleases/server-ring16-20261015-042104/`。
- 正常二进制：该目录 `gridopoly_server`，637328 bytes，ELF 64-bit ARM aarch64，SHA256 `ae01f6e5032d66c2e24aa47ba8b4d2e6a344b2c282575e4f0b11c3e1f2723af0`。
- Pi 隔离目录：`/home/kicofy/gridopoly-ring16-20261015.EG6oof`，候选在 `build-pi-native/gridopoly_server`。测试均使用临时目录和 loopback 临时端口，没有向生产端口注入测试报文。
- 证据包含 `source-manifest.json`、`sources.tar.gz`、`run-ring-tests.sh`、`native.log`、`verification.log/json`。128 份输入逐 SHA 核对工作区、源码归档和 Pi 实际构建目录，三处 drift 均为空；下载后二进制 SHA 与 Pi 相同。
- 最终核对时刻 `2026-10-15T04:23:25Z`，生产 `/proc/822/exe` SHA 仍为旧版 `2bc084d7a981ff9ca54c81f85933efed893b9c9151bf4b4e6883570b32adfa18`，service active/running。没有把候选文件哈希当作已部署版本。

## 生产现场只读核对

时间以 Windows UTC 采集时间为准；Pi 的系统日志启动时间与客户端日期不一致，不据其日期推断真实运行时长。

- 04:06:16 UTC：Pi `10.0.0.124` HTTP/SSH 可达，gridopoly active/running PID 822。运行文件 SHA256 为 `2bc084d7a981ff9ca54c81f85933efed893b9c9151bf4b4e6883570b32adfa18`，仍是先前版本。room `993580110` / version `10`，0 注册模块，玩家屏离线。
- 主任务/格子会话随后处理首批四块板。04:19:31 UTC 独立 GET 已见四块 V0.33 在线：`tile-a4cb8fc298e4`（long）→ `tile-a4cb8fc298f8`（long）→ `tile-a4cb8fc298ac`（long）→ `tile-a4cb8fc1e188`（corner）。箭头为 ORDER OUT 方向，三条真实邻接已出现。四者均 unanchored、assigned=false、closedRing=false，无冲突；这证明首批开放链，不能代替完整 16 板闭环验收。
- 该次 room/version 仍为 `993580110`/`10`，PID/服务器文件哈希未变，玩家屏仍离线。本端没有线上 POST、手动锚点或房间修改，没有服务/AP 重启、串口占用或固件刷写。

原始证据位于 `C:/Users/kicof/AppData/Local/Temp/gridopoly-ring16-20261015/` 的 `live-20261015-040616.json`、`live-20261015-041931.json` 及对应 `service-*.log`。

## AP 容量核对

只读实际配置和驱动，未修改网络：

- Raspberry Pi 5 Model B Rev 1.1，ap0 与 wlan0 共用 phy0，SDIO brcmfmac，当前 channel 2。内核日志识别 BCM4345/6、brcmfmac43455-sdio 固件 7.45.265 / FWID `01-b677b91b`。
- 实际 `/run/gridopoly/hostapd.conf` 没有显式 `max_num_sta`；`ap_max_inactivity=10`、`disassoc_low_ack=0`。`iw phy` 未提供 AP 最大关联站数量。它显示的接口组合 `total <= 4` 是虚拟接口组合限制，不是四个客户端上限。
- 实际 dnsmasq 使用 `/etc/gridopoly/dnsmasq.conf`，地址池 `10.42.0.20`–`10.42.0.200`，共 181 地址，未设置显式 `dhcp-lease-max`。
- 04:17:57 UTC 实际 AP 关联四站。配置中未发现低于 16 格加屏幕的显式客户端上限，但未获得固件内部关联容量，也没有 16+屏的并发实测；不能据 DHCP 池或四站成功宣称容量验收通过。

证据：同临时目录 `ap-20261015-041540.log`、`ap-detail-20261015-041757.log`。后续由主任务协调逐批入网并检查实际在线数；若出现关联容量瓶颈再确定网络方案，本端未擅自修改 AP。

## 交接边界

本端源码实现、三个目标回归、正常 Linux 构建和候选归档已完成，`git diff --check` 通过。主任务审核并统一提交 `ca4b91e` 后，授权下述一次生产部署。完整 16 板闭环、全部在线、真实锚点旋转和玩家屏实际入局仍未由本阶段证明。剩余硬件与现场验收由主任务继续协调；本端没有操作共享 Git 索引或创建后台轮询/自动化。

## 生产部署已完成：一次重启、存档保留

主任务独立复核候选完整 SHA、128 源一致性及三个目标结果后，明确授权部署 `ae01f6e5…` 和一次服务重启。操作期间没有 POST 新局、锚点、Tag 或资产，也没有修改 AP、服务配置或占用串口。

先保存旧二进制和运行期数据压缩包，再将一次重启拆成 `stop` → 正常 flush 完成后的 `cp -a` 一致快照 → 原子替换程序 → `start`。一致快照与停止后的整个 `/var/lib/gridopoly` 共 250 文件逐 SHA 相等；旧二进制 SHA 也复核为 `2bc084d7…`。备份位于 Pi 的 root 私有目录 `/var/backups/gridopoly/server-ring16-20261015-042540`，包含 `data/`、`previous-gridopoly_server`、前后清单和证据。启动或存档验证失败会保留失败数据并恢复此一致快照；本次全部检查通过，没有执行回滚。

客户端执行窗口为 `2026-10-15T04:28:55.101Z`–`04:29:18.935Z`。停服时刻 `04:28:56.747Z`，新服务 HTTP 就绪 `04:28:58.793Z`，约 2.05 秒。实际生产 PID **10677**，`/proc/10677/exe` SHA256 **`ae01f6e5032d66c2e24aa47ba8b4d2e6a344b2c282575e4f0b11c3e1f2723af0`**，与审核候选完全一致。

部署后 20 秒、10 组真实 GET 观察通过：

- `/health`、`/api/sync`、`/api/player-devices`、`/api/tile-debug/assignments` 正常返回。room `993580110` 保留，版本按既有恢复初始化逻辑从 10 增至 11。
- 最终完整 `/api/sync` 相对紧邻停服前只有三处差异：`version`、`identity.serverEpochMs`、`tagPairing.sessionId`。剔除这些恢复/时钟字段后，业务差异为空；玩家、现金、位置、资产、Tag UID、身份状态、冻结设备、回合及其它字段全同。
- 250 数据文件没有缺失。`state.bin` 因恢复后的版本写回发生变化；其余 249 份数据（包括 `identity.bin`、`authority.meta`、`device-seats.bin`）均逐字节一致。
- 首页 HTTP 200 / gzip，解压后 HTML 为 92714 bytes（`decodedHtmlBytes`），冻结 gzip 体为 25504 bytes（`gzipBytes`），ETag `gridopoly-ui-15e74b00f252`。gzip 大小由归档 `WebUiGzip.h` 数组核对，解压内容与部署时保存的 `page.html` 逐字节相同；原部署记录中的 92714 是解压长度，不是压缩传输长度。解压后 HTML 完整 SHA256 为 `15e74b00f2527805d7a7c26ef424a1bc687a9dcb8f2fbe15455c567ba75aa3f5`。
- 四个服务全部 active/running，gridopoly `NRestarts=0`。AP、DHCP 和 watchdog 的 PID 819/820/821 与部署前完全一致，未重启网络。
- **部署前最新现场已变化为两板**：V0.33 corner `tile-a4cb8fc1e188` 与 long `tile-288485ba9fe8`，均无上游；早先的四板链不是此时快照。新进程初始模块表为空，20 秒内同两板自动重注册，仍未锚定/未分配。玩家屏仍离线；不把此次部署写成 16 板闭环、RFID 或玩家入局通过。用户目前没有 24V，USB 低压启动也不代表 RFID 电气验收。

本地证据目录为候选归档下 `deployment-20261015-042540/`，包含前后 JSON、十组观察、服务状态、部署脚本/日志、页内容及清单。`recovery.tar.gz` 为私有离机恢复包（7388585 bytes，SHA256 `a40b2b1dfc88c5caad89f842a2b093ec9e042f18015efe289abf47f312873762`），下载后再次核对其中 250 文件和旧 binary，drift 为空；`archive-verification.json` 记录核验。该恢复包及原始生产快照含用户存档，不纳入普通组合分发包。

服务端部署阶段完成，后续由主任务协调玩家屏上线、用户实际新局及完整 16 板闭环验收。此次授权不包含替用户创建真实新局。
