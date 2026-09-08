# 服务器端开发进度 — 2026-09-07

本轮范围：恢复开发后的 Action 17 服务端回归、现场只读核实及联合验收支撑。
实际采样/构建时间：2026-09-08 01:35–01:45 UTC（美国东部 2026-09-07）。
工作目录：`Z:/Files/我的项目/Gridopoly`。未读取凭据到报告或输出。

## 结论

- 服务端无需重复实现或部署；本轮独立 ARM64 构建产物与生产二进制 SHA-256 完全相同。
- Windows Debug CTest 12/12 PASS；Pi 严格 native 16/16 PASS，三项素材检查 PASS。
- 补充的同一移动重连版本变化及新房间隔离测试另行定向重编、运行 PASS。
- Tag 先稳定、闸门后就绪时，相同 tagRevision 与相同完整集合的下一心跳可以自动确认到达，不需要标签离开重放。
- 本轮未修改权威业务源码、共享协议或核心；未部署、重启服务、重设房间、改派模块、修改线上存档或操作串口。
- 圆屏真实首帧至 LED 的实机链路尚待联合验收；以下 PASS 不代表已烧录或端到端通过。

## 线上只读证据

`/usr/local/bin/gridopoly_server`：475800 bytes，SHA-256：

```text
346dfc92d630f07c434aa2ed56fc5f652ca147f9e5725f02396718374787da36
```

与 2026-08-28 交接候选一致，也与本轮当前源码独立重编结果一致。
四个服务 `gridopoly`、`gridopoly-ap`、`gridopoly-dnsmasq`、`gridopoly-ap-watchdog` 为 active。

只读样本：

- `/health`: ok，roomId=993580098，version=86，peers=1。
- `/api/sync`: phase=2，P1，gate active=true / ready=false / origin=3 / target=7。
- 模块 tile-288485ba9fe8 在线，分配 A2 / mapIndex=3，tagReaderState=stable，tagRevision=1，tagOverflow=false。
- UDP authFailures=0，replayDrops=0，txErrors=32；约数分钟后复查仍为32，不能把累计值解释成当前持续故障。
- 本样本为 Copper Lane 出发、Tideway Drive 目标；不能据此否定用户此前回合的现场报告。

## 本轮代码变更

仅以下测试及观测工具是本轮新增/修改范围；三份 C++ 测试原本已有大量历史未提交变更，提交时应保留并区分历史依赖。

1. `tests/host/authority_persistence_tests.cpp`
   - Action 17 旧/未来版本、非法/非当前玩家、assetIndex 非0xFF、负数/越界目标拒绝，且状态与闸门不变化。
   - 到达已完成后即使请求使用当前版本也不得再次释放闸门。
   - 同一移动中连接状态更新导致 version 变化时保持已释放状态；旧版本仍拒绝，新版本就绪回执不推进版本。
   - 新房间即使复用相同玩家和目标，也不得继承旧 ready。
2. `tests/host/udp_server_integration_tests.cpp`
   - 已认证 P1 session 冒充 P2 的 Action 17 返回 InvalidPlayer，闸门保持关闭。
3. `tests/host/http_asset_integration_tests.cpp`
   - 闸门关闭时已上报 stable Tag revision19，释放后重复完全相同 revision19/集合可自动到达。
   - 之后重复同一心跳不二次推进状态。
4. `Server/RaspberryPi/tools/sniff-udp-frames.py`
   - 增加 `--actions`，解析 ActionRequest/ActionResult 的公共字段和请求 sequence，输出 epochMs。
   - 改用 ETH_P_ALL 捕获本机发送方向；IPv4/UDP/端口仍由解析器过滤。原 ETH_P_IP 实测只见入站 Heartbeat，改后见 Heartbeat 与 Ack。
   - 明确该工具不验证 HMAC，观察到请求不等于服务端接受。
5. `Server/RaspberryPi/tools/observe-movement-cue.py`
   - 只读 GET sync/health/assignments，输出带 UTC epochMs 的变化记录；默认500ms间隔，最小250ms。
   - 不发送玩家动作、心跳注册或分配请求；不输出凭据/原始认证数据。

## 本轮验证

### Windows

