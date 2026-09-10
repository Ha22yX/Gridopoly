# 玩家屏幕端开发进度 · 2026-09-07

## 当前状态

恢复开发已获得授权。实际目录 `Z:/Files/我的项目/Gridopoly`，玩家屏 COM7。
截至本次记录：Action 17 实现已落盘，24 项原生行为检查通过；三模式均构建通过，
但 COM7 设备 SelfTest 失败，已恢复旧固件，尚未部署正式 Action17 候选，
不能宣称联合时序通过。最新 Wi-Fi 构建 1920343B / RAM 121724B，
ESP-NOW 1946547B / RAM 121228B，SelfTest 2094175B / RAM 151340B。
COM7 最初进行一次串口身份读取。记录识别为 Viewe UEDX48480021-MD80ET、
设备 `02d9b4dc`、anti-tearing mode 3 / rotation 0。
尽管 DTR/RTS 设为 false，该次打开仍捕获 USB_UART_CHIP_RESET；已告知主会话，
后续串口/烧录只在协调窗口执行，避免反复打开。
主会话随后授权 COM7 备份、设备 SelfTest、通过后正式固件烧录窗口。esptool 确认
ESP32-S3 rev0.2 / 8MB PSRAM / MAC dc:b4:d9:02:d1:dc；stub 模式两次读闪存中断，
未写入固件，改用官方支持的无 stub ROM 路径读取完整 16MB，1198.6 秒后成功。
备份路径 `LocalAppData/GridopolyPlayerTools/backup-com7-20260907/old-full-flash-16mb.bin`，
SHA-256 `07233EA9BE522D82AA9ADEFB59498960321265B75DEF45725E67551CFE58054B`。
旧 app0 镜像 checksum / validation hash 均有效；旧与候选的 6 个分区类型、
offset、size、flags 完全一致。旧镜像 footer 显示 IDF v5.5.5，本轮为 IDF5.5，
这是工具链差异，设备验证仍是必需项。

## 本轮实现

- App 独立维护 room/version/origin/target、首帧状态、请求和回执。普通命令优先出队，
  movement cue 不占普通 pending，不会阻塞手动到达。
- 同一移动的重同步保留首帧凭据；未确认时遇到新 version 使用新 request，
  已确认的同一移动不因 version 更新重放。新房间、目标或阶段结束清理。
- 共享 transport 使用现有 Action 17，assetIndex=0xFF、argument=target、精确非零 version；
  重发缓存的原始内层帧，Wi-Fi UDP 封装每次生成新包序号。会话恢复保留未完成 cue。
- 成功 ActionResult 按原 wire sequence 结束，不等待游戏版本推进。普通动作独立。
- 首包丢失后较新心跳可能越过内层序号。至少两次原帧发送后收到带较新 ack 的
  Resync 才开启恢复探测；再成功发送同一原帧且继续收到 Resync 后，才以本地
  RetryRequested 退役旧逻辑请求。App 保留首帧，450ms 后建立新 requestId/内层序号。
  单个普通 Resync 不触发续期；缓存成功回执优先终止探测。负 ActionResult 则等
  认证全量同步后再续期。该恢复只用于同一移动的幂等 Action 17，不改服务端 wire。
- Demo 分支与传输行为测试已补齐。
- 不再在 uiRendererRender 返回后立即宣称呈现。生产 direct-mode 双缓冲在 vendor
  framebuffer switch 完成等待后登记 ticket，再跨过两个 vendor frame-complete 回调确认完整扫描（IDF 5.5 的回调表示 DMA 发送完成，不能直接等同 VSYNC）；
  页面 ticket 匹配后才进入 appNotifyFramePresented。
- `GRIDOPOLY_CUE presented/send/acknowledged` 提供关联日志；启动输出 Action 17 构建标记。

## 原有改动验证与必要修复

保留既有 40 行 RGB bounce、35ms 旋钮反向毛刺过滤、单步队列、头像双预览缓冲、
2304KiB Setup 临时池、45ms 合成等待和后台手写 worker。

