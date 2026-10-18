# 玩家屏配对提示新鲜度与英文界面（2026-10-17）

本轮负责玩家屏固件。原理图与 PCB 由「Gridopoly · PCB设计」负责；本会话没有操作 EDA、串口、设备电源或 Git 索引。主会话负责提交、烧录与实机验收。

## 当前状态

源码及测试由主会话审核并提交为 `82e57e84`；配对主机回归 137/137、真实 LVGL 回归 141/141 通过。普通 16 MHz 与 12 MHz 候选均已构建通过并完成独立核验，主会话已完成 COM7/27/28/29 四台写入与读回校验：前三台应用为 `77af3c26…`，COM29 保持 12 MHz、应用为 `efb60d8a…`。四屏画面、电量和交易页 `LOCKED` 已获用户现场确认；真实新局逐人配对仍待验。部署、设备身份、采集及目视证据统一见[主部署记录](debug-tools-pairing-rollout-2026-10-17.md)。

COM7 首次电量初始化被异常时钟读回拒绝，用户手动重开/重接后恢复；不记为自动恢复或异常根因已解决。精确字节分析见本报告末尾。

这次修复的是确定可复现的玩家端软件缺口，不能据此认定它就是用户现场“先识别、又要求换格”的唯一原因。服务端配对与格子上报侧由对应会话另行交付。

## 配对修复

- `IdentityClientState` 将提示是否新鲜与已保存的绑定证据分开，记录 Tag 快照接收时间和剩余有效期。
- `ConnectionLost` 或非 Inactive 的 JoinStatus 变化使旧提示失效。Identity/State 心跳先恢复联网时，页面仍显示 `SYNCING PAIRING / KEEP YOUR TOKEN`；隐藏旧 reader 编号和 Retry/Cancel，外圈置灰。只有当前房间、当前席位、通过 revision 校验的 Tag 快照才能恢复提示。
- 接受态 ClearRequired、AwaitingTag、AlreadyBound、MultipleTags 使用服务端 `expiresInMs` 和接收时间到期。普通身份/游戏心跳不能续期；同 revision 的有效 Tag 可重新续期。零期限接受态直接失效，Complete 或已绑定且允许编辑身份的零期限不会清除绑定。操作提交自身也检查期限，避免在下一次 appTick 前发送过期 Retry/Cancel；无符号时间差覆盖 millis 回绕。
- 换席时，第一份新席位 Tag 可能早于 admission 到达而被正确拒绝，却已消耗 transport epoch 标记。接受席位变化后清除旧席位的 Tag 缓存与 revision 下限，下一份低 revision 的当前席位快照可以正常进入；不放宽错误席位/旧房间/旧 revision 的过滤。
- 已绑定玩家仍保留头像、姓名草稿及游戏状态；短暂留在配对页时优先显示自己的绑定结果，不展示下一位玩家的目标。
- Tag 页 retained-render 比较加入 freshness 与 success deadline，确保相同 revision 的恢复/失效会重绘文字、颜色、按钮和目标。

期限是从客户端收到快照时计时；协议没有服务端绝对截止时间，本次没有修改 wire 格式、自动发起 Retry 或清除服务端绑定。

## 英文文案与财务标签

检查 94 个 PlayerConsole C/C++/Arduino 源文件，解析普通/原始字符串及 `\x`、`\u`、`\U`、八进制转义；排除 tools 测试和本地网络配置。改动前 43 处中文字符串命中，其中生产/演示界面 41 处、已有自测期望 2 处；修改后该范围零命中。

| 来源 | 替换数量 | 内容 |
| --- | ---: | --- |
| app_state.cpp | 10 | 骰子已发送、收租到期、资产变化、初始操作提示及演示弹窗 |
| ui_renderer.cpp | 8 | BACK、PLAYERS LIST、DEVICE ERROR |
| demo_data.cpp | 22 | 演示玩家、地产与颜色组名称，含转义字节中文 |
| demo_transport.cpp | 1 | 演示目标名称 |
| logic_tests.cpp | 2 | 对应演示地产和资产变化提示的既有期望 |

“已锁定”实际是 `kLockedMarkerMap` 的 48×16 位图，文字扫描无法发现。已解码检查并移除，改成现有字体的 `LOCKED` 标签。初始 60 px 区域在真实 LVGL 中截断，修正为 74 px、右对齐，并给接收方名称保留独立区域；禁用行清除 CLICKABLE，应用层仍拒绝更改锁定接收方。PlayerLocked、CounterLocked 和 HomeEditable 的差异均有真实页面回归。

