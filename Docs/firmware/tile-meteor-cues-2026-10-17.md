# 格子 V0.39：旋转流星提示灯

日期：2026-10-17，America/New_York。用户要求棋子配对、移动来源、移动目标由整圈呼吸改为旋转流星拖尾，并强调头灯。格子任务仅负责 LED 源码、主机验证、普通候选与本报告；主会话负责设备身份、USB、部署与现场验收。

当前源码完成，7套69项主机回归通过，普通corner与long均完成构建、独立验证及本批部署。COM10角落和COM16/17/18长边均已运行普通V0.39；三块长边的完整十灯流星、亮度和屏幕均获用户确认。COM10的U20/U21在同板回退V0.37后仍不亮，具体单板故障点待测，不能标全灯通过。其余12板仍V0.37。详见文末逐板结果，候选构建与实体验收分别记录。

## 灯效与亮度

两种 PCB 当前均为 10 颗 WS2812。活动提示每 2 秒转一圈，20ms 独立任务刷新；一个头灯加逐级变弱的尾迹，沿灯珠串行编号正向移动。头位于整数灯珠时，主色通道沿后方为 `32 → 18 → 8 → 2 → 0`；尾迹长度约环周 40%。两相邻空间帧插值，使头在相邻灯珠之间交接，同时末尾渐暗，越过最后一灯回到第一灯不会全环闪烁。10 灯时任一相位亮 4～5 灯，头部最亮通道始终高于 16。

| 状态 | 头灯 RGB 上限值 | 表现 |
| --- | --- | --- |
| 配对等待放入棋子 | 9, 14, 32 | 蓝色旋转流星 |
| 配对成功／请取回棋子 | 7, 32, 14 | 绿色旋转流星 |
| 需清空／已绑定／多个棋子等配对提示 | 32, 14, 4 | 橙色旋转流星 |
| 移动来源 Departure | 32, 19, 10 | 橙色旋转流星 |
| 移动目标 Destination | 12, 32, 26 | 绿色旋转流星 |

32/255 是单通道数值上限，不是功率、耗电或感知亮度翻倍的承诺。普通格子、已占据格子、联网等待／故障原有低亮波形和四分之一缩色保持；最终发送仍限制每通道不超过 16。维护纯红／绿／蓝依旧 8。只有当前实际选中的流星帧采用 32 限幅，配对 lease 过期、预览过期或切回普通场景时立即恢复普通限幅。

`led_scene.h` 的 renderer 现在产出逐 LED 帧，而非全环同色。运行时发送该帧，避免通过另一条“预览专用”路径展示效果。`LED STATUS` 的 `requested_rgb` 保持指像素 0；新增 `[WS2812-FRAME]` 输出 `pattern`、当前 `limit`、最亮像素编号 `peak_index`、最大通道 `peak_channel` 和有界的 10 灯 RGB 列表。最亮像素编号不是连续的数学头位置，在相邻像素交接时以实际量化后的亮度选取。

## 相位、优先级与隔离边界

独立 core0/priority2/20ms LED worker、单 RMT 发送者、长度 1 场景队列与长度 4 维护队列保持。延迟帧跳过积压，不补发一串过期帧；发送失败停止，不能自动重试仍在传输的帧。

配对优先于移动提示。有效配对按 room、session、player、state、active 变化重置相位；单独 revision、显示名字或有效 lease 刷新不再将流星拉回原位。旧 lease 确实过期后，哪怕生产循环错过中间的失效时刻，相同身份的新有效 lease 也从新起点开始。移动提示继续使用既有 mode/player/assignment/tile 上下文，不受全局 stateVersion 心跳重置。

已有协议仍未提供稳定的独立移动 ID：同一玩家连续两次同一目标且客户端没有见到中间 none，不能据此区分两次动作。本轮不扩展协议。RFID 阈值、扫描与发布、ORDER、网络协议、显示方向/8MHz、背光 150、房屋图片 RAM 下载缓存均没有改动。

