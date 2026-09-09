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
