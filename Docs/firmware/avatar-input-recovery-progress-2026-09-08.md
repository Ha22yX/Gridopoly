# Avatar Setup旋钮连跳与顺序异常：调查和修复进度

## 用户症状与证据界限

2026-09-08用户报告新建对局Avatar Setup选中字段后，一格旋转偶发连续跳多个预设，顺序也不完全正确。该问题新增到正在进行的玩家24FPS/断线恢复工作，不替换原目标。

当前正常COM7仍为ded1375634d29cc454ce401dd7e1da56373e91055d2bd464a865bee3d3c31a8c。90秒只读观察opened1788911819560、closed1788911909585，无写入/reset，原始文件在本机GridopolyPlayerTools-3311/avatar-input-baseline-20260908-1957。刚打开时USB有积压旧日志，旧GRIDOPOLY_INPUT没有deviceMs、原始GPIO或recipe值；不能把同一host批次当作一次物理转动，也不能把用户简短“1”回复反推成完整5+5步验证通过。用户随后明确该问题是小概率。

## 已确认的源码缺陷

1. 原RotaryInputFilter仅拒绝35ms内反向事件，接受同向返回。因此原始+1/-1/+1净1，输出却是+1/+1净2。主任务用真实旧header单独MSVC编译并执行复现，before.exe输出raw_net=1 applied_net=2、exit1。旧header、reproducer和独立ESP32编译日志保存在本机Temp/gridopoly-rotary-review，未把人为输入冒充物理GPIO捕获。
2. 原ESP32_Knob 0.0.1使用3ms定时、A/B分别两次采样防抖。READY同时存在A/B变化标志时固定选A，下一轮会消费留下的B并发方向事件；没有验证联合Gray变化是否合法。两路同时跳变或采样漏过中间相位时不应猜测方向。 主任务又提取实际旧iot_knob.c的类型和完整knob_handler，在GPIO替身中施加11→00双位变化并保持，3次采样后旧驱动count_value=1，复现无合法方向信息仍生成一步；输出invalid_two_bit_transition=11_to_00 old_direction_count=1、exit1。材料knob-before.cpp/exe及原始source SHA在同一rotary-review临时目录。新decoder对应非法双位用例输出0已通过；这仍是原函数模拟，不冒充用户实际GPIO波形。
3. appHandleInput/Avatar applyIdentityDraftDelta没有多步加速：按delta对字段编号循环，队列每轮拆成±1。仅显示刷新变慢不会自己改变recipe，但会使积压输入集中呈现。
4. 玩家端审查发现previewDescriptor/front-buffer由后台改写，LVGL可能仍读旧front；旧头像页面删除之前还可能释放它的图像。另UI忽略frame.exact=false，把新选项文字与上一张头像并列且隐藏更新提示。这些显示问题由玩家任务修复，不能因此否认输入缺陷。

服务端只读核对30/30 GAVC文件SHA及kind/preset编号与仓库manifest一致，按numeric1..10寻址；20发色/8肤色着色RGB一致，重复相同recipe的RGB565与ETag一致。没有发现h1/h10/h2字典排序或服务端编号串位。仅Snow white UI色块0xEBEAEA应为0xEBEEEA，交玩家一并修正。证据在Temp/gridopoly-avatar-catalog-audit-20260909.json，已核对上述显示行为存在于ded137实际源码快照。

## 输入修复实现与验证

替换ESP_Knob回调适配为项目内RotaryQuadratureDecoder：同一次GPIO_IN_REG读取A/B，1ms ESPtimer采样，两次跨至少1ms的相位一致才接受；同一时刻的补发timer调用不能当作重复稳定观察。合法单比特Gray转移累计到00/11端点输出一步，保留原驱动半周期粒度及六点钟安装的单次方向反转。接点返回会抵消未完成相位，双位非法变化不猜方向，重同步后继续；快速真实反转不再被35ms单向过滤删除。

输入队列仍固定32项、同向合并后每轮消费±1，按钮边界不合并。新增64项有界原始相位诊断，记录deviceMs/rawAB/stableAB/emittedStep/invalidTransitions/traceDropped/queueEntries；回调不打印、不分配。主循环诊断开关与Avatar前后编号日志由玩家任务集成，输出不得反过来占用高频回调。

`tools/test-rotary-input.ps1`执行与设备共用的15项测试通过：两种端点、快速反转、接点返回、+/-/+净位移、非法双位/重同步、启动在半途、同刻采样、millis回绕、4000次快速合法双向变化、Avatar五字段正反序及环绕。逻辑自检另增加队列反转净位移与按钮顺序测试，待完整设备SelfTest。hardware_input.cpp已用实际ESP32S3 fresh构建的compile_commands重定向当前源和独立对象编译exit0，确认GPIO寄存器/esp_timer API适配；不等于最终固件链接、烧录或物理操作通过。