另将内置地产、交通、公用事业、头像四张 sample 位图解码目视检查，均为插画，没有中文文字；没有改动图片内容。

服务端新增事件 kind 31 在玩家财务列表显示 `DEBUG ADJUSTMENT`，与 RENT、TRADE 分开。kind 31/32 枚举及共享核心实现归服务器会话，本会话未修改核心文件。

玩家传输错误是枚举，应用已有 `SERVER TIMEOUT - RETRY` / `ACTION NOT AVAILABLE` 等英文映射；未发现直接显示服务器 HTTP 中文错误原文的路径。游戏/地产标题来自本地英文目录与代码映射，网络玩家名称保持原样，不进行翻译或修改存储值。既有不支持字形时的显示回退仍保留，真实中文姓名测试数据未删除。中文注释、文档、字体字形及本地 Wi-Fi 配置不属于翻译对象。

## 验证与证据

证据目录：`C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/tag-pair-freshness-20261017/`。

| 检查 | 结果 | 证据 |
| --- | --- | --- |
| 同一套 setup 用例运行旧生产实现 | 137 项，37 失败；含级联，不代表 37 个独立缺陷 | setup-before-final.log |
| setup-client 修复后 | 137/137 | setup-final.log |
| 真实 LVGL / 应用事件回归 | 141/141；含既有输入、姓名、建房等回归 | runtime-final.log |
| 字符串解码审计 | 94 源文件，修改后中文字符串零命中 | audit-english.py、english-source-audit.json |
| Trade 与 center-list 静态契约 | 通过 | layout-targeted.log |
| 完整旧 UI layout 检查器 | 失败，修改前基线可复现相同错误 | layout-baseline.log、layout-final.log |

完整旧检查器仍要求 drawHome 调用旧的 `uiCarouselActions(visualPhase, …)` 形式，而当前实现已有 `appHomeActions(state, …)`。本轮 drawHome 函数未改变，已逐函数比较确认；本轮仅将检查器对中文位图的要求改为英文标签，不把完整检查器写成通过。

实际 LVGL 回归包含断线/Identity 先到/同 revision 恢复、reader 更换、过期时外圈/文字/按钮清理、下一位玩家与已绑定玩家隔离，以及 Players List → 玩家详情 → Financial Report → Trade Offer 路径。`LOCKED` 和 `DEBUG ADJUSTMENT` 使用实际字体及可用区域测量，没有截断。截图由真实 LVGL flush 生成，动画完成后采集：

- `trade-player-locked-english.png`：BOB 接收方及完整 LOCKED 标签。
- `financial-debug-english.png`：DEBUG ADJUSTMENT、+$250 与下一条 RENT。
- `locked-marker-before.png`：修改前中文位图的解码结果。

测试夹具首次使用不存在的 `lv_color_eq`，以及未等待 LVGL 布局、未完成列表先居中再激活的交互，造成过中间失败；相应日志保留。最终测试已按实际 API、布局与交互运行。60 px 标签截断和禁用行默认 CLICKABLE 则已在生产实现中修正。

## 源码与候选

本会话源码范围：

- `Firmware/PlayerConsole/app_state.cpp`、`app_state.h`、`app_types.h`、`tag_pair_view.h`
- `Firmware/PlayerConsole/ui_renderer.cpp`、`demo_data.cpp`、`demo_transport.cpp`、`logic_tests.cpp`
- `Firmware/PlayerConsole/tools/test-setup-client.cpp`、`input-runtime-tests/runtime.cpp`、`verify-ui-layout.py`
- 本报告。

两份隔离源快照各 2146 项，内容哈希完全一致，冻结时 checkout base 都是 `d3455140c374869943f00b4a03f6382075b2631e`。本轮修改在冻结时尚未提交，后来由主会话提交为 `82e57e84`；候选保留原始 base，不重写成新 HEAD，精确内容以 source manifest 为准。相对已部署电量恢复候选，只有上述 8 份 sketch 源文件和服务器会话的 GameEngine.cpp/.h、GameModel.h 三份核心文件变化；电量恢复、弧形 UI、防抖、显示策略和触摸诊断源内容保持一致，见 source-comparison.json。

其中 1328 项文件与通过 141 项回归的实际 LVGL 主机 stage 逐字节哈希一致，见 test-source-binding.json；这证明测试与候选使用同一份相关源码，不代表已执行硬件验证。11 份本会话代码/测试文件的审阅清单见 owned-source-hashes.json。