- CMake Visual Studio 18 2026，x64，Debug。
- 独立目录：`C:/Users/kicof/AppData/Local/Temp/gridopoly-server-host-20260907`。
- `ctest --test-dir <上述目录> -C Debug --output-on-failure`：12/12 PASS，10.02秒。
- MSBuild 有临时目录增量构建提醒 MSB8029；本轮全新目录构建通过。
- Windows CMake 条件不包括四项 Linux 服务端集成/持久化/交易测试；这四项由下面 ARM64 验证覆盖。

### Raspberry Pi ARM64

- 独立源码快照：`/home/kicofy/gridopoly-server-verify-20260907.ELazja`。
- 独立构建：该目录下 `build-native`；独立临时数据：`test-tmp`。
- 工具链：系统 g++，`-std=c++17 -O2 -Wall -Wextra -Werror -pthread`，未定义 NDEBUG，assert生效。
- 运行当前 `Server/RaspberryPi/tools/build-and-test-native.sh`：16项测试及三项素材校验全部 PASS，最终 `GRIDOPOLY_VERIFY_EXIT=0`。
- HTTP/UDP 测试绑定127.0.0.1、请求 port=0 分配临时端口；不占用生产4242/80，无需网络命名空间。
- 初次快照脚本因 CRLF 导致 Bash 选项解析失败；仅对隔离快照脚本归一为LF，工作区脚本未改，然后执行成功。
- 后补生命周期测试仅重编/运行 `gridopoly_authority_persistence_tests`，PASS；未重复无关测试。
- 日志：`native.log`、`focused-authority.log`；初始文件哈希清单：`source-manifest.json`，后续更新清单：`validation-updates.json`。
- 测试覆盖包括原有成功不增version、重复ActionResult缓存回放、阻塞/就绪持久化恢复、RFID门禁、手动ConfirmPosition兜底以及本轮新增断言。

### 观测工具

- Request/Result字段以构造的12字节payload核对，截断payload不解析：PASS。
- 本机对线上GET观察3秒，6样本/0错误；Pi同样6样本/0错误。
- Pi ap0 只读抓包3秒，见 Discover=4、Heartbeat=1、Ack=1；期间无玩家动作，因此不宣称已捕获Action17实机回执。

## 玩家屏协议约定（已直接同步玩家屏会话）

ActionRequest：action=17，playerId=认证seat，assetIndex=0xFF，argument=目标tile，expectedStateVersion=当前精确且非零版本。

ActionResult为12字节：byte0=1；byte1=ErrorCode（0为成功）；byte2=认证seat；u32LE@4=当前stateVersion；u32LE@8=请求frame.sequence。响应header acknowledgement同请求sequence。

- 成功不推进stateVersion，独立pending应由匹配ActionResult结束。
- 同请求重试业务payload和内层sequence保持不变，外层UDP packetSequence必须递增。
- 旧版本拒绝后先同步；若仍同一待确认移动且已真实呈现首帧，以新request sequence和精确新版本重发，不修改旧请求payload。
- 服务端仅缓存最近一个动作结果；手动到达或更新动作可覆盖缓存。旧cue请求需要随room/target/phase变化清理，不能占住普通动作pending。

## 联合验收准备

已向主会话与两端同步接口和回归结论。线上房间动作/改派/烧录窗口由主会话协调。

在Pi的独立快照目录，可分别启动：

```bash
sudo python3 Server/RaspberryPi/tools/sniff-udp-frames.py --interface ap0 --actions --duration 60
python3 Server/RaspberryPi/tools/observe-movement-cue.py --duration 60
```

关联依据：圆屏串口首帧与发送记录 → 抓包Action17请求sequence → result=0且version不变 → GET gate变化 → 格子真实heartbeat/cue串口记录及LED观察。

轮询的多个GET不具备原子性，工具记录healthVersion及采样跨度；500ms采样不能证明屏幕实际首帧，也可能错过Tag已在位时快速放行后自动到达的短暂ready。因此必须结合圆屏与格子串口/实物证据，不能用轮询缺少ready样本判定功能失败。

当前服务器端独立工作已完成，联合实机验证待主会话窗口。若出现服务端失败再定向修复；目前没有部署需求。