## 安全本地预览

以下命令均接受 1～30 秒，过期后 LED worker 自行恢复最新实际场景，`LED AUTO` 可提前撤销。它们不修改房间、配对 session、Tag、格子分配、移动或资产。

```text
LED PAIRING 16
LED PAIRING-SUCCESS 10
LED PAIRING-CLEAR 10
LED DEPARTURE 16
LED DESTINATION 16
LED AUTO
LED STATUS
```

预览与生产状态共用逐 LED renderer 和最终限幅。维护时限从指令入队时刻开始；队列延迟不能延长预览。过期窗口被永久退出，即使后续毫秒计数回绕也不能重新激活。

## 验证与失败记录

证据根目录：`C:/Users/kicof/AppData/Local/GridopolyTileTools/meteor-v039-20261017/`。

| 主机套件 | 通过项目 | 重点 |
| --- | ---: | --- |
| firmware_boundaries | 22 | 实际 main 场景投影与配对相位消费、最终发送限幅；保留 RFID、显示及 Tag 边界 |
| led_scene | 10 | 逐灯头/尾数值、相邻头插值、环接缝连续、1/2/7/10/16/256 灯泛化、计时回绕、生产者阻塞、lease 与各预览到期 |
| ws2812_led | 11 | GRB 编码、时序、发送完成、错误停止、限时维护指令 |
| display_diff | 8 | 原差异提交与完整帧恢复 |
| development_artwork | 11 | 原下载/CRC/容量/缓存/取消及图片预览 |
| pairing_json | 2 | 严格配对字段、lease 与旧服务兼容 |
| network_contract | 5 | 实际服务器 JSON 与客户端解析、16 格分配和配对优先级 |

`host-tests-first.log`：上述 69 项首次执行全部通过，0 失败/0 忽略。第一轮 host 编译因 MSVC 要求显式捕获模板内部局部 constexpr `tail` 失败，记录保留于 `host-build-first.log`。已改为 `[tail]`，随后 host 构建通过，并将同一修正同步冻结普通源码、重新生成 overlay/hash，再进行 corner 最终构建。不是只修测试或绕过编译失败。

ESP corner 首次构建已成功，但最终候选使用修正后的第二次增量构建；见 `corner-build-first.log` 与 `corner-build-final.log`。现有 ArduinoJson C4180 主机警告保留。本任务主机测试不证明实际 LED 方向、亮度舒适度或 ESP 高负载真实调度上限。

## COM10 尾部两颗 LED 不亮：现场诊断边界

主会话在 COM10 首板升级后收到用户反馈：PCB 标号 U20、U21 不亮。随后的低亮纯红／绿／蓝对照中，用户确认其余八颗换色，U20/U21 始终不亮；软件帧已列出十颗相同的维护色，读取供电约 4.906V，驱动发送失败计数为 0。发现后暂停其余三块升级，保留普通 V0.39 候选不变，先定位问题。

### 数量与串链复核

角落和长边 PCB 均为 **10 颗 WS2812B-B-V6，U12～U21**，与固件 `kWs2812Count=10` 一致，用户也确认实物十颗。依据为 2026-10-15 原生 PCB 源中的元件属性和 PAD_NET，而非仅从旧文档推断：

- `Mechanical/BoardBase/interfaces/pcb-interface-2026-10-15/evidence/corner-current.source.jsonl`
- `Mechanical/BoardBase/interfaces/pcb-interface-2026-10-15/evidence/long-current.source.jsonl`
- `PCB Files/CornerModule/LEDWhite-2026-10-15/archived-led-power-extract.json`

U20 对应零起始像素 8，DIN=`LED_DATA_09`，DOUT=`LED_DATA_10`；U21 对应像素 9，DIN=`LED_DATA_10`，位于串链末尾。它们的 VDD 标注为 `5V_IN`。这些设计文件证明预期串链，不能单独证明实物焊接、灯珠和连线均正常。

### 独立脉冲计数，不靠发送完成标志推断