## 待验收

输入修复源码已交接玩家任务合入缓存/绘制修复。尚需最终fresh构建、完整SelfTest及24FPS实测、正常候选部署、真实单格/反转原始相位到recipe的对应验证，再做已授权断线窗口。用户尚有未提交头像/名字草稿，已请其完成确认，期间不重刷COM7；这不阻挡代码与构建继续。没有使用定时任务，也未修改线上房间、头像草稿或玩家资产。

## 玩家端异步预览与诊断集成（20:12 EDT源码冻结，实机待验）

后台finishCompose保留generation+recipe双重校验，但不再直接修改LVGL图像descriptor或front。新增AvatarPreviewBuffers所有权状态：worker只写back，完成后pending back与正在显示的front都保持不变；UI在LVGL锁内获取pending后才切换descriptor，之后旧front才可供下次合成使用。新选择丢弃未显示的旧pending，进行中的过时合成不能发布。发布通知只唤醒UI领取，不从worker调用LVGL。

main在锁外只调用remoteAvatarCacheRequestPreview排请求；remoteAvatarPreviewFrame及兼容精确图API要求LVGL锁。setup释放移到renderAppFrame之后仍持LVGL锁，旧页面对象先移除；进行中的合成完成后再清理组件/预览。释放后立即禁止再次暴露旧preview，防止快速退出/重入期间悬空引用。preload进入Preview不再立刻freeFinals，旧public头像等AvatarLoading/Setup新页建立后在锁内释放。未放宽原generation检查或增加第三张大图缓冲。

初绘和增量两处保留旧完整头像时，若exact=false显示UPDATING PREVIEW；新选项文字不再暗示旧图已对应当前选择。Snow white色块修正为0xEBEEEA。该提示说明合成尚未完成，不把正常异步延迟标记成recipe状态倒退。

验证：生产使用的所有权类通过22,768项检查/20,000次交错操作，涵盖未领取back不能复用、worker完成不改变front、UI只领取当前generation、过时/失败合成、快速丢弃重试以及显示像素在UI领取之间不可变。工具test-avatar-preview-buffers.{cpp,ps1}。当前ino/remote_avatar_cache.cpp/ui_renderer.cpp已用真实ESP32S3 compile_commands定向独立对象编译，三项exit0；layout检查通过。此为主机及编译边界证据，不是实际合成竞态被物理复现，也不是设备修复验收。

INPUT TRACE ON开启90秒原始相位输出，INPUT TRACE OFF提前结束；默认关闭，自动到期继续排空诊断但不丢正常输入。主线程打印，回调不打印；开始仅清诊断，不清解码器/应用队列。GRIDOPOLY_INPUT增加input_ms/consumed_ms；GRIDOPOLY_AVATAR_INPUT记录focus/editing及recipe前后hair/hairColor/face/skin/outfit编号。提取实际serviceInputTrace编译模拟10项通过，含默认静默、有界排空、分段命令、计时回绕自动到期、超长命令恢复；日志在本机input-trace-review-20260908/result.log。

合并0630c01输入修复、预览修复与边框性能修复的fresh selftest/production正在构建，分别为201239与201248独立run/build/库快照。此前194516边框-only fresh SelfTest构建通过但未烧录，未含新增输入/预览修复；194319中间production也未部署。COM7仍ded137正常固件，当前用户又新建room993580100，旧room99/98不可直接沿用。未提交头像/名字的保护请求仍pending，未把未回答或新建房间视作同意重刷。

### 20:32 EDT 合并fresh构建通过，设备仍保护原草稿

两份最终候选均从唯一run/build、本轮sketch/库快照构建，exit0且HWCDC检查通过，与正常ded137分区相同；各run保留inputs.json及非merged产物artifacts.json。SelfTest为201239-cb733606813d42b9b14c196ac2f4217c，app SHA256 c8faac38240d23db2ccad54b76357ed330668142bf1149afc5608189b573933d，2065936 bytes（program2065790/RAM156672）。正常候选为201248-5be1e6777cf443feb24a5b6d9864feea，app SHA256 ad8a154d50bab39551bf028d0e126f96b8e8687751071c82cdab1bf65b423b54，1890064 bytes（program1889922/RAM124568）。均含0630c01输入修复、预览交接/提示/释放、诊断、重复到达resync及边框优化。

当前未烧录新候选，COM7继续正常ded137且已释放。唯一实机前置阻塞为用户尚未确认完成当前头像/名字保存；root最后现场room993580100。待主任务确认可安全切换后，以有界SelfTest窗口实际验证完整逻辑、组件及七场景原性能门槛，并确保结束恢复正常固件；再部署正常候选、90秒显式INPUT TRACE对应物理单格/反向验证，最后与服务器分别执行UDP120秒/单站断关联90秒。后两者分别证明通信恢复/基本WiFi自动重关联，不能冒称触发30秒recoverWifi分支。此前四项性能失败仍保留，新构建不能替代24FPS或新输入修复的实机验收。

