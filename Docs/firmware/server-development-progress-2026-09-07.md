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