| 配置 | 隔离目录 |
| --- | --- |
| 普通 16 MHz，COM7/27/28 | `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/runs/production-20261017-210544-e659105cef5c4823b76df7d08dbdd74f/` |
| 普通 12 MHz，COM29 | `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/runs/production-20261017-210544-acf6ee63a7604596aa883b5d14fbf8f6/` |

两份均使用 production、viewe-md80et-v10、displayDiagnostic=none、core1/mode3，分别 16/12 MHz；构建退出码均为 0。独立脚本确认每份 2146 项冻结文件及其当前来源哈希一致、两配置源内容一致，四段 SHA、应用内嵌 ELF SHA、3 MiB 应用分区及 16 MiB 无重叠分区布局全部通过。三个非应用段与之前已验收的普通候选相同。完整结果见本机 `candidate-verification-root.json`；未改写候选冻结来源。

| 配置 | 应用文件字节数 | 应用 SHA-256 |
| --- | ---: | --- |
| 16 MHz | 1,945,952 | `77af3c26585cd6baa2ecd487a81a278f8be31e19eccf76c6e7cabdabcd87a6b7` |
| 12 MHz | 1,946,112 | `efb60d8a7113a4fcb4b90a62d2497335575822060ad78cf6c42312a9e29ac047` |

### 16 MHz 交付

- candidate：`C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/runs/production-20261017-210544-e659105cef5c4823b76df7d08dbdd74f/candidate.json`
- candidate SHA-256：`56af872de026053c224389a1742cab0925675ef6271a5a3dc100d64934421861`
- sourceDigest：`033d6a68effa95753f79bdea6efecbf0accc9ed82ecc79516e439e054065393e`
- ELF SHA-256：`314ab859e314f44289c39a53d6894a6a356c64f2467ec216e4c7bc54e1100f98`（等于应用内嵌值）

| 烧录偏移 | 文件（相对上述候选目录） | SHA-256 |
| --- | --- | --- |
| `0x0` | `build/PlayerConsole.ino.bootloader.bin` | `31b3c1be45dc5a76aa85c82540d6787b675e711eaf11021a5eea7e36f469c6de` |
| `0x8000` | `build/PlayerConsole.ino.partitions.bin` | `ace02503447d0f470692e65fa76002f2d77a92dc81cd3813d8aa66718d716da9` |
| `0xE000` | `build/boot_app0.bin` | `f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0` |
| `0x10000` | `build/PlayerConsole.ino.bin` | `77af3c26585cd6baa2ecd487a81a278f8be31e19eccf76c6e7cabdabcd87a6b7` |

### 12 MHz 交付

- candidate：`C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/runs/production-20261017-210544-acf6ee63a7604596aa883b5d14fbf8f6/candidate.json`
- candidate SHA-256：`936d6168b5ed5d7c4e0ca310e93fcc3b519dca5172a05dd7e6fcab335c98759d`
- sourceDigest：`8e769e4ed0898bd605f1319ed3d6cd04e6bac4606e47cbdd5c39741948eb9e12`
- ELF SHA-256：`f7c75994ab2105e7114707a044464f673fe8e9ca9fde08fd405ff2e9634e7bf8`（等于应用内嵌值）

| 烧录偏移 | 文件（相对上述候选目录） | SHA-256 |
| --- | --- | --- |
| `0x0` | `build/PlayerConsole.ino.bootloader.bin` | `31b3c1be45dc5a76aa85c82540d6787b675e711eaf11021a5eea7e36f469c6de` |
| `0x8000` | `build/PlayerConsole.ino.partitions.bin` | `ace02503447d0f470692e65fa76002f2d77a92dc81cd3813d8aa66718d716da9` |
| `0xE000` | `build/boot_app0.bin` | `f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0` |
| `0x10000` | `build/PlayerConsole.ino.bin` | `efb60d8a7113a4fcb4b90a62d2497335575822060ad78cf6c42312a9e29ac047` |

16 MHz ELF 的静态 RAM 合计为 126,424 字节，与已部署电量恢复候选一致；`.dram0.data` 增加 16、`.dram0.bss` 减少 16，合计变化 0。两份编译摘要也均为 126,424 字节。应用文件实际大小使用上表值，未将编译器的程序段摘要当作 bin 文件大小。该统计不代表实际运行堆、栈余量或屏幕视觉验收，详见 memory-verification.json。

## COM7 首次电量失败与手动恢复