ESP32-S3 每个 RMT block 为 48 symbols，原驱动保留 4 blocks，共 192 symbols；十颗灯需要 240 symbols。实际 IDF 4.4.7 驱动支持中断回填和 wrap，因此 **192 的缓冲容量本身不是只能驱动八颗的证据**。普通候选从 V0.37 到 V0.39 的 ESP32 发送后端、板型引脚和灯珠数量文件 SHA 相同，GRB 编码、16MHz 时钟、5/15 与 10/10 ticks、350µs reset 也相同。本轮没有调整 IRQ 优先级、内存 block 或时序来猜测修复。

另建单角落诊断目录 `C:/Users/kicof/AppData/Local/GridopolyTileTools/meteor-v039-pcnt-probe-20261017/`，从已核验普通 V0.39 的 85 项输入复制，仅改 `ws2812_esp32.cpp` 与候选导出器两文件。PCNT 在同一 GPIO21 上监听上升沿：配置时两项引脚均为 `-1`，无 ISR、无滤波，随后只开启输入及连接输入矩阵，不调用会关闭／改接输出的 `gpio_set_direction` 或 `pcnt_set_pin`。每帧在原 RMT write 前清零并启动，原 TX wait 后暂停读取。RMT 参数、颜色帧、亮度与游戏逻辑不变，诊断 API 失败也不会改写原输出行为。

主会话的 `v039-COM10-pcnt-observe.jsonl` 记录：从累计 1,482 至 4,182 帧，覆盖普通状态及蓝／橙／绿三类流星预览，始终 `expected=240 observed=240 min=240 max=240 mismatch=0`，`blocks=4 wrap=1`，TX 失败为 0。这排除了该观察窗口内“MCU 每帧只输出 192 个脉冲”的假设。**240 次上升沿不能证明全部数据位的高低时长、内容、后级电平和最后两颗接收结果正确**；PCNT 没有测量 U19 DOUT 或 U20 DIN。

设备原始证据统一位于 `C:/Users/kicof/AppData/Local/GridopolyDebugPairingRuntime/`：`v039-COM10-pcnt-flash.json`、`v039-COM10-pcnt-observe.jsonl`、`v037-COM10-control-flash.json`、`v037-COM10-control-observe.jsonl`、`v039-COM10-restored-flash.json`。本报告已直接复核计数观察日志的首尾记录和 V0.37 的版本／十灯状态记录。

诊断包明确标记 `normalFirmware=false`、`ledDiagnostic=pcnt-gpio21-passive`，只用于单角落定位，不得进入普通批量升级。其版本字符串仍为 V0.39，必须同时核对候选标记、哈希与 `[LED-PULSE]` 日志，不能只看版本号识别。诊断 candidate SHA256 为 `34bf8686661d8392620dbf68dafda30cabeda6e7cacaefa0f0b54a088120a6f7`，sourceDigest 为 `4925472750761a0759cf5a63194a37160e3063e875db09cfac159154da568707`，app SHA256 为 `bc937ccdfe61d58249a7a92985162fc7b9a7a555ed09d32f8d36bfb5de131000`。完整四段和分区校验见该目录 `probe-validation.json`；两份普通 V0.39 候选及其 85 项源码再次校验未变。

### 同一实物回退旧版对照

主会话随后将 **同一 COM10** 回刷冻结正常 V0.37，保持同一块板用于对照。日志显示 V0.37、十颗灯、普通帧 `4,4,4`、驱动失败 0、HTTP 200；用户明确反馈：仍只有另外八颗亮，U20/U21 不亮。因此，此板的两颗灯异常也存在于 V0.37，不能归因于 V0.38 独立 worker 或 V0.39 流星动画。

用户随后补充：其他角落模块没有同样问题，似乎只有 COM10 这一块。此项属于用户观察，其他角落本轮未逐块进行串口／颜色验收；它支持继续针对单板排查，不证明某个焊点或灯珠已确定损坏。