## Git交付

已按用户要求仅执行只读status/diff；未git add/commit、切换分支或改索引。主会话统一串行审查、暂存与提交。
建议本轮提交说明：`test(server): verify movement cue lifecycle and unchanged tag arrival`，观测工具可独立提交 `tools(server): correlate movement cue requests and authority state`。
候选二进制、快照、日志和本地凭据均不应提交。

## 后续交叉复核：丢包后较新心跳越过旧序号

玩家端请求只读接口复核时发现恢复缺口：若首个Action17请求从未到达服务端，后续较新内层序号的Heartbeat先被接受，旧cue序号既不是新序号、也不在最近ActionResult缓存中，重试只能得到Resync。保持原pending无限重传无法自行恢复。

本轮追加 `tests/host/udp_server_integration_tests.cpp` 57行，明确区分两种场景：

1. **首个请求丢失**：生成N但不发送，发送较新Heartbeat H并确认Ack H。两次用原N和原业务字段、新UDP外层序号重试；每次收到FlagResync StateSnapshot，ack=H、version/phase/target不变，且无ActionResult、gate保持关闭。随后新逻辑请求使用新内层序号，成功放行且不推进version。
2. **请求已接受、结果未被客户端保留**：cue已被接受后，再发较新Heartbeat H2；重试原cue序号仍返回原ActionResult，匹配原请求ack、result=0，gate保持ready且version不变。这证明较新Heartbeat不会自行清掉动作缓存。

重要证据边界：现wire没有缓存命中字段或Resync原因。单个FlagResync或ack越过旧序号不能严格证明缓存不存在；普通完整重同步也可产生这些字段。恢复应限于已呈现且未ack的幂等Action17，结合不可变重试探测及后续重同步进行受限逻辑请求续期，不改服务端重放规则、不改普通动作行为，也不重播骰子或首帧。

追加验证：Pi同一隔离目录定向重编UDP集成测试，`focused-udp-recovery-final.log` 为PASS/exit0。为验证新增异步收包fixture的稳定性，再重复该二进制5次，5/5 PASS。没有重跑无关全套或部署服务器。

上述fixture与恢复判据已直接同步玩家屏与主会话。玩家端新增本地RetryRequested、不可变探测及新逻辑请求实现由玩家屏会话负责；其最终交叉复核和实机结果另记。

追加待提交范围仅此UDP测试及本报告；此前Git交付为5c844b1（历史服务端基线）、71b4dfc（首轮新增回归）、1b68afc（观测工具与报告）。本会话仍不操作索引或提交。

## 2026-09-08 补齐 Tag 联合观测记录

此前21:58的真实UID事件缺少同步HTTP采集，且服务器短期历史随后过期，不能追补自动到达证据。本项补齐下一次采集工具，不表示项目整体修复完成。

变更：

- `Server/RaspberryPi/tools/observe-movement-cue.py` 现在每轮顺序只读GET `/api/sync`、`/health`、`/api/tile-debug/assignments`、`/api/tile-tags`。
- schema=2；保留sync room/version/tagBindingRevision/gate（包括origin/target）、health room/version、assignment room/serverRevision、每模块tagRevision/state/overflow以及assignment。
- `tagSnapshot`完整保留`/api/tile-tags`响应：所有UID、currentlySeen、全部模块sightings、历史lastSeenMs、绑定UID和player、roomId、global tagRevision、bindingRevision、updatedAtMs。未根据一次采样合并或猜测跨端位置。
- `sampleStartedEpochMs`和`epochMs`分别为采集主机开始/结束UTC Unix整数毫秒；`requests[path]`分别记录startedEpochMs、receivedEpochMs、durationMs、ok/error。`sampleSpanMs`用主机monotonic测量。
- `snapshotAtomic=false`明确四个请求不是原子快照。不同room/version保留各自原值。服务器Tag的lastSeenMs/updatedAtMs根据当前`TileDebugAssignments`默认system_clock实现标为server UTC毫秒；设备uptime必须另列，不混用。
- 现在保留每个样本，含未变化样本和部分失败；某个请求失败时该来源为null并记录错误，其他成功来源仍保留。成功`tags=[]`与无法取得tags明确区分。
- `--output`独占创建新的JSONL文件，每条记录立即flush，同时保留stdout。已有文件拒绝覆盖。`--duration`、`--interval`、`--timeout`控制采集范围，默认60秒/500ms/每请求2秒超时，间隔至少250ms；每轮顺序执行，绝不并发堆积请求。一个进行中的采样可能使运行时间超出duration，最多受四次GET超时边界限制。
- 与格子会话的`observe_tile_serial.py`对齐epochMs为主机接收UTC毫秒。优先同一采集主机；跨主机比较必须另有时钟偏差证据。离线串口日志没有原始时间时不能用处理时刻伪造。