源码检查点已由主任务统一提交：0630c01（旋钮输入）与ff348a2（预览所有权、重复到达、边框绘制、构建与验证工具）。玩家任务未执行Git写入。当前16个实现/工具文件的校验清单位于本机review-system-20260908/source-manifest.json；其中10个固件输入分别与两份最终候选快照逐文件核对，20/20匹配。两份进度报告已补齐候选SHA、实际设备仍ded137和待验收，交主任务统一提交。

主任务已向用户提供当前测试草稿的具体选择：保存后更新，或明确允许丢弃草稿直接更新；两种选择尚未得到确认，未回答不视作授权。接到明确选择或确认保存的现场证据后，由主任务立即续派本任务执行已准备好的设备窗口。当前仅交付可审查的源码/构建阶段，不关闭≥24FPS、真实旋钮和两段恢复验证的总体目标；没有重建自动化。

### 20:52 EDT 用户允许丢弃草稿后的合并实机验证

用户已明确允许丢弃当前头像/名字草稿，原烧录前置阻塞解除。system-window-20260908-2043 对 c8faac38240d23db2ccad54b76357ed330668142bf1149afc5608189b573933d 实际烧录并运行完整 SelfTest：pure=1/component=1/perf=0，clean_before=1/clean_after=1，FIRST FAILURE NONE。七场景 fps/首帧/最大间隔分别为 WAIT_FWD 26/37/39 PASS、WAIT_WRAP_REV 28/17/39 PASS、MYTURN_5 26/35/39 PASS、RETARGET 26/18/57 FAIL、SWIPE_EVENT 26/31/39 PASS、ASSETS_SCROLL_COLD 24/55/58 PASS、ASSETS_SCROLL_WARM 25/35/58 FAIL（间隔单位ms）。Cold沿用既有60ms最大间隔例外，其58ms不能写成小于42ms；其余门槛未放宽。全部平均FPS已达24，仍有两项间隔失败，不能标完整性能通过。

SelfTest结束finally成功恢复ded137，45秒正常采集后，按主任务明确安排部署功能正常候选ad8a154d50bab39551bf028d0e126f96b8e8687751071c82cdab1bf65b423b54，不让输入/预览修复继续停留源码。normal-system-window-20260908-2046保存候选SHA、完整上传和45秒启动：MAC dc:b4:d9:02:d1:dc，build Sep8 20:23:51，初始关联reason2重试后ms12242取得10.42.0.37，配对room993580100/seat1/session1245378996，v13/phase0/cash800/position0；30个组件均ready，未见组件retry/failed，后续status3稳定至ms45313。

正常启动下载附近有29行可识别setSocketOption/Bad file number错误（并发日志交错，不能等同29次调用）；旧ded137恢复也有同类错误。源码downloadAsset在HTTP连接前对WiFiClient调用setNoDelay(true)，该core直接setsockopt(fd)，fd=-1会产生此日志，因此存在已定位的调用顺序问题；此次30/30组件完成，不把该日志当下载失败，也不声称零错误。后续修复应随性能候选一并构建验证。

独立物理INPUT TRACE于20:50:28–20:51:58 EDT开启90秒，启用设备ms212113；目录input-trace-20260908-205028。只发送诊断ON/OFF，不reset或注入游戏输入；原始phase/step/recipe/丢失计数用于核对真实detent。主任务已收到开始/截止并负责用户最小操作请求；未出现实体操作或超时不得当PASS。两段实际网络中断仍未执行，将在独立输入窗口结束后协调。当前实际设备是ad8a功能候选，性能仍有上述两项FAIL。

### 20:58 EDT 物理输入确认与下一性能候选

用户向主任务明确确认“每格一步、方向正确，最后回到原选项”，本轮症状按用户实机验收通过，不扩展为全部间歇边界已证明。实际操作发生在90秒RAW TRACE结束后的recovery-udp-20260908-2052只读段：59个Avatar事件，45个编辑事件的delta到对应字段编号模数变换全部一致，应用延迟中位2ms、最大130ms，设备输入ms409685至456045；分析保存在该段avatar-input-analysis.json。普通INPUT/recipe有证据，原始AB未在实际操作期间采到，不声称完整相位到detent波形验收。无需用户重复同样操作。

