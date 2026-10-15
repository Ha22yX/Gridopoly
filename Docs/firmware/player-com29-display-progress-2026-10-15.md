# COM29 等待页频闪与偏移诊断（2026-10-15）

## 当前状态

用户报告第四台刷后“有频闪和错位，这次是向下偏移了一点”，随后确认页面为 `Waiting for new game`，底板已返修且接着电池。后续补充静止时持续异常、当前看起来偏上，又纠正最初可能就看错方向；因此可靠事实是静止等待页持续频闪/偏移，最初方向不确定，不把方向变化作为动态扫描失同步的证据。该16MHz版本的COM29显示验收记录为 **FAIL、重新打开**；前次完整备份、四段写验和联网证据仍有效，但不能替代目视。COM27/28 同版本等待页曾获用户正常反馈，保留为对应设备和场景的历史对照。

12MHz诊断候选已于11:06 UTC完成COM29写验。用户随后明确反馈 `Waiting for new game` 页“画面正常，频闪和错位都消失”，本轮该设备静止等待页目视为 **PASS**；旧16MHz失败记录保留。它支持降低请求PCLK可消除本次症状，但没有证明RGB underflow的唯一根因，也没有证明动画性能或其它设备场景全部通过。135秒有界观察已结束并释放串口。

当前COM29运行普通功能的12MHz诊断应用 `716ff623...`，临时日志窗口已自动结束。同样12MHz、无临时诊断日志的普通候选 `3da4c493...` 已构建并核验通过，尚未部署，等待主会话安排单独写验和目视窗口；USB热点已部署、原7台容量阻塞解除。保持UI、porch、core1、mode3双帧缓冲、40行bounce、BatteryBoard=none和既有性能叠层。COM27/28及旧冻结v4保持16MHz。本轮不把12MHz设为全部屏幕的生产默认。

## 身份和被动日志

- 设备：COM29，完整 MAC `20:6e:f1:ba:c9:04`；PnP 父身份 `USB\VID_303A&PID_1001\20:6E:F1:BA:C9:04` 本轮重新核对匹配。
- 10:43 UTC基线时运行冻结 `trial16-2026-10-15-v4` 的普通应用 `dfc4b7e547a129fd5b500396a2a1f0e3866edf80b53d6684f0830d0539340cb6`，`BatteryBoard=none`。该被动窗口没有ROM reset或重新读取Flash，版本来源为前次受保护写验；后续诊断写入另记下文。
- UTC 10:43:27.790128 起被动采集 30.093 秒、2580 bytes；DTR/RTS=false，不发送串口命令，无主动复位；error=null、closed=true。
- uptime 3900984～3925991ms，Wi-Fi status3、IP `10.42.0.46`，持续发现 room993580113。日志 heap 约33128～33168B、largest22516B，无窗口内启动、panic 或 brownout 标记。
- 主会话 UTC 10:43:29.765 HTTP 记录四屏 online/eligible，原三真人加一机器人局保留，COM29 seat0 等下一次新局。未创建新局、操作 Tag、改变 AP 或游戏状态。

这些日志没有 RGB underflow 计数，也没有本轮启动首帧；不能由联网和无 panic 推断显示正常。`none` 仅表示软件遥测关闭，不表示物理无电池，也不据此把异常归因于 USB 默认电流。

## 显示路径核对与诊断选择

当前 vendor 板型为480×480、16MHz PCLK，水平总周期548（480+8+20+40）、垂直总周期558（480+8+20+50）。按请求时钟计算扫描约52.32Hz；12MHz约39.24Hz。这里是时序计算，不是有效 UI FPS 或实际示波器测量，原24FPS/42ms门槛保留。

固定SDK的 `CONFIG_LCD_RGB_RESTART_IN_VSYNC=1` 已开，PSRAM为80MHz；不能再把启用VSYNC restart作为新增修复。本轮发现 `esp32s3-libs/3.3.11` 头文件及已部署dfc4的冻结ELF版本字符串均为 **v5.5.5**；旧报告引用的v5.5.2官方说明只是机制参考，并非该ELF的精确版本。

等待页相同可见内容已保留控件；frame ticket仍只用于MoveGuide/Auction。vendor在IDF≥5.4使用frame-buffer-complete回调，现有ticket额外等待硬件边界，不将其名称误当真实VSYNC。未修改这些同步语义。