新增本地验证脚本：`Server/RaspberryPi/tools/test-observe-movement-cue.py`。

执行 `python Server/RaspberryPi/tools/test-observe-movement-cue.py -v`：7/7 PASS，4.302秒。包含：完整多UID与sightings/revision、每请求时间、跨GET房间变化、空集合与HTTP503区分、坏JSON/结构、缺revision、模拟超时、本地真实连接拒绝、CLI重复未变化样本落盘及拒绝覆盖。

短时线上只读采集证据：`C:/Users/kicof/AppData/Local/Temp/gridopoly-observe-tags-20260908-002614.jsonl`，2完整样本、0请求错误、加1条结束记录。room=993580098、version=183、global tagRevision=49、bindingRevision=2、tags为空。本样本只证明采集连接/解析/保存可用，不证明真实UID已上报或自动到达。

下一次主会话安排联合窗口时，在同一Windows采集主机显式启动，例如：

```powershell
python Server/RaspberryPi/tools/observe-movement-cue.py --base-url http://10.0.0.124 --duration 60 --interval 0.5 --output "$env:TEMP\gridopoly-joint-tags-unique.jsonl"
```

输出文件名每次必须唯一。与格子串口JSONL、圆屏真实首帧/Action17发送/ack记录共同保留，再按host epochMs与room/version/request/UID关联。GET仍不能证明短暂ready已出现，也不能证明LCD真实呈现或LED肉眼效果。

剩余联合验收阻塞：玩家屏完整构建/SelfTest/性能与候选版本验收由玩家屏会话推进；主会话需安排串口独占、同步采集和用户放置/移动绑定棋子的窗口；必须实际获得首帧→Action17→gate→完整Tag心跳/自动到达及LED的关联证据。历史UID窗口没有记录的部分仍不可补证。服务器未改服务、房间、Tag绑定或线上动作，未后台持续采集。

本项Git提交范围：`observe-movement-cue.py`、新增`test-observe-movement-cue.py`及本报告。采集日志为临时本地证据，不纳入Git；主会话统一提交。

## 2026-09-08 正常候选联合窗口：AP 只读诊断

主会话已授权玩家屏正常 Action17 候选烧录及三端有界采集。服务器端 HTTP 500ms/600秒与 Pi ap0 被动 UDP 600秒采集正在执行；本节先记录玩家屏出现 wifi_recover/status6/ip0.0.0.0 时的只读 AP 诊断，采集结束统计另补。

实际服务名称为 gridopoly、gridopoly-ap、gridopoly-dnsmasq、gridopoly-ap-watchdog，四项均 active；系统自带 hostapd.service masked/inactive 不是项目 AP 停机。ap0 为 gridopoly、channel3/2422MHz/20MHz。

目标 MAC dc:b4:d9:02:d1:dc（玩家设备02d9b4dc）日志：

| 2026-09-08 EDT | 服务器记录 |
| --- | --- |
| 00:52:26 | 原有 watchdog 因邻居失败驱逐，grace20s |
| 00:52:29 | 关联、WPA四次握手、DHCP完整交换并ACK 10.42.0.37 |
| 00:55:23 | watchdog 因rx-stalled驱逐，grace45s |
| 00:55:25 | 再次关联、WPA四次握手及DHCP ACK 10.42.0.37 |
| 00:57:56 | watchdog 因rx-stalled驱逐，grace45s |
| 00:57:57 | 再次关联、WPA四次握手及DHCP ACK 10.42.0.37 |
| 00:58:40 | station dump authorized/authenticated/associated=yes，connected42s、txfailed0 |

