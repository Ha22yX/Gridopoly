# 新玩家屏静止频闪与偏上排查（2026-10-15）

## 当前结论与范围

用户报告两台新玩家屏静止时仍有频闪、画面错位且偏上，原 COM7 正常；两台新底板均已返修并接着电池。重新打开 COM27/28 的显示验收。原有四段读回校验、45 秒联网和无 panic 结果保留，但不能代表显示通过。

已复现等待新局状态的多余整页重绘：60 次正常发现/拒绝重试触发 120 次整页重建。最小修复在渲染层保留可见内容未变的等待页，实际 reducer 仍接收并处理全部事件；修复后同样序列为 0 次重建、0 个 LVGL 脏区。真实 LVGL 51 项及 setup 83 项主机检查通过。普通候选 `dfc4b7e5` 已在 COM27/28 完成四段写验和各 60 秒观察，两口均已释放。

先只更新 COM27 后，用户明确反馈“更新的那台已正常，另一台仍异常”，确认 COM27 等待页频闪/偏上恢复正常，COM28 原版作为现场对照仍异常。随后经主会话授权，COM28 也更新同一候选，用户再次明确反馈“两台新屏现在都正常”。本轮静止等待新游戏页的频闪、错位和偏上症状按用户实机确认结案。此次对照支持该修复改善等待页症状，但未测得扫描 underflow 的逐事件因果，不扩展为所有游戏页、帧率或电池验收。

本端仅维护 `Firmware/PlayerConsole`、所属测试及本文。先由 COM27 独占诊断、COM28 保留原版本作对照；COM27 获用户确认后才按主会话新授权更新 COM28。COM7 全程不操作。主会话维护 [两屏刷写与迁移记录](player-two-screen-flash-2026-10-15.md)，统一 Git 提交、用户视觉反馈及现场协调。冻结 v3 包和原厂完整备份保持原样；不创建新局、不注入游戏/Tag 输入。

## 新旧屏对照

| 项目 | 原屏 COM7 | 新屏 COM27 / COM28 |
| --- | --- | --- |
| 完整 MAC | `dc:b4:d9:02:d1:dc` | `dc:b4:d9:02:bf:88` / `dc:b4:d9:07:81:b0` |
| 本次问题发生时普通应用 | `22ca08e663a312db860bfd4c60443b9849f76a1e0de50e5976f417f2820392f6` | 相同应用，四段写验已由主会话完成 |
| 现场页面 | 已有 seat1，配对超时页 | seat0，等待下一次新游戏纳入 |
| 用户视觉反馈 | 正常 | 静止时仍频闪、错位、偏上 |
| NVS/分区 | 已有本项目普通布局 | 原厂布局首次迁移，20 KiB 新 NVS 初始化，原始 16 MB 各有完整备份 |
| 网络行为 | 已配对，正常席位会话 | 约每秒发现服务器、PairAccept 拒绝后重试 |
| 供电说明 | 用户已修复、接电池 | 用户确认两块也已返修、接电池；不扩展为电气全项验收 |

没有证据证明新旧屏具体面板型号不同。两份新屏原厂镜像相同也不能证明其硬件或校准参数完全相同。当前显示初始化为编译期板型，并在 `transport.begin()` 前完成；现有代码未见从用户 NVS 读取显示位置/时序校准的路径。首次迁移差异和等待新局业务状态分别记录，不直接归因于 NVS。

## 初始化、扫描和历史故障核对

冻结 `22ca` 构建使用 ESP32 core 3.3.11、ESP32_Display_Panel 1.0.3 和 `BOARD_VIEWE_UEDX48480021_MD80ET`：480×480、RGB565、16 MHz PCLK，水平 HPW/HBP/HFP=8/20/40、垂直 VPW/VBP/VFP=8/20/50，PCLK 上升沿。库板型控制器声明为 GC9503，采用板型自带初始化序列，SPI 控制引脚在初始化后与 RGB 引脚复用。

