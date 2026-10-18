# 格子 V0.38：目的地呼吸灯与配对扫描边界

日期：2026-10-17，America/New_York。格子任务负责TileModule、host回归和本报告；服务器任务负责配对模型/WebUI，主会话负责设备、串口和Git。起始实机基线为16块普通V0.37，图片缓存、180度方向和低亮均已获用户确认。本任务未打开串口、烧录、修改线上房间/Tag/资产/分配或操作共享索引。

本轮格子源码完成、7套65项主机回归通过、普通long/corner均构建成功并冻结。主会话已完成COM10角落首板V0.38写验和30秒本地目的地预览，用户确认呼吸柔和、亮度及屏幕正常；其余15块仍为V0.37。真实骰子触发目的地到达的完整流程不由本地预览替代，实机证据见文末。

## 原因与修改

旧目的地每900ms在两个区间硬切换亮灭，最终灯珠RGB在`(5,15,12)`与`(0,0,0)`间跳变。只是降低强度不能改变这种双闪节奏。旧LED刷新也在主循环内，名义50ms，但同步RF扫描、LCD提交可能占用数百ms；HTTP在另一个任务，主循环主要还受短互斥等待、状态/串口处理影响。因此本轮同时调整波形和刷新所有权。

新目的地使用2400ms对称smoothstep呼吸，1200ms渐亮、1200ms渐暗，端点斜率为零。直接在最终灯珠RGB的`(1,3,2)`与`(5,15,12)`之间以Q10插值再取整，避免先将亮度量化为8位、随后缩色造成额外台阶或低亮颜色偏移。低谷不全灭；20ms采样下每个通道相邻帧至多变化1，绿色始终高于红蓝，完整经历green=3～15全部级别。不采用时间抖动或额外开关闪烁。

出发格仍为原1600ms橙色呼吸；配对、连接、普通/已占据格子的颜色与周期保持。所有场景和本地维护指令都通过同一个最终发送边界，逐通道硬限16；维护纯红/绿/蓝仍为8/255。LCD8MHz、rotation2、背光150、房屋图片RAM缓存及既有RFID/ORDER生产逻辑保持。

`led_scene.h`提供纯值场景和Animator；`led_runtime.cpp`拥有唯一RMT发送器、编码缓冲、维护期限和动画状态。LED任务在core0、priority2、4096字节栈，以20ms节拍运行。主循环只向长度1的场景队列非阻塞覆盖最新值；维护命令队列最多4项，每帧最多取4项，队列满会明确报告失败。状态仅在短临界区复制，不持锁发送RMT、等待网络或绘图。实际错过周期后跳过旧帧，不连续补发积压帧；RMT失败后停止且不自动重试未完成帧。

`LED STATUS`增加`period_ms=20`、`max_gap_ms`和`late_frames`（相邻采样间隔超过40ms计一次），供主会话核对真实调度。主机生产者阻塞测试仅证明Animator不依赖主循环继续调用，不能代替ESP调度或灯珠视觉实测。

服务端的`movementCue.revision`目前是全局`stateVersion`，不是稳定的移动ID。原客户端因此会在无关状态变更时重置起点。新逻辑仅在模式、玩家、分配可用性或格子身份改变时重置，相同cue重发及单独server/assignment/movement revision变化保留相位；起点使用消费时的新鲜millis，避免沿用一次RF扫描之前的loop时间。本轮按主会话与服务器确认不扩展协议：如果同一玩家连续两次同目标、中间`none`完全未被客户端看到，现有协议无法区分这两个相同上下文，此边界保留。

## 配对问题的本端证据与归属

本端没有将不完整RF观测伪装成fresh。`scanHitagTag()`在扫描开始锁存当时可见配对的room/session；后续缓存和心跳沿用该次scan上下文，不用新的HTTP配对会话追认旧UID。完整相同Tag扫描仍推进scan sequence和completed_ms，保持库存revision，下一次心跳据此计算age；集合未变化时通常按2秒周期发送，集合改变会提早触发。

`publishTagInventory()`每轮先记录complete=false。不完整碰撞树可以保留原Present/NoTag画面，但会向网络缓存发布stable＋fresh=false；单次漏读在移除去抖期间也会保留UID而不宣称新鲜。真实`publishTagStatus()`空集合包装下，两次漏读后UI移除UID，但该次过滤后的空集仍非fresh，下一次完整空扫描才可作为新鲜空读证据。连续完整同UID扫描能恢复fresh，并不会因为库存revision没变就永久不更新age。

