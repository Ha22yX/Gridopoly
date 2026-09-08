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