新的draw_bg优化只在无外部mask、无gradient、归一化opa=COVER且clip完全位于缩进2px的圆角填充内时使用一次实色blend；保留AA边缘和所有其他旧路径。第一版对半透明同样优化在case10813 AA关闭时失败，原因是原mask128会阈值化为0，而直接blend128不会；该错误版未构建固件或部署。限制opaque后54,190组实际LVGL新旧逐像素一致，覆盖11种alpha边界、4色、全部blend mode、gradient/外mask回退、AA开关、边框缩进及圆弧边缘。日志pixel-bg-20260908-2055.log与目录inputs.json保存源/参考/config SHA，host总CPU5414→5091ms仅作host参考，不代替ESP32性能。

同一候选将downloadAsset的setNoDelay移到http.GET后且client.connected()时，避免连接前fd=-1。完整fresh SelfTest与normal构建已启动，当前设备仍ad8a。新自检将在网络窗口完成后执行，finally回到已验收ad8a，不回退缺输入修复的ded137。两项现有性能FAIL继续保留至新实测；不改24FPS/间隔/首帧门槛、AA、PCLK或frameTicket同步。

### 21:00 EDT 两段真实网络恢复证据

正常ad8a上单设备UDP阻断实测：服务器观测UTC00:55:58.125–00:57:58.125，Pi规则00:56:13.513生效、00:56:53.516到期、.541清理tableAbsent=true，入向drop15/出向0。设备记录GRIDOPOLY_UDP degraded后重新发现，00:56:56.500服务端PairAccept，同seat1/session1245378996；HTTP v13→offline v14→online v15，设备v15/phase0/cash800/position0。完整服务端HTTP141样本/564GET零错误且业务状态未变。玩家COM7段recovery-udp-20260908-2055从00:55:06.072记录170秒，略早于服务器尾部约2秒结束，下一串口段重新打开的间隔如实保留，服务器观测连续；未把WiFi一直status3当成断关联验证。

随后独立单站断关联90秒服务器窗口00:58:40.276–01:00:10.276，COM7为recovery-wifi-20260908-2058，140秒有界。实际设备DISCONNECTED event113/reason2 ms718828，短暂CONNECTED718872后IP0，wifi_lost及应用recoverWifi ms718875，udp_stop_start718875/done718876，reconnect_start718876、return requested1/elapsed3ms，随后reason8/718878、CONNECTED718938、GOTIP719968并恢复原seat/session/v15及现金位置。断关联到GOTIP约1140ms，后续稳定记录仍在完成。

需修正此前预期描述：30秒kWifiRecoveryMs是距lastWifiAttemptMs（beginWifi/recoverWifi时更新）的重试节流，不是必须连续断线30秒。本次开机已718秒，短断关联确实进入recoverWifi并证明WiFi.reconnect返回、重新DHCP和配对；不是仅SDK自动重关联，也不是30秒长断网耐久测试。没有WiFi OFF/ON/AP重启、拒绝列表或测试游戏动作。当前phase0无pendingMove，移动中恢复与幂等由服务器隔离用例证明，不冒称此次现场发生移动恢复。

21:01 EDT补记：独立WiFi服务器90秒及COM7有界采集均正常结束、串口已释放。设备至ms816018保持status3/IP37，无panic；服务端尾部确认nft空、四服务active、目标associated、v15业务不变且未见watchdog介入。后续不再中断网络。两项本轮固件输入与SelfTest/production快照4/4 SHA相同，清单bg-source-snapshot-comparison.json。源码检查点110a5ba，下一候选仍在fresh构建，不能把此检查点或网络结果当成未完成性能通过。

### 21:25 EDT BG候选实测未解决长帧，转入圆外裁剪因果核对

bg-perf-window-20260908-2113实际SelfTest32bc19203fad156b224e7eb5731992df77c3a115d9f24f0d585eb45033b08fa3（2066112bytes/program2065970/RAM156672），pure=1/component=1/perf=0/clean_before=1/clean_after=1/FIRST FAILURE NONE。WAIT_FWD26/34/39、WAIT_WRAP_REV28/18/39、MYTURN_5 26/35/39、RETARGET24/34/58 FAIL、SWIPE_EVENT26/35/39、AssetsCold24/52/57 PASS原60ms例外、AssetsWarm25/31/57 FAIL（FPS/首帧ms/最大间隔ms）。BG内部优化仍不足。新normal4d71693b186aa6b1ce9ff0a051d05e32678a18bcefb6563daa341e2766f01c40已构建（1890256bytes/program1890110/RAM124568）但未部署。finally恢复ad8a并45秒稳定，room993580100/v17/seat1/session1598407263/phase0/cash800/position0，30组件ready，原socket日志仍在该旧恢复固件存在；COM7释放。

root独立native证据显示右下PERF标签bbox与大圆外框相交、实际环像素贡献0；标签每300ms刷新与Assets尖峰时刻/额外约3800px吻合。根因仍待ESP32因果对照，不能由host时间宣称设备提升。新外侧裁剪只在draw_border_generic中，clip完全落在单个圆角象限，最近clip点距圆心的int64平方距离大于(rout+2)^2才返回；不直接使用仅检查四角的_lv_area_is_out，避免跨圆或包圆clip误判。不关闭PERF overlay、不改变其样式/频率、AA或帧率门槛。