原始 journal 为 +08:00，表内转为 EDT（减12小时）；Pi 与 Windows 采集时钟另有 Pi 超前245–466ms的单次往返界限，不能视为绝对同钟。

线上 watchdog 与仓库脚本仅注释不同，实际环境配置为失败邻居20秒、累计rx_bytes无增长45秒、轮询2秒。rx-stalled 分支不检查客户端WiFi.status或游戏版本，只能说明驱逐前至少45秒未观察到该站接收字节增长。DHCP ACK由AP发出也不能证明ESP已处理GOT_IP事件。结合玩家屏串口卡在recover打印，已向屏幕端建议在WiFi.mode(WIFI_OFF)、mode(WIFI_STA)、WiFi.begin返回点增加边界日志，先区分驱动调用阻塞与事件/状态更新失败。烧录时间及设备uptime仍需屏幕端关联，不能把每次DHCP成功擅自归给某固件版本。

原始证据：C:/Users/kicof/AppData/Local/Temp/gridopoly-joint-20260908-0053-ap-diagnostic.log；线上脚本只读副本：同目录 gridopoly-joint-20260908-0053-live-watchdog.sh。没有输出密码/PSK，没有重启服务、改配置或手动驱逐设备。结果已直接同步主会话与玩家屏会话。

玩家屏随后提供烧录文件边界：正常候选写入EDT00:54:30–00:54:51，故00:55:25的AP关联/WPA/DHCP成功属于候选；旧生产恢复写入00:57:07–00:57:33，故00:57:57属于旧生产。候选串口采集00:55:44才启动，五条日志为缓冲批量读取，不能用接收时间间距推算设备执行时间。玩家屏已确认恢复后ready/UDP10.42.0.37/原room993580098，COM7已释放；当前设备旧生产不含Action17。本轮新候选功能未验收，玩家屏继续修复恢复路径和增加事件/设备millis诊断。

联合采集现已按时结束，两个服务器采集进程均exit0，未自动续开：

- HTTP：`C:/Users/kicof/AppData/Local/Temp/gridopoly-joint-20260908-0053-http.jsonl`。开始epoch1788843261420（EDT00:54:21.420），结束1788843861416（01:04:21.416）；1186样本、4744次GET、requestErrors=0、interrupted=false。顺序四请求的样本跨度中位94ms、最大1172ms，500ms为目标周期，非严格硬实时。
- HTTP全窗口room993580098，version186/187/188/189/190，phase5 AwaitDebt；gate.active/ready始终false，Tag集合始终空。P1绑定UID8EFA24DF、position6；T-WEST/mapIndex6的tile-288485ba9fe8在线，assignmentRevision10、tagRevision1/globalTagRevision53/bindingRevision2。udp.authFailures/replayDrops=0，txErrors32为未变化历史计数。
- UDP：`C:/Users/kicof/AppData/Local/Temp/gridopoly-joint-20260908-0053-udp.log`，Pi ap0被动600秒完成。detailFrames=0、actionFrames=0；Discover597、PairRequest2、PairAccept2、Heartbeat268、StateSnapshot10、GameEvent28、AuthoritySnapshot10、RosterSnapshot10、Ack268、PlayerCardEvent18、0x28计10、0x2a计12。能够观察双向配对/心跳，不存在“完全没收到包”歧义；本窗口没有观察到ActionRequest/ActionResult，不能证明Action17链路。抓包工具本身不验证HMAC。
- 格子端确认COM6同机有界串口窗口epoch1788843195698–1788843795850（00:53:15.698–01:03:15.850）、70行，cue revision186–190均none，无tag_inventory/reset/fault，COM6已释放。其离线交集核验1055个完整HTTP样本，零错误、phase5、gateReady0、Tag集合空。