主会话烧录后首次 25 秒被动采集，在启动约 6～31 秒的六组快照中均读到 `regs=557E13DF66`、`init=557E13DF66`，`fail=16/op=16/e=4/rx=5/5`、`fix=0`、`link=0`。生产读取帧为 `52 02 03 07 08 09 50`，五字节的对应关系如下：

| 寄存器 | 首次读回 | 当前驱动要求 |
| --- | --- | --- |
| 02 / PortConf1 | 55 | 输入模式配置可由现有路径恢复为 00 |
| 03 / PortConf2 | 7E | 输入模式配置可由现有路径恢复为 55 |
| 07 / I2CClkL | 13 | 只读，必须为 13 |
| 08 / I2CClkH | DF | 只读，必须为 00 |
| 09 / I2CTO | 66 | 只读，接受 66 或既有 67，并记录超时能力 |

直接拒绝条件为 **08=DF**。`InspectBridge` 保存首次及最近原始读回后，先检查 07/08/09；检查失败即返回 `BridgeConfig`，尚未执行端口修复计数或配置写入。因此 `fix=0` 符合实现，并非 03=7E 仍被旧端口校验挡住。已有暖启动恢复只修复 02/03，不修复时钟及超时寄存器。

版本查询和五字节读回成功，不能把本次错误解释成 UART 收包超时。驱动尚未到达 MAX17048 访问阶段，日志中的 `gauge=0000`、`i=00` 是未采样缓存，不是电量计返回零或已损坏的证据。[NXP 数据手册](https://www.nxp.com/docs/en/data-sheet/SC18IM704.pdf) 将 08:07 定义为一个 16 位 I2C 时钟设置，复位值为 00:13；DF:13 与所要求的时钟配置不同，不能仅放宽校验当作兼容。07/08/09 保持只读，也避免重新触发厂家确认的相关寄存器写入冻结问题；具体批次和异常来源仍需独立证据。[NXP 官方问题记录](https://community.nxp.com/t5/Other-NXP-Products/SC18IM704-internal-register-7-8-and-9-write-freezes-chip-access/m-p/1579139)

针对这五字节的隔离主机检查已完成，结果为 **10 项、0 失败**。检查使用生产 `battery_telemetry.cpp/.h`，验证精确错误阶段、原始证据保留、失败前无电量计访问及无配置写入；对照证明仅 03=7E 时现有端口修复可用，仅 08=DF 时仍被拒绝。另验证模拟只重启主机不能清除桥寄存器，而模拟桥复位后现有流程可重新通过，且不写 07/08/09。这是软件路径测试，不是物理电源或实际 I2C 波形测量。证据目录为 `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/com7-clock-readback-20261017/`，结果见 `results.log`；未因此修改生产源码、重新构建固件或操作串口。

随后用户明确确认期间手动重开或重接。主会话第二次 30 秒被动采集的六组均有效，SOC 为 99%、电压约 4.16 V；新的初始化读回为 `55 55 13 00 66`，`link=1/mv_ok=1/io_ok=1/e=0`。其中 `regs/init` 是初始化检查缓存，不能误作采样时配置寄存器的实时值。新启动记录没有继承旧启动的首次错误，首次失败原始日志单独保留。这一结果解除当时 COM7 电量显示阻塞，**不证明自动恢复，也没有解释首次 08=DF 的来源**；UART 启动干扰、遗留配置或其他原因均未被实证。此后没有追加硬件操作或规避校验的代码改动。

原始证据为 `C:/Users/kicof/AppData/Local/GridopolyPlayerBatteryRuntime/english-pairing-first-20261017-COM7.log` 及 `english-pairing-four-20261017-COM7.log`，相关摘要和用户现场反馈由[主部署记录](debug-tools-pairing-rollout-2026-10-17.md)统一归档。

## 剩余验收与负责方

玩家会话已完成源码、定向回归、候选构建、独立核验及上述失败分析；主会话已完成四台部署与版本核对，并收到四屏画面、电量及 `LOCKED` 英文显示的现场确认。最新设备与验收状态以[主部署记录](debug-tools-pairing-rollout-2026-10-17.md)为准，COM29 继续保持 12 MHz。本会话没有操作串口或独立完成这些实机验收。

主会话继续协调真实新局逐人配对及扫描波动场景；调试资金财务记录目前具有真实 LVGL 回归证据，不能将交易标签目视确认扩大为所有线上财务行为通过。报告由主会话定向审核提交。原理图和 PCB 仍由「Gridopoly · PCB设计」负责；COM28 手写事项与本轮历史失败不重新开测。