68,014组实际LVGL逐像素对照在正常宏和SelfTest诊断宏下分别通过，包含四角、包圆/跨圆大clip及原有各种opacity/blend/mask/gradient/AA/边界/非方/负坐标/radius1、2/outline。证据pixel-outer-normal-20260908-2122.log、pixel-outer-profile-20260908-2120.log，各自inputs.json记录精确源/参考/config。测试工具新增-Profile验证实际C计时宏编译；修整background匹配正则为显式转义，baseline只移除三个优化块。

新SelfTest增加有界诊断：bg/bg_img/border/outline/shadow累计us，radius init/calc/hit/miss，最慢矩形coords/clip/radius/border/shadow/opa，每帧最多6个实际flush区域和溢出计数。与原s/w/c/r/g一样跨完成的last-flush边界；mask计时嵌套于绘制步骤，不能相加当独立耗时，frame索引从0开始。只在场景全部完成后打印，不在动画或ISR串口输出。完整OFF七场景静态结果后运行ON七场景，BASELINE前缀单独保留；最终pure/component/perf判定仅按ON完整七场景原门槛。生产宏无这些计时/诊断，外侧裁剪默认启用。

21:24两份唯一目录fresh构建已启动，包含完整诊断/圆外裁剪，未刷入。当前正常设备仍ad8a。构建成功后检查新增静态诊断RAM、启动及七场景A/B，以有界窗口finally恢复ad8a；只有ON全门槛通过才部署新正常候选。性能尚未完成，继续执行已授权工作。

21:28 EDT构建修正记录：21:24 SelfTest在Arduino依赖扫描早期失败，port直接包含src/draw/gridopoly_draw_profile.h未被库发现器解析；未形成候选、未上设备。条件include已移到lvgl.h公共入口，21:27唯一目录selftest-20260908-212716-f6e8c4cc224e4f2bbc638664aba5a99a重新fresh构建。root审查指出A/B开关写入必须在LVGL锁内，OFF/ON两处均已lock(-1)/写入/unlock，并保留fixture稳定等待；新诊断快照包含修正。21:24 production-20260908-212452-404ec1c6f41a42769896f3493954b91b继续构建，其与修正版差异均被SELF_TEST条件预处理屏蔽，生产绘制代码不变；后续记录源码快照差异，不能声称所有文件字节一致。两种宏的68,014像素PASS仍有效，新增锁保护由实际ESP32构建/运行验证。

### 22:00 EDT 诊断内存修订、外侧裁剪结果与行裁剪候选

1d63e15710e6cdfd237955c57cb00f6be5804764ff5ce442270128b015d3c4de虽构建成功，但实机初始化LCD时内部bounce buffer分配失败（BOARD_BEGIN），未完成组件/性能。失败原始日志outer-perf-window-20260908-2143；100秒监控结束finally恢复ad8a并45秒稳定，room100/v19/seat1/cash800/pos0。后续监控遇明确Gridopoly fault/Guru Meditation立即退出并恢复，仍保留设备原文，不伪造设备SELFTEST标记。

14个大型结果槽改到board/renderer ready后PSRAM分配并placement-new，全部打印完析构释放；小profile probe仍内部存放。受控增量diagnostic-incremental-20260908-2147仅替换同路径ino，保留1d63原output、original/patched ino、patch、base-inputs及实际命令。最初2134清单包含11个Arduino自动导出产物，4个随编译变化；正确分类为2123源码依赖全不变，不能声称2134全不变。新e4e23b4671438f381161ba5754ab085b7d7365125048016b97fccb5677ed39eb（2068064bytes/program2067914/RAM155496）通过HWCDC，实机STORAGE33824bytes、internal_free64612/largest31732/psram_free7429008，板/组件正常。

outer-perf-window-20260908-2150完整OFF7→ON7，pure1/component1/perf0/clean1/FIRST FAILURE NONE。OFF Retarget26/max57及Warm24/max57 FAIL；ON AssetsCold/Warm均26/max39 PASS，但Myturn24/max58、Retarget25/max58仍FAIL，其余ON26/max39 PASS。外侧裁剪有效证据为Assets遮罩生成峰值从约3ms降至常态115us、miss约9降至3，不能因一个最长间隔变化单独断言所有收益。ON Retarget峰值bg8779us/border11703/mask_calc2100/14miss，最慢rect area178,302–245,371/r8/bw1/opa200约2849us；聚合仍跨last-flush边界。flush回调区域全部是全屏，不能当作真实细粒度invareas。draw-analysis.json和完整DRAW行保留。finally已恢复ad8a、45秒稳定并释放，未部署7b82。