本次结论是失败窗口与AP定位证据完整落盘，并非自动到达功能验收通过。现有正常候选遇到WiFi恢复阻断，玩家屏已退回不含Action17的旧生产并继续修改恢复路径；格子设备维持V0.28，服务器二进制维持先前已核实346dfc92...，没有重新部署。主会话下一步协调可启动的正常候选及真实移动窗口；玩家屏负责WiFi恢复/实际首帧/Action17与完整测试，格子端负责有Tag输入与目标LED观测，服务器端在新窗口关联Action17接受、gate、UID和权威到达。LED空线圈测试与预放Tag立即到达测试分开；当前房间停留债务阶段，不擅自注入roll/confirm或改变房间/绑定制造通过。

本节仅修改本服务器进度报告，交主会话串行审核提交；原始临时证据不进Git。

## 2026-09-08 第二轮正常诊断候选：联网成功，移动验收仍待执行

本节更新上一窗口的设备状态：玩家屏已上传并保持运行第二轮正常Wi-Fi候选，SHA256 `ded1375634d29cc454ce401dd7e1da56373e91055d2bd464a865bee3d3c31a8c`（玩家屏报告hash verified、compiled01:09:44）。实际上传UTC05:15:01.593–05:15:24.630，串口opened05:15:24.897；当前不再是上一节的旧生产固件。服务器版本不变、未重启服务或改AP。

玩家屏设备时间证据：最初ms1918/2929/3952/4961/5974出现reason2断开事件，随后ms13097 CONNECTED/channel3、14117 GOT_IP，UDP ready10.42.0.37、paired seat1/room993580098/session4088196611，并收到v192 phase5 position6及完整AUTH/ROSTER。status3持续，无panic，未触发30秒恢复。开头reason2不能标成整窗口连接失败，WiFi.reconnect及channel0调整也不能单凭本轮成功认定其中一个就是根因。

服务器按玩家屏通知立即启动120秒有界只读采集（文件前缀0114是计划窗口，实际开始时间以下面记录为准），全部进程exit0、未自动续开：

| 证据 | 实际结果 |
| --- | --- |
| HTTP | epoch1788844524349–1788844644352，即EDT01:15:24.349–01:17:24.352；239样本/956GET/零请求错误、interrupted=false |
| HTTP状态 | room993580098，v191→192，phase5始终不变；gate.active/ready=false，tags为空；authFailures/replayDrops0、txErrors32未增长 |
| UDP | Pi ap0被动120秒，结束epoch1788844655714（01:17:35.714）；Discover119、PairRequest1、PairAccept1、Heartbeat59、Ack59、StateSnapshot4、GameEvent10、AuthoritySnapshot4、RosterSnapshot4、PlayerCardEvent8、0x28计4、0x2a计5；detailFrames/actionFrames均0 |
| AP | 四服务active、channel3/2422MHz/20MHz；01:15:34现有watchdog因FAILED邻居20s驱逐旧station，01:15:36目标MAC重关联、WPA/EAPOL四次握手完成、DHCP ACK10.42.0.37 |
| AP后续 | 01:15:50–01:17:20连续station样本connected14→104秒，rxbytes6708→17152、末次170包，authorized/authenticated/associated均yes、txfailed0；末次journal无后续断开/驱逐 |

Pi时钟相对采集主机超前288–506ms为该次218ms SSH往返测量界限，不能作为绝对同步时钟。串口设备ms单独保留，不从host读取间距推设备uptime。UDP工具本身不验HMAC，配对/心跳数量需与客户端配对成功及服务器鉴权错误计数共同理解。

原始文件均在 `C:/Users/kicof/AppData/Local/Temp/`：`gridopoly-joint-20260908-0114-http.jsonl`、`gridopoly-joint-20260908-0114-udp.log`、`gridopoly-joint-20260908-0114-ap.log`、`gridopoly-joint-20260908-0114-ap-interim.log`。玩家屏对应本机目录为 `GridopolyPlayerTools-3311/functional-window-20260908-0114`。已直接同步主会话及屏幕端；本节只改服务器报告，原始临时日志不进Git，由主会话统一提交。

本轮证明新候选启动后可完成联网/配对/状态同步，仍未证明真实首帧→Action17→目标cue→绑定UID到达。线上仍停在债务phase5，未注入游戏动作。下一步主会话协调用户正常推进至移动并安排目标空线圈/预放Tag分别测试；玩家屏继续完整SelfTest与性能门槛，格子端负责物理UID/LED观察，服务器端在实际移动窗口补齐权威报文与状态证据。当前正常候选可以通信不等于整个功能正式发布。