应用在 `board->begin()` 前配置两个 PSRAM 帧缓冲和 40 行内部 bounce 缓冲，LVGL 为 mode3/direct，任务核与 Arduino/RGB 初始化相同（core1）。固定 SDK 已启用 `CONFIG_LCD_RGB_RESTART_IN_VSYNC`，PSRAM 80 MHz，`bb_invalidate_cache=0`。当前库在 IDF≥5.4 使用 `on_frame_buf_complete` 回调；应用保留等待提交完成和额外帧边界的既有 frame-ticket 屏障。

历史 core0 候选虽曾通过性能测试，随后用户报告频闪和错位，已撤回；其性能 PASS 不能移植到 core1，更不能作本次新屏验收。[历史报告](player-console-development-progress-2026-09-07.md)保留详细过程。[Espressif RGB 文档](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html#bounce-buffer-with-single-psram-frame-buffer)说明 bounce 补充受到 PSRAM/cache 争用时可能造成偏移，重启扫描后仍可能闪烁；这是机制线索，本轮没有采得硬件 underflow 计数，尚不能证明本次具体原因。

首个候选不改变面板初始化、PCLK/porch、40 行 bounce、双缓冲、core1、mode3 或 frame-ticket，不重用旧 core0 方案，也不把已经启用的 VSYNC restart 作为新增修复。

## 确定性重绘缺陷与修复

真实路径为：`processUdpPairAccept()` 拒绝入局 → `resetUdpSession()` 推送 Connecting → 再推送 WaitingForNewGame。状态层保留可操作的等待提示，但每次事件仍更新 revision；`uiRendererRender()` 原本只对部分头像和焦点变更保留控件，相同等待内容进入 `rebuild()`，最终整屏 invalidate。

本次只在 `ui_renderer.cpp` 增加等待页保留分支：前后均为 WaitingForGame、所有可见状态相同、焦点/编辑状态和边界提示未改变时，同步已渲染 revision 而不重建。网络重试、状态清理、命令清理均照常执行；真正 Connecting、Syncing、首次拒绝、覆盖提示、素材失效以及离开等待页仍走原更新路径。

主机回归通过实际 `appHandleTransportEvent()` 和真实 LVGL 执行，未用假渲染器替代：

- 修复前：60 次重试产生 120 次 rebuild、整屏 dirty；重复 Syncing 也重绘，2 项预期失败。
- 修复后：相同重试 0 rebuild、0 dirty，重复 Syncing 同样不重绘。
- 首次拒绝、真实 ConnectionLost、开始 Syncing 均仍重绘；其余头像焦点、手写和队列交互覆盖一起通过，共 51 项。
- setup 83 项通过，保留已入局清理、真实状态恢复和等待页禁止游戏输入的既有契约。

本轮证据目录：`C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/new-screen-display-20261015/`。失败基线 `runtime-before-final.log`，修复后 `runtime-after.log`，状态回归 `setup.log`。最初夹具累计 rebuild 计数断言修正前的日志 `runtime-before.log` 也保留，不把其中额外的计数断言失败算作产品缺陷。

## 构建、实机与剩余验证

新普通构建目录：`C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/runs/production-20261015-034937-65c3bb6e35bb4de8ad6f70de5db387ad/`，`-Mode production -BatteryBoard none`。构建成功，program 1,905,666 bytes、静态 RAM 125,672 bytes，HWCDC 链接检查通过。构建 Git 基线为 `02da7d3231166705b476f8f7f1daa393e56c57fc`，源码随后由主会话统一提交为 `e6d6563d49a0a7f07d2ef67bdea6e51ab5caf0c2`，没有因 HEAD 前进重建。

- 应用 SHA256：`dfc4b7e547a129fd5b500396a2a1f0e3866edf80b53d6684f0830d0539340cb6`（文件 1,905,808 bytes）。
- `candidate.json` SHA256：`15f4b88650a5a061af9fc38ebab08d3953c8647461ede683f7f5447580a7e138`。
- `candidate-sources.json` SHA256 / sourceDigest：`2885640893da7a8b26f2e7e30f85ead131c8b601024382a955ed7f7b2335f19a`。
- 新旧各 2,135 份源清单逐项比较，仅 `sketch/ui_renderer.cpp` 改变；core/flags/fqbn/mode/batteryBoard 均相同。新快照全部源文件及四段二进制 SHA 独立核验通过，主会话另行复核通过。bootloader、分区表、boot_app0 与 `22ca` 完全相同。

COM27 基线于 UTC 07:50:13.095230（EDT 03:50:13）开始，30.016 秒、2,580 bytes，DTR/RTS=false，无命令或主动复位，error=null、closed=true、无窗口内启动/故障标记；IP `10.42.0.196`、Wi-Fi status3，持续发现原房间服务器。见 `COM27-before-retained-summary.json` 及原始日志。

COM27 候选写验在 UTC 08:00:39.966920–08:00:57.511517（EDT 04:00:39–04:00:57）执行一次。沿用未修改的冻结 v3 受保护 worker、固定 esptool 5.3.1，同一串口上下文校验 ROM/stub 完整 MAC、16 MB 及已有分区一致；四段写入与校验均成功，明确 hard-reset，未 erase-all、未重新初始化 NVS、未改变分区。见 `COM27-retained-flash.json`，结果 `verified`。

COM27 正常观察于 UTC 08:01:20.824927（EDT 04:01:20）开始，60.078 秒、5,428 bytes。error=null、closed=true，无观察窗口内重启、panic/brownout；前段有 reason2，uptime35368 ms 获得 IP `10.42.0.196`，之后 status3 并持续正常发现原服务器，末条进度 uptime80038 ms。首次完整启动/build 行早于采集，未捕获；不能写成完整启动全过程或完整 60 秒持续在线。既有 WiFi.reconnect 返回 requested=0，不能说由该调用触发恢复。主会话 `root-after-com27-http.json` 于 UTC 08:01:45.549276 核对三台 online/eligible、两新屏 seat0、原屏 seat1、原 room993580111/version23 保留。

用户随后明确反馈“更新的那台已正常，另一台仍异常”，记录为 `user-COM27-visual-confirmation.json`，仅确认当前等待页静止频闪和偏移改善。基于此结果，主会话再授权 COM28 使用同一候选、同一受保护流程，未重新构建。COM28 在 UTC 08:03:35.399294–08:03:53.034811（EDT 04:03:35–04:03:53）完成四段写验、完整 MAC `dc:b4:d9:07:81:b0` 核对和 hard-reset；未清 NVS 或改变分区。见 `COM28-retained-flash.json`。

COM28 正常观察于 UTC 08:04:19.879997（EDT 04:04:19）开始，60.047 秒、5,434 bytes。error=null、closed=true，无窗口内重启、panic/brownout；前段 reason2，uptime34295 ms 获得 IP `10.42.0.54`，末条 uptime85024 ms 仍 status3，持续发现原房间。WiFi.reconnect 同样 requested=0；观察开始晚于首帧/build 行，不补写未捕获的启动证据。两次采集均 DTR/RTS=false、不发串口控制或业务输入，结束关闭句柄；明确复位只发生于各次受保护烧录的流程中。

两次刷完均立即同步主会话请求用户目视，未等观察结束才发问。COM28 刷后用户明确反馈“两台新屏现在都正常”，已保存 `user-both-visual-confirmation.json`；主会话 `root-after-both-http.json` 确认三屏 online/eligible、room993580111/version23 保留。两口已释放，本轮不再追加串口或刷写。COM7 保持 `22ca`，本端没有操作它；原冻结 v3 的 `22ca` 文件不改写。主会话负责其余文档、Git 和新 v4 包；所有页面、完整实体局及性能后续验收由主会话协调。

最终证据入口为本机目录内 `outcome.json`、`evidence-sha256.json` 和本文副本；原始串口、写验结果、失败基线、冻结源清单核对与两次用户反馈分别保留。源码检查点 `e6d6563` 已由主会话提交，本端没有暂存或提交共享 Git 索引。

原 core1 Retarget 最大 57 ms 高于 42 ms 的性能限制仍保留；本次没有新增性能 PASS。精确旋钮步数、Avatar/手写和完整游戏页面验收仍按对应报告待验证。