首次正式构建暴露原有手写代码将 `UiHandwritingSample.connectsPrevious` 错写为
`connectToPrevious`，本轮修正。静态素材检查还停留在旧 2MiB 单预览池，已更新到
现有双预览池；布局检查和负向测试适配保留式 Avatar 渲染及新首帧包装函数。
新增 ISR 首帧计数初用 std::atomic，但 Xtensa 编译器不声明 always_lock_free，
已改用 FreeRTOS 的短临界区，不在 ISR 调用潜在锁实现。
另对 captureGeneration 的触摸端递增加同一状态互斥锁，与 worker 发布结果同步。
SelfTest 链接定位内部 DRAM 超限 14232B：新增传输夹具约 29KB，加上既有大量
常驻测试状态挤占内部内存。新增 `TestFixture<T>`，将传输夹具及 37 个既有
AppState/DemoTransport 测试对象改为作用域 PSRAM 存储，结束自动析构释放；
分配失败会明确使测试失败。此调整只涉及测试对象，给随后物理 RGB DMA 留出内存。

## 当前验证

实机失败候选保存在 `LocalAppData/GridopolyPlayerTools/candidates/selftest-20260907-2248`，
应用 bin SHA-256 `3FAC4A575606744D1AE4BA63894210F7B307C5E864D4D63D764A1CA9B2C3E69D`。
写入 hash 校验通过；组件阶段到达等待/确认文本的两条断言失败，随后 pure 阶段出现
StoreProhibited / double exception。准确 ELF 发现 `runPureLogicTests` 栈帧 15808B
直接调用 `runIdentityLifecycleTests` 栈帧 21392B，总计已超实际 32768B loop 栈。
已拆分协议投影测试为 noinline helper，保留 32KB 栈并加入 highwater 诊断；
到达文本断言不放宽，正在增加实际 label text/大小/long_mode 诊断定位。
旧 boot/分区/OTA/app0 按备份原字节恢复且写 hash 校验通过；20 秒单次串口观察
重新 ready、IP 10.42.0.37、发现原 room 993580098，无 panic；未捕获 paired 行，
故还不能据此声称 seat 重连已确认。

第二轮 `selftest-20260907-2258`（bin SHA-256
`530FD22163D404A1E2B7FFB5B7AF1F43510814AF1061FBBA31B6C81ED689FF7E`）
已完整运行到 SUMMARY，无 panic，`after_pure highwater=6796`。新 ELF 的
runPure 栈帧 2512B，协议 helper 15408B，identity 21392B，消除了原累计超限。
组件诊断按钮文本正确，最后状态标签在布局前仍为 0×0 且文本 `...`；测试现先
执行真实 flush 前必经的 `lv_obj_update_layout`，然后保留原完整文本断言。
pure 的首失败为旧 2MiB Setup 容量断言，现同步已批准 2304KiB，并约束双预览加
组件总大小。第二轮物理性能：Waiting 正反向及 Swipe 26FPS/max39ms 通过；
MyTurn 21FPS/max58ms、Retarget 23FPS/max57ms、资产列表冷/热 20FPS/max58ms 未通过。
没有降低性能门槛，也没有部署正式候选。第二轮结束后再次恢复原备份，写 hash 校验通过。

主会话从旧 build.options.json 确认原构建工具链为 esp32 core 3.3.11 / -Os。
已在完全分开的 `LocalAppData/GridopolyPlayerTools-3311` 官方安装 3.3.11，
保持源码、FQBN、mode3、40 行 bounce 和优化参数不变，三模式正在构建以受控对比。
共享目录旧 bin 的 hash 与 COM7 备份不相同，不能把共享旧产物当作设备的准确固件。

- 原生 x86 MSVC 状态机/首帧 ticket 检查：24/24 PASS，临时夹具调整后再次通过。
- 字形检查 PASS；布局检查 PASS；布局负向破坏测试 PASS。
- 棋盘/头像素材静态检查 PASS；SelfTest runner 回归 PASS。
- 第二轮真实 ESP32 执行的 Action17 子集：App/首帧状态机 24/24 PASS，
  transport fixture 17/17 PASS，已从完整设备日志逐条核对。该传输夹具模拟丢包，
  不等于真实骰子 → LCD → UDP → LED 联合链已验收；完整 SelfTest 仍未通过。