这与服务端原缺口直接吻合：任一暂态不稳定/不新鲜/overflow会把最后有效扫描的freshUntil立即置零，活动reader随即ReaderOffline；若另一个reader新鲜，便立即换格和新session。该路径可以解释用户描述的识别后又要求换格，但尚无原始现场轨迹证明那一次必然由此触发。客户端保留严格RF证据；服务器任务负责保留已有5秒有效租约，暂态不能绑定、清围栏、续期或使非活动reader被重新选中，真正超时/设备更换仍失效。服务端实现及验证以该会话报告为准。

本轮普通V0.38的scanHitagTag、publishTagInventory、publishTagStatus、recordTagScan及重试发布函数已逐函数核对，和已部署普通V0.37完全一致。共享树历史stage2诊断没有混入候选。

## 回归结果与失败记录

证据根目录：`C:/Users/kicof/AppData/Local/GridopolyTileTools/cue-pairing-v038-20261017/`。

`host-tests-final.log`为7套65项、0失败/0忽略：

| 套件 | 项数 | 本轮关注 |
| --- | --- | --- |
| firmware_boundaries | 21 | 实际场景投影/最终限幅、低亮波形、重复心跳上下文；真实Tag发布与缓存/去抖；既有方向/刷新保护 |
| led_scene | 7 | 对称曲线/端点、时钟回绕、延迟采样、生产者阻塞、值拷贝、配对lease、本地预览到期/取消 |
| ws2812_led | 11 | 编码/GRB/完整发送边界、限时命令、RMT故障停止 |
| display_diff | 8 | 现有差异提交与完整帧恢复 |
| development_artwork | 11 | 生产下载/CRC/容量/缓存/取消及图片预览 |
| pairing_json | 2 | 严格配对字段、lease及旧服务兼容 |
| network_contract | 5 | 实际服务端JSON与格子解析、建筑等级及配对优先级 |

首次7套运行中firmware_boundaries有2项断言失败、led_scene有1项失败，原日志`host-tests-first.log`保留。两处波形断言揭示低亮二次量化的绿色/蓝色同值及端点额外台阶，已改为最终RGB插值；另一处测试最初直接传nullptr，未经过真实publishTagStatus的空集包装，改为调用提取的生产包装函数后正确覆盖移除去抖。不是修改客户端RF门槛去满足测试。最终全部通过。

host首轮及最终构建日志分别为`host-build-first.log`、`host-build-final.log`；既有ArduinoJson C4180警告保留。`firmware-build-first.log`记录两种普通固件首次构建均SUCCESS，实际工具按INI顺序先long后corner；交付后附带构建脚本使用两次单环境调用，确保需要时corner先出。没有以主机测试代替物理波形、RF稳定性或整盘视觉确认。

## 冻结普通候选

两应用均2726288字节，共同sourceDigest为`db55626d006b8a36b4339e1c40b908108d96ac1beeed36ea4eefd4fc2965ad1a`，85项实际源输入。下面路径均相对于证据根目录：

| 板型 | candidate.json | 应用SHA256 | candidate文件SHA256 |
| --- | --- | --- | --- |
| corner | `build/tile_esp32s3_corner/candidate.json` | `c94c31c5d7d12e3eef5a8adc221b7d7baaa898e9d1146741444bb26d82675cfd` | `61f5fe0888c547cd994c30760327229adfd58da55e3605e5532c34abf6d85893` |
| long | `build/tile_esp32s3_long/candidate.json` | `81aa1b3595f20d87bdb403ff54c70928f03be158de5e54aec05ef069848a2136` | `dfc9437474608312538535d9de879c3ec703ade7b7ef969d0ce9ed1b9fe581b2` |

`candidate-validation.json`记录对85输入、四段SHA/长度、真实分区表及数据保护范围的独立核验；normalFirmware=true、rfidDiagnostic=none、普通应用无RFID-DIAG。0/0x8000/0xe000/0x10000四段完整。旧V0.37两个candidate及其四段SHA全部保持原值。

候选来自已验证普通V0.37 sourceDigest `75a822d2c7fd9452e24f3500a5a9d657238a44d14581f7935b2058c431915218`的82输入，经8个变更/新增文件覆盖成为85输入，不直接构建共享树历史诊断main。`gitRevision/sourceBaseRevision`保留原正常基线`59efe7a4…`，同时标记uncommittedOverlay及normalV037SourceDigest；真正内容以完整输入清单和本次sourceDigest为准，不能将旧base字段误解为V0.38已提交的源码版本。