当前尚未证明具体硬件故障点。后续应针对 U19 DOUT → U20 DIN → U21 DIN 的实际串链、局部供电和灯珠状态核查；上游一颗的数据输出异常可能影响其后所有灯。这里保留排查范围，不将供电总线读数或 MCU 端脉冲计数当作整条灯链健康证明。主会话已将 COM10 恢复普通 V0.39，四段写入校验通过；其余板的独立验收继续由主会话记录，本章节不宣称四板最终部署或全灯验收已完成。

## 冻结普通构建

本轮从 V0.38 `normal-source` 派生，先逐项核对其 85 个输入，基础 sourceDigest 为 `db55626d006b8a36b4339e1c40b908108d96ac1beeed36ea4eefd4fc2965ad1a`。覆盖仅 7 个生产输入：`led_scene.h`、`led_runtime.h`、`ws2812_led.h`、`module_profile.h`、`led_runtime.cpp`、`main.cpp` 的 LED 限定片段及 `export_candidate.py`。共享树的历史 RFID stage2 诊断没有进入普通候选。

正常 main 保留 V0.38 主体，只替换 LED STATUS、LED 帮助文本及配对动画起点判定；五个 RFID 扫描/发布相关函数与基础源码逐函数相同。LCD、ORDER、网络及图片缓存相关输入保持原 SHA。`gitRevision/sourceBaseRevision` 固定为已提交 V0.38 的 `c3a2b118` 对应完整 revision，`uncommittedOverlay=true` 如实标记冻结时本轮尚未提交，不回填将来的 HEAD；真正内容由完整输入清单和 sourceDigest 锁定。

corner 候选：`build/tile_esp32s3_corner/candidate.json`。

- candidate SHA256：`18c2d4be3e9a8dff03d7bc2f0f39bce329dc460e118d144d4c8368eb4d37c7d1`
- application SHA256：`05c805042e58eee1e6a35180dfd266065408dff77e34ffe5ca898b97c9af8882`
- application：2,727,072 字节；sourceDigest：`08f5c6adea48c656e645679d37e9a690a376a952290e72050ed4c2ac39ab7bd9`

85 项输入与冻结源匹配，normalFirmware=true，rfidDiagnostic=none，四段 `0/0x8000/0xe000/0x10000` 的 SHA、长度、16MB 分区和重叠校验通过。非 application 三段与普通 V0.38 相同，既有 V0.38 两个候选和全部段 SHA 未变化。所有段完整绝对路径及 SHA 位于候选和 `tile_esp32s3_corner-validation.json`，由主会话升级前再次核对实物 MAC 与板型。

long 候选：`build/tile_esp32s3_long/candidate.json`。

- candidate SHA256：`112221753350c2b96b25484a7c922c437bb4ef3ca4bfb752db2e0b77a0efef9c`
- application SHA256：`cb24f21227d3273972c8084eeb02a2d624cb94ca791d6c212c57e519b7463c78`
- application：2,727,056 字节；sourceDigest 与 corner 相同。

long 一次构建通过，独立验证结果见 `tile_esp32s3_long-validation.json`；两种应用静态 RAM 均为 70,660 字节。`source-comparison.json` 证明 85 个源输入中恰好上述 7 个变化、78 个完全相同，`owned-source-hashes.json` 记录共享仓库的 7 个生产文件及 2 个测试文件最终内容。候选与共享树 main 的差别是已有的正常／诊断来源差别，本轮没有将诊断混入发布。

`build-input-overlay.zip`、`build-input-overlay-sha256.json`、`v038-to-v039-overlay.patch`、两个版本的完整输入清单及 `prepare-normal-source.py` 提供可检查的复现入口。已实际运行复现脚本到独立 `reproduction-check`，再次验证 85 项完全一致。`build-normal.ps1` 明确角落优先、再长边；依赖版本与正常 V0.38 相同。跨路径重新编译仍需重新验证新二进制 SHA，不承诺完全相同字节。普通源目录含私有配置，不得整体公开；公开 overlay 不含凭据。