尝试SPLIT_LIMIT50→96在完整native像素case23149/radius1/outline场景FAIL（oldbc89/newbc69），未构建设备版本，源码已撤回50。主任务证明大圆镜像循环会额外计算上下两行均被clip裁掉的遮罩（代表clip mask_apply132→70、129→65仍像素一致）。当前行裁剪仅跳过完全不可见镜像行、收紧单侧blend及四角循环边界；背景新增路径仅无gradient时启用，保留dither语义。68,014组真实LVGL像素对照PASS，参考固定ROW_CLIP_ENABLED=0且SPLIT仍50，pixel-rowclip-20260908-2200.log。

新诊断两组均outer cull=1、SPLIT50，仅row_clip OFF/ON（setter写在LVGL锁内），完整七场景ON原门槛不变。diagnostic-rowclip-incremental-20260908-2159以e4e为基线、仅同路径ino/rect.c变化、2122其余源依赖校验，独立output/manifest；normal-rowclip-incremental-20260908-2200以7b82为基线仅rect.c变更、2123其它依赖，独立output/manifest。两构建进行中，均非fresh，不覆盖旧候选output；只有实机ON全门槛PASS后部署新normal。当前设备仍ad8a正常，≥24平均FPS已达，但完整性能目标继续未完成。

### 22:12 EDT 行裁剪六项通过，剩余重定向与大圆缓存候选

rowclip-perf-window-20260908-2202实测d5ff9a97b0f6bbb80f6c0da242d76b735e4090fb163025fa20e9af3410684662，pure1/component1/clean1，但perf0。ON WAIT26/max39、REV26/max39、MYTURN26/max39、RETARGET24/max58 FAIL、SWIPE28/max39、AssetsCold26/max39、AssetsWarm26/max39；原首帧/覆盖门槛保留。OFF Myturn24/max57、Retarget24/max57，资产两项已受此前outer cull改善均26/max39。行裁剪仅剩Retarget未过，本次不能标全性能通过。正常行裁剪787c9183f305beb600b604c737f556242727fcbe99591a7284ec012512714e69（1890544bytes/program1890402/RAM124568）构建及2123依赖校验通过，但未部署。finally ad8a45秒稳定/串口释放。

ON Retarget峰值mask_calc4174us/14miss、border10711/bg8357，继续跟踪大圆与小卡片混存4-entry导致的重复计算。候选仅mask.c新增4槽大圆专用池，radius128..256、payload上限6168bytes，小描述符固定；其余半径走原池；全部槽active且无匹配时回退原池/既有动态路径。参数引用计数/原AA生成算法不变，每刷新cleanup同时释放两池，GC开启构建禁用额外池。SelfTest setter在LVGL锁内切换；两组均outer/row裁剪ON、SPLIT50，只比较大圆池OFF/ON。

68,014实际LVGL像素对照及生命周期补强通过：pixel-largecache-lifetime-20260908-2211.log。涵盖共享209引用、释放204槽替换220且其它active参数输出不变、仍持有209时新228全满回退、范围端点128/256、范围外257和小半径1/8、payload限额、32轮双cleanup。LV_MEM_CUSTOM为malloc，lv_mem_monitor无法给真实allocator used/free；报告仅声称像素/引用行为、descriptor payload上限及代码释放检查，不伪造堆量测。

diagnostic-largecache-incremental-20260908-2209以d5ff为基线仅ino/mask.c变动，2122其他源依赖；normal-largecache-incremental-20260908-2209以787c为基线仅mask.c变动，2123其他依赖。两份受控同路径增量均保留原文件/补丁/基线output与新manifest，构建进行中。当前线上仍ad8a，只有下一完整ON七场景全门槛通过才部署正常候选。


### 22:22 EDT 大圆缓存实测与单独 core0 候选

largecache-perf-window-20260908-2213 实测 a73e94494750d637c657e53295503353d3d8eeb8bd5ac8f82a8786193c6fb7a6：pure1/component1/perf0/clean1。ON 六项均26FPS/max39ms PASS，RETARGET26FPS/first18ms/max57ms FAIL；OFF Retarget24FPS/max58ms。大圆缓存使峰值miss降至8仍不足，不能部署正常b53e31f2bb8f1ae6befbc132a734904575862020ff2c3ed794922e5c3d8d7250。完整DRAW与draw-analysis.json保留，计时包含抢占且按完成flush边界聚合，不能把阶段总时简单当成纯运算成本。

finally恢复ad8a并45秒观察完成，最新原日志配对room993580100/seat1/session3661319480，IDENTITY version30/revision26/phase3/stage6，至ms45038仍status3/IP10.42.0.37。此为身份消息字段；不沿用较早phase0/v25，也不把identity.phase直接当成游戏state.phase。此次没有注入游戏操作。