双40行RGB565 bounce占76,800B内部SRAM。若直接改60行还需38,400B，已经超过本次日志空闲量，故不扩大bounce；也不平移UI坐标或盲改porch。只降低PCLK来检验扫描带宽/时序余量方向，不预先认定欠载就是原因。[Espressif RGB机制说明](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html#bounce-buffer-with-single-psram-frame-buffer)说明补充bounce不及时可能造成偏移，VSYNC恢复仍可能闪烁；本机尚无逐事件因果证据。

诊断开关默认为关闭，生产默认沿用板型16MHz；首轮冻结诊断使用当时的 `-DisplayDiagnostic pclk12`，后续源码将时钟与诊断分开，当前参数为 `-DisplayClock vendor|pclk12 -DisplayDiagnostic none|counters`。计数读取遵守LVGL/ISR锁，串口输出在锁外，每5秒最多一次，正常loop就绪后120秒结束；USB无可用发送容量时跳过，不等待主机。记录重建/增量更新、实际flush提交次数、switch失败、硬件frame-complete与内部内存。硬件回调数不等于LCD可见FPS，也不等于underflow计数。日志结束后12MHz继续生效，恢复须重写原候选。

## 验证、候选与回退

有界窗口检查15/15通过，覆盖5秒间隔、120秒终止、延迟loop无追赶输出及millis溢出。原输入/真实renderer回归51/51通过，其中60次普通入局重试保持等待页重建数和dirty area均为0。原有setup已将HWCDC发送超时设为0，实际SDK的发送互斥及ringbuffer均使用该值，本次无需改变串口配置。以上是主机检查，不能代替COM29目视。

本次隔离构建目录为 `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/runs/production-20261015-065404-54b8eb05df524862883ae2e86797fbec/`，Git基线 `8c5189ada9047b43b3c25bd6d20344667514026d`，实际修改尚未提交。2136项输入快照与dfc4对照：修改 `.ino`、`lvgl_v8_port.cpp/.h`，新增 `display_diagnostic.h`；其余文件（含板型与依赖库）一致。FQBN和BatteryBoard不变，flags仅增加 `-DGRIDOPOLY_DISPLAY_DIAGNOSTIC=1`。

隔离构建退出0，程序1,906,390/3,145,728B，静态RAM125,696/327,680B，HWCDC链接检查通过。四段哈希、2136项快照哈希和分区范围均通过校验；bootloader、partition、boot_app0三段与dfc4逐字节相同。实际ELF版本字符串为 `v5.5.5`。

| 项目 | SHA256 |
| --- | --- |
| candidate.json | `979f05890ea18809d798670893fcdae1c952b1ad871290acdb5e9f6f42ede1da` |
| sourceDigest | `6c0280a9ac21051972802e1562924a6bbeb6ddd68e13db4b8acc056d892a1133` |
| app，1,906,544B | `716ff623b5a4a9a31a9ce7ff6757ce344ecf56ea56f1eb3844b3ad1a0cfb5739` |
| ELF | `d8fa1bee0940baa2f7e00d404843c2e6c81ea7d57ebf084e8c0726e1ddf38bfe` |

本机 `candidate-verification.json` 保存完整flags、FQBN、四段hash、SDK及分区核对结果。主会话另行独立核对后通知执行一次诊断部署；本轮诊断代码随后由主会话提交 `ff9d3795`，发布工具门禁提交 `36e4e902`，构建本身仍对应上表精确快照。

候选显式标记 `diagnosticOnly=true`、`displayDiagnostic=pclk12`；主会话已在普通发布打包/加载入口添加诊断门禁，旧清单缺省仍兼容。COM29诊断使用独立受保护流程，不因本轮诊断改变旧冻结v4。

回退使用原冻结v4的dfc4四段，经同一受保护流程核对完整MAC、Flash和既有分区后写验；不清NVS、不迁移分区。COM27/28保留当前固件作现场对照，其他串口不操作。主会话负责候选独立复核、写验通知和即时用户目视。

## 诊断实机结果

11:05:49.899 UTC重新核对PnP父MAC，11:05:59.061～11:06:17.117 UTC同一受保护串口连接核对完整MAC、16MB和同布局后，一次写入并校验四段，通过hard-reset。未erase-all、未重建NVS、未迁移分区。固定esptool5.3.1来自本机GridopolyReleaseTools/python-packages；此前在未设置该模块搜索路径的宿主解释器检查出现缺包，未因此触碰串口或安装其它版本。

11:06:17.242 UTC立即接续无主动复位采集，135.062秒、17,431B，error=null、closed=true。采到应用build/ready；未捕获ROM复位标记，不把它写为完整ROM启动。早期Wi-Fi认证重试event113/reason2，34.091秒获得IP10.42.0.46，后续99次发现原room993580113，未额外复位或改AP。主会话11:07:13.930 UTC确认四屏online/eligible、COM29seat0，游戏sync除serverEpochMs外与基线完全一致。

共24条诊断行：首条5622ms、末条120621ms且done=1，之后到135秒观察结束不再输出。所有switch_fail=0。稳定区间40624～120621ms，rebuild保持2，incremental增加79，flush增加263（约3.288次/秒），frame-complete增加约39.251次/秒；该区间采样内部空闲33,140B、最大块22,516B。frame-complete是回调频率，不是可见FPS或欠载计数；本轮不是24FPS/42ms动画性能验收。

等待页保留分支的incremental计数对普通入局revision直接加一后返回，不调用LVGL setter，因此不能等同脏区。实际flush有独立来源：冻结与新候选的lv_conf.h均启用LV_USE_PERF_MONITOR；LVGL lv_refr.c每300ms更新系统层FPS/CPU label，频率与观察吻合。等待页本身没有时钟或spinner。此前60次重试主机测试证明该调用不增加脏区，没有覆盖持续运行的性能叠层定时器；未把它推广为整机完全不刷新。该叠层未在本轮改变，也未证实其造成偏移。

## 正常12MHz收尾

新增独立DisplayClock参数及18项配置检查，覆盖vendor/pclk12与none/counters四组合、既有默认模式及不支持组合拒绝，全部通过。默认flags和vendor16MHz保留；COM29普通候选仅增加 `-DGRIDOPOLY_DISPLAY_PCLK_HZ=12000000`，诊断宏未开启。候选及输入快照记录displayClock/requestedPclkHz/displayDiagnostic/diagnosticOnly，导出时额外核对flags和元数据。普通版仍保留既有FPS/CPU叠层。

正常12MHz命令为 `compile-isolated.ps1 -Mode production -BatteryBoard none -DisplayClock pclk12 -DisplayDiagnostic none`。fresh构建目录为工具根下 `runs/production-20261015-071115-9878b6febcb3414c8d4a1314b1b58014/`，Git基线 `ff9d3795c5d114cefffcf94fd1f60a63c85efb0c`。快照建立后主会话更新了README纯文档，该文本不属于本次冻结输入；以sourceDigest为准，不为此重复构建。

普通候选构建退出0，程序1,905,822/3,145,728B，静态RAM125,672/327,680B；HWCDC链接、四段布局、2137项冻结输入哈希均通过。相对首轮诊断快照仅修改 `.ino`、`display_diagnostic.h`，新增 `display_config.h`；无库或其它业务源码变化。应用不含 `GRIDOPOLY_DISPLAY_DIAG` 日志字串，ELF没有诊断计数函数符号，实际SDK仍为v5.5.5。三个非app段与原dfc4完全一致。候选明确normalFirmware=true、diagnosticOnly=false、displayDiagnostic=none、displayClock=pclk12、requestedPclkHz=12000000。

| 正常12MHz产物 | SHA256 |
| --- | --- |
| candidate.json | `d352da601bb6192308d99018b531a0b0b206e655c6bafd34ed4eafa69d6805c1` |
| sourceDigest | `b50401eb1522dc95896c85b8b11a05fc42498264e4832c3cac8c897a296ef2bb` |
| app，1,905,968B | `3da4c49308363997d11c437f9ec1f93bcfe8007624f3c938893b3710c676532a` |
| ELF | `72f80acd97b5cb9d0cf98aa4a233d1ca510539f1bb066193b4d623f9b2aacc4c` |

本机 `normal-candidate-verification.json` 保存完整核验。正常配置分离已由主会话提交 `13e4de40`，并报告连同前述诊断及门禁提交推送GitHub；这不表示3da4候选已经部署。主会话已收到候选；因用户新增24V供电下格子联网问题，服务器任务正在排查AP，最终刷写等待主会话新的协调通知，不沿用已完成的首轮诊断许可。主会话当前观察为总关联7台（3屏+4格，COM28由用户关闭）；重刷COM29释放的关联位置可能被其它格子占用，因此明确保持现有716ff623，先不写3da4。

下一步由主会话在网络方案明确后安排COM29单台同布局四段写验，再立即60秒被动启动观察及用户正常版本目视；当前串口已释放，不占用AP诊断窗口。原51项业务回归及15项窗口检查未因仅配置分离而重复，未追加诊断reset或其它串口测试，也不为维持活跃增加采集。

本机证据：`C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/com29-display-20261015/`；包含 `pnp-before.json`、`COM29-passive-before.bin/.log/.chunks.jsonl` 与 summary。主HTTP证据为同工具根下 `root-com29-display-20261015/before-http.json`。原始Flash、HTTP现场快照与本机构建不提交Git。历史对照见[前两屏修复](player-new-screen-display-progress-2026-10-15.md)及[COM29首次迁移](player-fourth-screen-flash-2026-10-15.md)。

## 晚间网络恢复后的当前状态

USB热点已完成迁移，16格＋4屏20台同时接入及自启动恢复已有证据。上述7台限制/避免释放名额是11点时的历史维护决定，不再是当前阻塞；本次文档更新未刷COM29。设备仍运行716ff623/12MHz诊断，普通3da4尚未部署，下一窗口须记录普通版本启动与用户目视，不复用旧16MHz或只凭日志标正常。[最新部署](usb-ap-deployment-2026-10-15.md)。