## 2026-09-08 22:09 UTC 五分钟现场移动观测

主会话发现room993580098已从债务转为phase1，授权服务器端300秒只读HTTP/UDP Action观测。本端没有重启服务、注入游戏动作、修改assignment/绑定或打开COM6/COM7。按交接版本：COM7为正常Wi-Fi候选ded1375634d29cc454ce401dd7e1da56373e91055d2bd464a865bee3d3c31a8c，COM6为V0.28，服务器维持原版本；本轮未重新读取设备固件hash。

两项采集均按原截止结束并exit0，已释放，不续开：HTTP UTC22:09:48.567–22:14:48.577（epoch1788905388567–1788905688577），591样本/2364GET、requestErrors0/interruptedfalse；UDP约22:10:01.191开始，22:15:01.533结束。UDP双向Heartbeat150/Ack150、Discover298、StateSnapshot2、GameEvent4、AuthoritySnapshot2、RosterSnapshot2、PlayerCardEvent1、0x28计2、0x2a计2；detailFrames/actionFrames均0。

以下为主机UTC首次观测时间，不冒充服务端事件精确发生时间；每个样本保留四次GET时间及非原子标记：

| 首次观测UTC | HTTP证据 |
| --- | --- |
| 22:09:48.661 | v200/phase1/P1位置6，gate inactive；T-WEST/map6/assignmentRevision10；tags空 |
| 22:09:52.679 | 现场assignment变为C1/map13/revision11，本端未作分配写入 |
| 22:09:57.183 | v201/phase2，gate active、readyfalse，P1 origin6/target13；tags空 |
| 22:10:00.166 | 同v201、同目标，gate.ready=true；tags仍空 |
| 22:10:07.694 | v202/phase6 TurnEnd，P1位置13、gate inactive；绑定UID8EFA24DF currentlySeen=true，sighting为同一模块C1/map13；模块tagRevision2/globalTagRevision54，bindingRevision2 |
| 22:10:14.693 | UID currentlySeen=false，模块tagRevision3/globalTagRevision55；P1位置13/v202不变 |
| 截止 | Tag短期历史已过期为空；仍v202/phase6/P1位置13/C1 map13 revision11，无重复推进 |

全窗口15个样本gate.ready=true、14个样本Tag currentlySeen=true；authFailures/replayDrops一直0，历史txErrors32未增长。HTTP给出了匹配目标、闸门就绪、绑定UID上报和权威位置推进的关联证据。

限制必须保留：UDP因顺序启动和工具返回耗时，比HTTP晚约12.6秒启动，且晚于ready首次观测约1秒，漏过本次Action17之前的报文。actionFrames0不表示玩家没有发送Action17，也不能把gate.ready反推成已经抓到Action17原包或成功ActionResult。本轮未占串口，缺少真实LCD呈现/发送回执日志及LED目视验收；只凭HTTP关联无法唯一排除另一路手动Confirm请求。完整自动到达因果证明仍需主会话结合现场输入记录；持续预放Tag与手动入口亦不能冒称本轮通过。

Pi相对Windows时钟超前2333–2522ms，是本轮189ms SSH往返取得的界限，不能沿用早间约300ms的偏差。Tag lastSeenMs为Pi时间，与HTTP主机接收时间直接相减会误判顺序。

原始证据均在C:/Users/kicof/AppData/Local/Temp：gridopoly-joint-20260908-2208-http.jsonl、gridopoly-joint-20260908-2208-udp.log；派生完整状态转折文件gridopoly-joint-20260908-2208-observed-transitions.json保留前一个样本时间及首次变化完整样本。文件前缀2208代表主任务通知时段，准确开始/结束以上述epoch为准。

主会话已要求删除gridopoly定时任务，AGENTS/协调文档已更新，本端不重建或替代。当前窗口结束即交付主会话审核提交本报告，临时日志不进Git，不自行续开采集或唤醒其他端空转。剩余现场协调及缺失验收由主会话负责；本次证据增加不等于全功能或性能验收完成。