主任务明确授权仅将LVGL_PORT_TASK_CORE改0，优先级2、RGB初始化路径、PCLK16MHz、bounce40、mode3和frame-ticket保持。静态绑核在创建任务时生效，没有运行中迁移/销毁任务。已有a73e是core1跨启动基线，新固件仍保留原先完整缓存OFF七项和ON七项；最终验收取ON原门槛。不能称core绑核为同启动A/B，暂不叠加圆环图片缓存。

diagnostic-core0-incremental-20260908-2224 与 normal-core0-incremental-20260908-2224 分别以a73e/b53e为产物基线，只替换同路径lvgl_v8_port.h，各2123其他源/配置依赖列入清单；原output、原头文件、补丁及实际命令保留。属于受控增量，历史runs/inputs.json不是修改后快照。构建进行中；七项全部通过才部署正常候选并检查真实WiFi、头像组件和至少45秒稳定，否则恢复ad8a继续定位。


### 22:27 EDT core0 完整性能通过，正常固件已部署

仅绑核变更的受控增量已完成。SelfTest 545580fdd0f3c445eb6fd0e522f303323a513976a2b81d4bb10a96f846eb6389（2068560bytes/program2068414/RAM155608）和 normal c9b74814c21b751d00bd9dfdef4c9765f9087270a11006a820e040168a30e0c3（1890624bytes/program1890482/RAM124680）各2123未变源/配置依赖、变更头after SHA及基线app均重新校验通过。CLI compile均exit0；首次后置verify因Windows PowerShell5误读无BOM脚本内中文路径失败，单独在PowerShell7执行verify-build-output已PASS，两result.json如实记录，helper随后补UTF8 BOM；不是固件构建失败，也没有为此重编译。

core0-perf-window-20260908-2223实际烧录、自检、finally恢复ad8a并45秒稳定全部完成。SELFTEST SUMMARY pure=1/component=1/perf=1/clean_before=1/clean_after=1，FIRST FAILURE NONE，SELFTEST PASS。最终大圆缓存ON结果如下，全部沿用既有FPS、首帧、最大间隔、覆盖和增量刷新门槛：

| 场景 | FPS | 首帧ms | 最大间隔ms | 覆盖ms | 结果 |
| --- | ---: | ---: | ---: | ---: | --- |
| WAIT_FWD | 31 | 14 | 39 | 225 | PASS |
| WAIT_WRAP_REV | 36 | 14 | 39 | 244 | PASS |
| MYTURN_5 | 26 | 34 | 39 | 225 | PASS |
| RETARGET | 28 | 15 | 39 | 320 | PASS |
| SWIPE_EVENT | 32 | 17 | 39 | 247 | PASS |
| ASSETS_SCROLL_COLD | 28 | 18 | 39 | 553 | PASS |
| ASSETS_SCROLL_WARM | 26 | 31 | 39 | 528 | PASS |

同一次core0启动的缓存OFF七场景也全部通过（26–32FPS/max39ms）。绑核收益的对照来自上一启动a73e/core1的同样ON suite（Retarget26FPS/max57ms）与本次core0；这是跨启动对照，不是同启动改核实验。代码创建时固定core0，未运行中迁移任务。结果支持该实现满足现有测试门槛，不能仅凭阶段计时宣称已经实测证明某一次RGB ISR抢占的精确因果或长期任意负载保证。

按主任务授权，随后实际部署正常c9b748；normal-core0-window-20260908-2226完整upload Hash verified/exit0及45.037秒COM7采集保留，打开epoch1788920754278、关闭1788920799315并释放串口。设备build Sep8 22:22:08，约ms1919取得IP10.42.0.37，配对serveradaf8743/room993580100/seat1/session1617783228；SNAPSHOT v34/active1/players4/game phase1/cash800/position0，IDENTITY revision30/phase3/stage6分开记录。corner-central-launch实际下载32768bytes ready，至ms45024保持status3，heap29276/largest15348；本段未见panic/reset/fault、下载失败或setSocketOption/Bad file number错误。

当前正常游戏页面没有进入头像编辑，不能把本段地图图片下载和空闲连接等同30个头像组件再次真实下载，未制造头像/游戏动作来补证据。新core0纯逻辑与组件测试包含既有旋钮顺序/边界/预览相关检查；用户在ad8a上已明确确认的每格一步/方向/反向回原选项，以及已完成的UDP40秒丢包和单站WiFi恢复，仍保留原版本和场景界限，不要求重复物理操作或网络中断。此次新正常固件已包含累计绘制优化、socket调用顺序修正、输入/预览/恢复修复及core0，设备没有停留SelfTest或旧ad8a。