`build-input-overlay.zip` SHA256为`bf2b485820b16dfe313505d591fee1d59bf3501fd57c49e5c8dee3d366099d59`。可读差异`v037-to-v038-overlay.patch`、源清单、`source-provenance.json`及`REPRODUCE.md`一起交付。通用prepare-normal-source.py已实际执行至reproduction-check，再次验证全部85输入一致。

`v038-reproduction-kit.zip`包含17个条目，SHA256 `cb37053efd9f5ba147ebc9d2e77fcb83d99a5e7d338e3cfd3c777c99523e94b6`。工具/依赖元数据见build-environment.json：平台espressif32 7.0.1、Arduino框架2.0.17、GFX1.12.6、ST7789 1.11.0、BusIO1.17.4、seesaw1.7.9、ArduinoJson7.4.2，库版本与V0.37构建缓存一致。重建源输入一致不保证跨路径二进制相同，新产物仍须重新校验。normal-source及reproduction-check继承私有配置，不得公开；ZIP只包含覆盖源码、哈希、脚本和日志，不含本地凭据。

## 首板交接与安全本地预览

主会话识别COM10为`a4:cb:8f:c1:e1:34`、corner；串口/ROM身份核对、写验和设备观察均由主会话执行。首板更新后可用`LED DESTINATION 10`预览10秒目的地呼吸，`LED DEPARTURE 10`对照原橙色；允许1～30秒，按命令发出时间自动到期，`LED AUTO`提前撤销。命令只进入本地LED队列，不改变movementCue、房间、Tag、格子分配或资产。排队执行可能使紧接着打印的第一次STATUS仍显示前一帧，随后LED STATUS可读取实际状态。

预览走同一个生产Animator和RMT发送边界，到期恢复最新实际场景，即使主循环正在读卡/绘图也不会延长维护窗口。检查V0.38/8MHz/rotation2/HTTP/原分配，以及worker状态、max_gap_ms、late_frames、发送失败计数；真实柔和程度、颜色和与橙色区分仍需用户目视。

首板写验、呼吸目视及本次调度观察已完成，结果见下一节。其余15板的后续升级、服务端配对修复部署与用户重新配对观察仍由主会话接续；服务器会话继续其范围。本任务源码和候选冻结，不自行操作设备或索引。

## COM10首板已部署、限时预览目视通过（20:54～20:56 EDT）

主会话完成ROM完整MAC核对并于20:54:51.534～20:55:15.291写验上述corner候选，esptool5.3.1、四段verified及watchdog-reset；实际app为`c94c31c5…`。本端只读复核`C:/Users/kicof/AppData/Local/GridopolyDebugPairingRuntime/`内`v038-COM10-flash.json`、`v038-COM10-status.jsonl`和`v038-COM10-destination.jsonl`。日志原UTC日期为10月18日，本文换算为用户时区10月17日晚。

状态确认V0.38、8MHz、rotation2、HTTP200、原CORNER-START/map0、assignment revision233。目的地预览窗口20:55:54.167～20:56:33.167，共39秒；20:55:56.163发出`LED DESTINATION 30`，随后状态为destination-preview并有不同低亮RGB。20:56:28.333已记录`mode=scene remaining_ms=0 requested_rgb=4,4,4`，早于20:56:29.204的LED AUTO，证明本次自动退出已发生；结束时再次撤销并closed/released。末尾正常页面、HTTP200及原分配保留。

第一份实机状态在attempted/completed713时即已有`max_gap_ms=383 late_frames=2`；预览及末尾至attempted/completed3739仍为同样统计，failed始终0。预览多组约5秒间隔增加约250帧，未出现新的超过40ms间隔计数。**383ms不是20ms严格上限**；两次较长间隔发生于首个采样之前，现有日志不能定位其具体环节，不能写成所有帧或启动期都满足20ms。该有限观察不替代整盘负载/无线竞争下的长期节拍验证。

主会话明确转述用户反馈：“呼吸柔和，亮度和屏幕正常”。这关闭COM10本轮本地目的地预览的视觉验收，不能由RMT完成计数自行推断，也不等于已完成真实掷骰→目标格→Tag到达全链路。当前仅COM10为V0.38，其他15块仍V0.37；主会话负责后续升级、服务端配对修复部署及真实游戏验证。源码、两候选与复现包继续冻结，本端仅补文档，无新增测试、构建或设备操作。