- 硬件显示、旋钮手感、Avatar 动画与实际手写性能仍需完整设备验收。

## 可复现工具链与产物

本机独立目录 `C:/Users/kicof/AppData/Local/GridopolyPlayerTools`，不提交缓存、凭据、
构建产物或第三方库。

- Arduino CLI 1.3.1
- esp32:esp32 3.3.0 / ESP-IDF 5.5（匹配当前 esp_now_send_info_t 接口）
- ESP32_Display_Panel 1.0.3（含当前 Viewe 板型）
- ESP32_IO_Expander 1.0.1，esp-lib-utils 0.1.2
- ESP32_Button 0.0.1，ESP32_Knob 0.0.1
- 项目自带 LVGL 8.4.0

上游参考：[Display Panel Arduino 环境](https://github.com/esp-arduino-libs/ESP32_Display_Panel/blob/master/docs/envs/use_with_arduino.md)、
[Button](https://github.com/esp-arduino-libs/ESP32_Button)、
[Knob](https://github.com/esp-arduino-libs/ESP32_Knob)。历史工作区未保存外部库完整锁定清单，
这组版本是根据现有接口与板型建立的可复现本轮环境，不声称与旧开发机完全一致。

脚本：`Firmware/PlayerConsole/tools/compile-isolated.ps1 -Mode production|selftest|espnow`，
`test-movement-cue-app.ps1`。三种构建各自拥有本机 staging/build/output，不删除共用 build。
Git 暂存/提交由主会话统一串行操作，本会话没有 Git 写操作。

## 2026-09-08 用户屏幕可用性恢复

用户报告屏幕停在 My assets。已确认设备仍运行第三轮
`selftest-20260907-2334`，最后为 `SELFTEST FAILED`；其资产列表性能测试
是最后一个画面，并非生产界面卡死。停止后续 SelfTest 烧录；最新 profiling
候选仅离线保存，未上传。恢复前未发现正在烧录或采集串口的进程。

恢复前重新核验完整旧备份 SHA-256
`07233EA9BE522D82AA9ADEFB59498960321265B75DEF45725E67551CFE58054B`，
并逐字节核验 boot/partition 与 ota/app0 切片。COM7 两段恢复写入均通过
设备 hash 校验；NVS、app1 和文件系统未覆盖。

恢复后单次 45 秒被动日志确认生产 `Player Console ready; mechanical=60 firmware=0`，
Wi-Fi/UDP 地址 `10.42.0.37:4242`、原服务器 `adaf8743`、原房间 `993580098`；
权威快照 `version=183 seat=1 active=1 players=4` 及 AUTH/ROSTER 下发，席位 1
恢复。监听期间设备记录实体 INPUT 与 action=1 成功结果；本会话没有注入输入、
房间管理请求或 Tag/资产操作。串口现已关闭并释放。

日志位于本机备份目录：`restore-user-availability-20260908.log` 和
`restored-user-availability-20260908-device.log`。完整 SelfTest 仍未通过，
新正式候选未部署。后续构建与性能定位仅离线；下一次会占用用户屏幕的实机测试
由主会话先协调具体空闲窗口。

## 2026-09-08 离线收敛与性能定位

旧生产固件恢复后，继续开发而不占用 COM7。第三轮旧候选的 pure 首失败
`demo debt batch completes with selected mask and transaction` 源于新增
TransportCommand.roomId 插入成员中间，改变了旧聚合初始化的位置含义。
现将本地元数据移至结构末尾，并增加旧 MortgageBatch 聚合字段回归；序列化仍
逐字段编码，不增加协议线字段。最新离线审查另补 LCD 切换失败保护：仅当
`switchFrameBufferTo` 返回 true，首帧跟踪器才登记新缓冲区；失败时即使后续
硬件回调继续到来，也不能确认新页面。原生共享状态机测试现为 26/26 PASS。

显示性能已增加 SelfTest 专用 submit/wait 微秒计时，记录在已有 DIAG 中；
生产编译不包含该计时。该版本尚未获得新的设备测试窗口，不能声称性能已修复。
新增 `tools/analyze-frame-timing.py` 只读取保存的串口日志，保留原 PASS/FAIL，
旧日志缺失的时间分解保留为不可用，不当作零。

离线证据：Viewe UEDX48480021 MD80ET 的当前板型配置为 16MHz PCLK，
水平总长 548、垂直总长 558，配置推算扫描周期 19.1115ms。第三轮日志中：

| 场景 | 原结果 | 稳态约两次扫描间隔 | 稳态约三次扫描间隔 |
| --- | --- | --- | --- |
| Waiting forward | PASS | 5 | 0 |
| Waiting reverse/wrap | PASS | 5 | 0 |
| MyTurn 5 | FAIL | 3 | 1 |
| Retarget | FAIL | 4 | 2 |
| Swipe | PASS | 5 | 0 |
| Assets cold | FAIL | 5 | 5 |
| Assets warm | FAIL | 5 | 5 |

这是按配置周期对已有日志的估算，不是独立测得的 VSYNC。LVGL 8.4
`_lv_disp_refr_timer` 的 monitor 回调计时涵盖布局、双缓冲同步、绘制和 flush
阻塞等待，因此现有 renderMs 不能证明纯 CPU 绘制耗时。性能场景直接调用
uiRendererRender，不走 renderAppFrame 的首帧 ticket 路径；新增两次回调
观察不额外阻塞这些性能场景。

已核对 [ESP-IDF 5.5.5 RGB 驱动源码](https://raw.githubusercontent.com/espressif/esp-idf/v5.5.5/components/esp_lcd/rgb/esp_lcd_panel_rgb.c)：
bounce 模式每填满一帧就更新 bb_fb_index 并调用 on_frame_buf_complete；
该回调表示内存缓冲区可复用，不等于最后像素已物理扫描。保留额外帧边界
观察，也保留原有防撕裂同步。当前不调整 PCLK、bounce 行数、优化等级或
性能门槛，待新窗口获得 submit/wait 分解后再决定优化位置。

本轮离线检查：26 项原生状态机 PASS、布局负向测试 PASS、101 字形检查 PASS、
素材预算检查 PASS、SelfTest runner 回归 PASS、Git diff 空白检查 PASS。
计时解析器已解析两个核心版本的真实旧日志，并核对新格式和不完整日志处理。
完整 SelfTest、物理显示时序、旋钮/头像/手写手感、骰子→LCD→UDP→格子灯
整链仍为未验收；不将离线构建通过等同于这些验收通过。

最终离线构建已全部通过（esp32 core 3.3.11、默认 -Os、HWCDC 检查 PASS），
包含上面的切换失败保护。以下为编译器统计与归档 app bin 实际文件 SHA-256：

| 模式 | 编译器程序字节 | 静态 RAM 字节 | app bin SHA-256 |
| --- | --- | --- | --- |
| Wi-Fi production | 1885354 | 123352 | `878e3809ca1527a47ea4f1160c008e842eb87d6422dcf1135adc6955ec7e8697` |
| ESP-NOW | 1910350 | 122872 | `e30f861f948a717109ce8767fc678c015c33759341c268c677bcc381cb840be5` |
| SelfTest | 2059658 | 154016 | `98ee4288cdd50aa3000ba0938ac30839d1e6fbc339c3f58d83dbef5b7259a9a7` |

归档在本机 `GridopolyPlayerTools-3311/candidates/offline-review-20260908`，
每模式保留 elf/map/非 merged bin/build.log；artifacts.json 记录实际文件长度
和逐文件校验值。这三份最终候选均未烧录，COM7 保持已恢复的旧生产固件。

### 构建脚本审核修正（业务源码检查点 1481e61 之后）

`compile-isolated.ps1` 不再覆盖旧 source 目录。每次调用在 ToolRoot/runs 下
创建带模式、时间和 GUID 的独立 PlayerConsole 源码快照及 output；删除或
重命名的旧源码不会残留到新快照。脚本不执行删除操作，也不清理其他模式或
已归档产物。默认 ToolRoot 改为 GridopolyPlayerTools-3311，并通过 CLI
实际配置和已安装核心列表校验 esp32:esp32@3.3.11。

编译前检查 CLI、配置、构建验证脚本、主 sketch、三份仓库库和五份外部库；
GridopolyCore/GridopolyProtocol/lvgl 明确从当前仓库 Firmware/libraries
读取，ESP32_Display_Panel/IO_Expander/esp-lib-utils/Button/Knob 从 CLI
配置的 user/libraries 读取，不再使用 ToolRoot/libraries 中的旧复制件。
已逐文件比对本轮构建使用的复制件与仓库：Core 7、Protocol 6、LVGL 1215
个文件全部一致，因此库来源修正没有引入本轮构建之外的源码版本变化。

默认 build 路径也位于独立 run 中。仅显式传入 `-ReuseBuildCache` 才复用
对应模式的 build-MODE，并持有独占文件锁防止同模式并发写缓存；输出仍独立。
编译与 HWCDC 验证成功才返回成功，失败不调用后续验证。每次输入清单
inputs.json 记录源码、库来源、外部版本、核心版本、flags、build/output
及缓存选项。控制台打印该次准确输出路径，不再默认覆盖 ToolRoot/production、
ToolRoot/selftest 或 ToolRoot/espnow。

只用本机模拟 CLI/微型仓库验证脚本逻辑，9 项通过：默认工具链定位、删除/
改名不残留、库与产物路径、显式缓存/模式 flags、缓存并发排斥、错误核心拒绝、
缺配置拒绝、编译失败传播且不调用验证、旧快照保留。PowerShell 语法解析通过。
验证记录在 GridopolyPlayerTools-3311/script-tests-20260908/result.log。
没有因此重编业务源码，没有操作 COM7；先前归档三模式候选和硬件未验收结论不变。

## 2026-09-08 正常候选首次功能窗口与 Wi-Fi 阻断

主会话明确授权正常 production 候选功能验收，取消此前等待新窗口的限制。
归档候选 `878e3809...e8697` 与已提交业务源码一致；上传前核验所有产物 hash、
六个实际分区条目以及完整原备份/恢复切片。未重复读取 16MB Flash。

第一次正常候选上传主机时间为 EDT 00:54:30–00:54:51，设备 hash 校验通过。
COM7 记录构建标记 `movement_cue=17 compiled=Sep 8 2026 00:25:50`、原设备
`02d9b4dc` 与 Player Console ready；但只有 `wifi_begin status=6` 和
`wifi_recover status=6 ip=0.0.0.0`，没有 UDP ready/权威快照。采集于
00:55:44 才打开，五条记录为缓冲批量接收，不能把主机时间间隔当作设备事件
间隔；至 00:57:07 结束采集约 83 秒未见后继日志。

按失败即恢复要求，EDT 00:57:07–00:57:33 恢复旧生产固件，两段写入均
hash verified，保留 NVS 与原备份。随后单次 40 秒监听确认旧 ready、
UDP `10.42.0.37:4242`、原 server `adaf8743` / room `993580098`，并释放串口。
该次没有捕获新的 seat 快照。此时设备再次为旧生产，不含 Action17；恢复可用
不等于新功能已修复或部署。窗口前现场 version=186 / phase=5 / P1 position=6，
cue gate inactive，没有可触发新首帧回执的 pending move；本会话未注入 roll、
修改房间、绑定或资产。

服务端同步 AP 证据显示候选运行期间 00:55:25 已完成 WPA 握手和 DHCP ACK，
恢复旧版后 00:57:57 也完成连接；ACK 不证明客户端已处理 GOT_IP。SSID/密码
字面值均同时存在于新旧 bin，候选无 fallback 密码。当前私有配置 channel=1、
AP 实际为 3，但 IDF 5.5.5 STA 配置明确 channel 是起始扫描提示，不是强制
单信道约束，因此不能断定它是唯一根因。精确停止点尚未被旧日志证明；恢复日志
后依次为 udp.stop、WiFi.mode(WIFI_OFF)、delay、beginWifi，其中旧候选未出现
后续 begin 日志。

继续执行已获授权的最小 Wi-Fi STA 修复：channel=0；取消恢复路径的全栈
OFF/ON 和固定 delay，改为标准 reconnect 请求；在请求前更新时间，仍每 30 秒
至多一次。事件回调只写固定 8 项队列，不打印、不分配；主循环打印事件原因、
关联信道、设备 millis 和丢失计数，并每 5 秒记录客户端状态/IP、SDK关联结果、
内部 RAM。begin/recover 各调用边界记录 millis。ESP-NOW/服务端鉴权未改。
下一轮采集脚本在打开/成功打开/关闭时分别持久化主机时间，上传后立即附加串口。

主机验证直接提取当前 beginWifi/recoverWifi 原函数，替换驱动接口后编译执行，
覆盖 active STA 重连、失败更新时间、UDP关闭顺序、NULL STA 无偏好初始化、
mode初始化失败返回，全部 PASS；该验证不证明真实驱动不会阻塞。复现材料在
`GridopolyPlayerTools-3311/wifi-recovery-tests/recovery.cpp`、build.cmd、result.log。
生产构建进行中，后续负责人仍是玩家屏会话：构建通过后按持续有效的窗口授权
核验新 hash/分区并再次部署；若失败恢复并继续定位，若正常联网保持候选运行。
Action17/灯/自动到达仍待真实游戏与物理Tag证据；四个动画性能 FAIL 尚未证明
属于功能链阻断，保留为独立未达门槛项，不能据此宣称可正式发布。

### 第二次正常候选已实际部署并恢复权威通信

Wi-Fi 修复后的 production 构建 PASS：程序 1886866 字节、静态 RAM 123472，
HWCDC 检查通过。实际 app bin 1887008 字节，SHA-256
`ded1375634d29cc454ce401dd7e1da56373e91055d2bd464a865bee3d3c31a8c`。
构建标记 `movement_cue=17 compiled=Sep 8 2026 01:09:44`。产物位于
`GridopolyPlayerTools-3311/runs/production-20260908-010418-7ae190304983448a821cdf02fc35ae05/output`，
同级 artifacts.json 记录非 merged 产物、ELF/map 的校验值。此前三模式验证
属于 Wi-Fi 诊断修复之前；本次修改后验证的是 production，不冒称又跑过三模式。

第二次上传 EDT 01:15:01.593–01:15:24.630，设备写入 hash verified，
采集 opened=01:15:24.897。device ms=506 开始STA、567开始channel0连接、
610返回；前13秒有 reason=2(AUTH_EXPIRE)重试，ms13097 CONNECTED/channel3，
ms14117 GOT_IP，随后 `UDP ready ip=10.42.0.37 port=4242`、
`paired seat=1 room=993580098 session=4088196611`，权威
`SNAPSHOT version=192 seat=1 active=1 players=4 phase=5 cash=122 position=6`
与 AUTH/ROSTER 完整同步。设备ms15089至155203持续status3/IP有效，主循环
进度与堆诊断持续，无panic。服务器同时观察station连续在线、接收字节增加、
authFailures/replayDrops为0，支持认证通信恢复。

记录目录 `GridopolyPlayerTools-3311/functional-window-20260908-0114` 包含
upload-start/end、upload.log、candidate.json、player.log、player.jsonl、
capture-events.jsonl。设备ms和主机接收epoch分列，不能用串口接收时间替代
物理呈现时间。AP约01:15:34因既有FAILED邻居规则驱逐旧station，01:15:36
重关联/握手/DHCP，与新设备ms13097/14117事件吻合；不能仅据本次成功宣称
首次OFF停滞的内部根因已证明，也没有实机触发新30秒recover路径。

当前实际运行的是上述包含Action17的新正常候选，保持运行，不回退到旧生产。
旧备份仍保留。当前仍v192/phase5，没有pendingMove，窗口内暂未出现真实
首帧Action17；这不构成功能失败，也不构成端到端通过。主任务已请用户完成
当前付款/筹资，正常掷骰进入MoveGuide，先不按I'M THERE、目标线圈无Tag，
告知目标格后再协调唯一模块与LED→Tag验证。无需重插的持续在位轨迹单独验收，
允许直接自动到达并返回cue=none。

本阶段范围：修复STA恢复路径、补诊断、构建并实际部署可认证通信的正常候选；
剩余：真实首帧→Action17成功回执、正确目标模块灯、绑定Tag自动到达与持续在位
重评、手动确认及四项性能门槛。当前功能验收的外部依赖是用户正常游戏和物理Tag
操作；主任务协调下一具体窗口，玩家屏负责COM7证据，服务器/格子端负责对应
权威与实体观测。本会话业务源码冻结供主任务审核提交，不把阶段交付标作整体完成。

第二轮180秒串口窗口已按时结束并释放COM7，最后设备ms180226仍status3、
IP有效、无panic；没有Action17、用户输入或触摸事件，不自动重开窗口。服务器
同期120秒记录239样本/956GET零错，UDP PairRequest/PairAccept各1、Heartbeat/Ack
各59，actionFrames=0，鉴权/重放失败为0。此时设备继续运行新候选
`ded1375634d29cc454ce401dd7e1da56373e91055d2bd464a865bee3d3c31a8c`。
下一可操作节点由主任务等待用户完成当前债务并正常进入MoveGuide后协调；
保留功能和性能未验收清单，不把无移动的短窗口视为功能失败。

## 2026-09-08 19:10 EDT 性能优化基线与重复到达修复（本轮进行中）

用户已授权至少24FPS、必要SelfTest修复及真实断线恢复验证。首个有界COM7窗口记录在本机 GridopolyPlayerTools-3311/perf-window-20260908-1910，使用 SelfTest app 98ee4288cdd50aa3000ba0938ac30839d1e6fbc339c3f58d83dbef5b7259a9a7（DemoTransport，不写在线游戏）。窗口 finally 恢复正常 Action17/WiFi 候选 ded1375634d29cc454ce401dd7e1da56373e91055d2bd464a865bee3d3c31a8c，45秒观测 GOT_IP device ms1950、原room993580098/seat1恢复，权威v214/phase6/cash262/position0，COM7已释放。v212→214不归因为测试注入，服务端断连/重连也会推进版本。

原始自检 component=1、pure=0、perf=0。性能：WAIT_FWD 24FPS/max39ms、WAIT_WRAP_REV 26/max39、MYTURN_5 22/max58、RETARGET 23/max57、SWIPE_EVENT 26/max39、ASSETS_SCROLL_COLD 19/max58、WARM 20/max58。后四项维持FAIL；原42ms间隔、80ms首帧等门槛未放宽。submit大多50–65us，慢帧wait约12–18.7ms，部分刷新除submit/wait外仍约32–41ms，不能简单归因为提交调用。按当前16MHz及水平548/垂直558时序估算扫描周期19.1115ms，38/39与57/58ms约为2/3次扫描，仅是时序估计。

首个pure失败为 duplicate authoritative arrival does not restart or bypass the confirmation。修复 app_state.cpp 的 resyncContinuesCurrentRoll：相同房间/本人回合、MoveGuide已确认到达、同目标位置与同权威phase的resync保留确认截止；位置或phase变化仍重建。新增两个原生回归，修复前能复现同到达resync失败，修复后28/28通过；原完整SelfTest断言不改。尚待本版设备完整自检。

当前实验将 LV_MEMCPY_MEMSET_STD 从0改1，比较平台内存函数，不预设更快；SelfTest新增copy/rect/glyph耗时。c/r/g定义为相邻末次flush完成边界之间累计耗时，不是同条monitor总时间的精确拆分，不用相减伪造逐帧CPU归因。没有调整PCLK、40行bounce、任务核、模式3防撕裂或Action17呈现门槛。实验构建/实测结果随后补录；当前设备仍正常ded137。

### 19:32 EDT 标准内存函数A/B实测

SelfTest app638c27051f96b97922d185afbf635e6135ef0e1c1631c8ff4b217cbbfde2c945（2059520 bytes）在 perf-window-20260908-1932 完成：pure=1/component=1/FIRST FAILURE NONE，证明本次重复到达修复通过完整设备自检；perf=0。MYTURN_5 22FPS/max57ms、RETARGET 23/max57、ASSETS冷20/max57、暖20/max58，标准memcpy未显示足够收益，已将LV_MEMCPY_MEMSET_STD退回0。其余三场景26FPS/max39ms通过。

c一般0–1.7ms，r约12–25ms，g约2–8ms（各字段时间边界见上节），后续优化集中在矩形/圆角边框。正常ded137已自动写回，GOT_IP ms1960，原room/seat、v216 phase6 cash262 position0恢复，45秒status3无panic，窗口结束释放COM7。

### 保守边框裁剪与像素回归（实机性能待测）

唯一共享库变更为lv_draw_sw_rect.c：draw_border_generic在clip与外区求交后，若完整位于inner_area再缩2px的圆角内区则返回；抗锯齿边缘保留原路径。背景、阴影、outline仍由各自绘制步骤执行，不给整个对象提前返回。共享库范围已与主任务协调。

工具test-border-render.ps1/test-border-render.c在ASCII临时快照编译当前LVGL与去除这段优化的参考渲染器。28,523组逐像素一致，覆盖side0..15、0/小/大/CIRCLE半径、宽度0到220、多个opacity、AA开关、背景/阴影/outline、border_post、附加线遮罩、非正方形/小尺寸/屏外坐标和圆弧边缘小clip。此为主机像素正确性，不是设备帧率。原始未修改参考文件和首轮25,523组结果另保留在native-render；可复现脚本结果见pixel-test-repro-20260908.log。本版SelfTest与正常候选正在构建，未部署优化版。

后续compile-isolated脚本同时新建本轮run/libraries快照，记录sourceProjectLibraries与实际projectLibraries，避免依赖扫描反复读网络盘和共享库中途变化；外部板库仍来自已验证CLI配置。10项模拟检查通过，含新库编辑进入新run/旧run保存。当前正在编译的SelfTest仍使用启动时旧脚本输入，没有重启或替换；新production构建才用本地库快照。实际速度尚待该构建完成。

### 19:44 EDT 构建缓存污染发现与修复

193747 SelfTest在链接阶段失败，未上设备：lv_memset_ff/lv_memset未定义。保存的依赖文件确证lv_mem.c.d仍指向192041快照的STD1配置，而新rect.c.d指向193747的STD0配置。此前638二进制实测仍有效，但不将STD1/STD0比较作为可靠的全库单变量结论；上文“未显示足够收益”只指该候选未过门槛。失败日志compile-perf-border-20260908.log保留。

194516 SelfTest改为默认唯一run/build及本轮本地库快照全新编译。compile-isolated进一步把可选ReuseBuildCache按全部sketch/项目库文件的相对路径+内容SHA256、mode/flags/core/外部库版本选择build-mode-fingerprint和同名独占锁；配置/头文件/删除/重命名变化不会复用旧对象，不删旧目录。13项模拟检查通过，新增同输入跨快照复用、lv_conf变化隔离、恢复相同配置返回对应缓存。正常候选验收同样采用新构建；之前并行准备的共享缓存production仍保留作为未验收构建，不用于替代此要求。

### 19:57 EDT Avatar Setup旋钮反馈的正常固件只读采集

用户新增Avatar Setup单格有时连跳问题，主任务暂管输入过滤/Avatar状态路径；玩家屏继续性能验证并负责COM7。根据主任务明确安排，在正常ded137上打开90秒采集，无DTR/RTS、写入或reset。opened hostEpochMs1788911819560，closed1788911909585，90.025秒后释放；目录avatar-input-baseline-20260908-1957。主任务现场room993580099/v9/IdentityAwaitAvatar，不能沿用此前room993580098验收上下文。

旧INPUT行只有kind/delta/page/focus/queued等，没有raw回调时间或recipe。启动瞬间先读到旧progress ms50032/55041/60042再到1436621及输入，说明存在USB积压；不把第一批或同host读取批次作为物理旋转间隔证据。末段host相对79.053/81.787/84.536秒分别读到delta+1/page22/focus1/2/3。是否一次物理格触发多步需用户操作对应与新诊断，不能只凭现有日志定量断言。全程status3，无panic。

当前在编辑未提交头像草稿，主任务要求保护本地draft：窗口结束后不直接烧SelfTest，先由主任务核对用户保存/允许切换。两份既有构建不包含后续输入修复；最终正常候选需合入主任务交接的输入修复后再fresh构建。此限制是现场状态变化下的主任务明确安排，不是取消原性能/恢复目标。

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