本端本轮完整性能门槛与正常部署/启动观察已通过，无未完成构建或占用COM7窗口。主任务负责最后权威HTTP只读对照、统一Git提交及整体交付；任何此前独立的真实移动/目标灯验收仍按协调文档自己的证据判断，本次性能测试不替代跨端物理游戏验收。


22:28 EDT主任务完成正常部署前后的权威HTTP只读对照，原始core0-root-authority-before.json和core0-root-authority-after.json均保存在工具目录。后快照v34/gamephase1/identityphase3/P1在线，头像/名字/ready mask均15；主任务核对room/phase/round/active/decision/assets/debt/auction/card/movement/forcedRoll及全部玩家id/name/cash/pos/held/bankrupt/identityFlags/avatarURL/tagUID均无业务变化。连接造成的version/identity revision变化单独保留，不视作游戏状态变化。主任务明确无需当前普通页面重新加载30头像组件或再次物理旋钮操作，保留本次验收范围即可；剩余为主任务统一Git和整体交付。


### 2026-09-09 频闪/错位现场回归：撤回 core0 发布，视觉验收重新打开

用户反馈已部署c9b748出现频闪和画面错位。主任务提供的现场图片codex-clipboard-2291fbcd-2a50-467c-9d99-174f8ae01e2e.png已实际读取：YourTurn页面外圈/内容偏移且底部残留上端元素，静态图支持错位事实，频闪由用户反馈，不能由单张照片推算频闪频率。昨日七项帧率、自检和45秒串口稳定并不证明视觉无误；c9b748的性能PASS仍是当时量测事实，但撤回其可用发布和整体视觉验收结论。

COM7 visual-fault-before-20260909-2233先30.019秒无reset只读采集，打开epoch1789007568813、关闭1789007598832。首批含旧USB片段，当前设备ms约86812719..86844130（约24小时），反复reason201/36、status6/IP0/ap_result12303；recoverWifi进入后WiFi.reconnect返回requested0/1ms并有sta-is-connecting错误。没有本次新build行或flash读取，当前c9b748依据上次已核验部署链，不能冒称此次无reset日志重读了完整app hash。主任务旧服务器地址访问超时，独立服务器任务只读排查AP/地址；不把此离线故障直接归因core0。

官方ESP-IDF v5.5.2 RGB LCD文档Bounce Buffer章节明确说明：两核同时经cache访问PSRAM会延长DMA EOF中断内拷贝、错过bounce切换，引发screen shift；VSYNC重启仍可能出现flicker。来源：https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html#bounce-buffer-with-single-psram-frame-buffer 。这是与本次跨核变更及现场症状吻合的机制线索，仍需回退后用户视觉对照，未采硬件underflow计数就不能宣称逐事件因果已证实。实际当前sdkconfig CONFIG_ARDUINO_RUNNING_CORE=1、CONFIG_LCD_RGB_RESTART_IN_VSYNC=y、RGB_ISR_IRAM_SAFE关闭、SPIRAM_FETCH_INSTRUCTIONS/RODATA关闭；实际BusRGB flags.bb_invalidate_cache=0。没有重复开启restart或启用跨核cache失效来冒充修复。

最小修复仅lvgl_v8_port.h恢复LVGL_PORT_TASK_CORE=ARDUINO_RUNNING_CORE（本板1，IDF fallback0），补充同核避免争用bounce refill的注释。优先级2、PCLK16MHz、40行bounce、mode3/direct双缓冲和frame-ticket回执屏障不变。复用已经构建验证的正常b53e31f2bb8f1ae6befbc132a734904575862020ff2c3ed794922e5c3d8d7250（1890624bytes），保留全部输入/头像预览、恢复、绘制优化及socket选项顺序修复；没有部署更老且缺功能的ded137。

visual-core1-window-20260909-2234实际upload Hash verified/exit0，启动build Sep8 22:11:16；45.101秒串口观察（epoch1789007678784..1789007723885）完成并释放。到ms46511仍反复reason201/36、status6/IP0，heap约32016/largest22516，无panic/意外reset；没有拿到新权威快照，因此不能写恢复了原房间联网。source-equivalence.json及原/当前头文件保留：去注释并排除只在SELF_TEST内的旧include差异后，当前头与b53e头token完全相同；此次是复用已验证产物，未新构建。

当前设备为b53e core1正常固件。该实现对应此前a73e诊断ON六项26FPS/max39ms PASS，Retarget26FPS/max57ms FAIL；c9b748/core0的七项全PASS不能移植为b53e性能结论。视觉回退效果待主任务协调用户观察；原对局场景需要AP/服务器恢复后再核对，离线等待页初步正常不能等同原场景通过。未注入游戏、改变Tag/席位/现金或创建自动化。本端继续持有视觉修复和剩余性能问题，主任务持有用户视觉协调与Git，服务器任务持有当前网络可达性排查。