格子子任务未占用串口、刷写、修改游戏数据或操作Git索引；以下设备操作由主会话完成。

## 主会话四板部署与现场验收

2026-10-17 22:25 EDT收尾。主会话逐项复核85个源码输入、候选来源与四段哈希、16MB分区、板型和完整MAC；用固定esptool 5.3.1在同一串口连接中完成ROM/Stub身份复核、四段写入与再次验证，以watchdog复位正常启动。没有擦除整片Flash或NVS。

| 设备 | 完整MAC | 升级前 → 最终正常应用 | 实物结果 |
| --- | --- | --- | --- |
| COM10，角落，map0 | a4:cb:8f:c1:e1:34 | V0.38 → V0.39 `05c80504…` | 已移除PCNT诊断并恢复普通包；正常联网/ORDER。U20/U21在旧新版均暗，单独待测 |
| COM16，长边，map13 | a4:cb:8f:c2:99:08 | V0.37 → V0.39 `cb24f212…` | 用户确认10灯均参与、拖尾连贯、亮度及屏幕正常 |
| COM17，长边，map14 | a4:cb:8f:c2:98:b8 | V0.37 → V0.39 `cb24f212…` | 用户确认完整10灯流星、亮度及屏幕正常 |
| COM18，长边，map15 | a4:cb:8f:c2:98:fc | V0.37 → V0.39 `cb24f212…` | 用户确认完整10灯流星、亮度及屏幕正常 |

所有实际普通固件均回读V0.39、LCD8MHz/rotation2、LED20ms worker、10灯、发送失败0。窗口内已恢复HTTP200及对应0/13/14/15格；启动期最大调度间隔约381～384ms和1～2次late保留，不能据此声称任何负载下都严格20ms。三块长边预览均有界结束，`LED AUTO`后普通帧限幅回到16，串口全部释放。COM18升级前曾无串口RX，升级后正常响应，该原始空窗口保留。

设备证据在本报告前述`GridopolyDebugPairingRuntime`目录：`v039-COM10-flash.json`、`v039-COM10-restored-flash.json`、`v039-COM16-flash.json`、`v039-COM17-flash.json`、`v039-COM18-flash.json`；恢复/启动记录分别为`v039-COM10-restored-observe.jsonl`、三个长边的`v039-COM*-startup.jsonl`，长边三色预览为`v039-COM*-meteor-preview.jsonl`。COM10初次首板及RGB对照记录另外保留，不用之后的成功覆盖其光学失败。

22:23与22:24:59 EDT两次只读HTTP（`v039-after-flash.json`、`v039-final.json`）确认16模块在线、唯一16节点ORDER闭环ready，环顺序与升级前一致，本批四板均已分配、reader stable、V0.39。刷写过程中节点重启曾使ORDER失效并暂时撤销分配，结束后自行恢复，未用手动分配掩盖。观察期间房间从993580118变为993580119，最终version15；本轮没有调用新局、修改资产、Tag绑定或掷骰接口，因此不将前后业务状态描述成完全相同。

主会话再次以独立收尾校验确认最终16/16 assigned、16/16 reader stable、orderIndex恰为0～15、四角0/4/8/12、版本分布4×V0.39＋12×V0.37；四份最终普通包写验报告及四板关闭串口记录匹配。结果为`v039-root-final-validation.json`，不将缓存的设备在线状态当作持续在线保证。

本次完成所接四块的固件升级；三块长边灯效与显示现场通过，COM10两灯问题独立未闭合。诊断结果支持优先查此板U19至U20的数据链、U20/U21局部供电、焊接或灯珠，但尚未做实物连通/波形测量，不指定某颗已损坏。其他角落正常仅为用户反馈，不算本轮逐板测试。实际玩家配对拿放与真实掷骰移动联动仍需游戏操作验收，维护预览不替代端到端结果。其余9长边＋3角落共12板尚未更新，本次没有对它们后台刷写。
