# 格子屏刷新线优化：V0.29

后续验收更新：用户确认本版本整页仍明显从上到下扫入，V0.29目视验收失败。
后续40MHz实机换页获得“明显改善，图文完整”确认，正常V0.31部署见
[后续时钟验证记录](tile-display-clock-progress-2026-09-08.md)。以下保留V0.29阶段原始结果。

## 范围与诊断

本轮负责 TileModule / COM6。用户已授权正常候选构建、烧录和有界验证；
不修改房间、分配、Tag 绑定，COM7 由玩家屏任务负责。此前灯光/Tag 到达症状已由
用户确认解决，本轮不得回退 V0.28 Tag 缓存提交重试功能。

- `tile_network.cpp::snapshotChanged` 包含服务器、assignment、movement revision。
  原主循环每次 consume 后无条件 `renderPage()`，后者没有变化检测。
- `drawTilePage` 先物理全屏清空，再画文本、图像和卡片；assignment 后一秒再次全刷。
  诊断页每 500ms 也清屏重画；页脚每次发送 240×34 像素，内容相同也发送。
- `Docs/hardware/esp32-s3-pin-map.md` 的 J4 只有 GND、3V3、BL、CS、CLK、MOSI、
  DC、RST；无 TE/MISO。不能同步面板扫描或读取像素确认显示。
- 保留已验证的 8MHz。240×320 RGB565 仅数据理论耗时 153.6ms，尚不含软件开销。
  历史 20MHz 和 Wi-Fi 活跃时长 PROGMEM 直接传输均不可靠，不恢复这些路径。
- 当前 Adafruit_SPITFT 的 ESP32 `writePixels` 调用 SPI `writePixels`，不提供可用的
  非阻塞 DMA 提交语义；本轮使用同步提交和脏区合并，没有伪称启用异步 DMA。

## 实现

- 两幅 PSRAM 帧各 153600 字节：当前合成页面和最近完成 SPI 提交的影像。
  原页面布局绘入 GFXcanvas16，物理屏不再接收逐个清空、画底、画字的中间状态。
- 比较每行首末变化像素，相邻相同跨度的行合并为一个地址窗口。未变化的页面提交
  零像素；文本变短时旧字像素也会被新的背景覆盖。
- SPI 每次从 PSRAM 拷贝到对齐的内部 RAM 行缓冲，发送完成后才更新相应 shadow。
  这里的“完成”是同步 SPI 提交完成；写入面板是否正确仍须目视，不能靠主机 shadow 证明。
- 首帧和 ST7789 初始化/REDRAW 使 shadow 无效并强制整屏提交。
  普通 cue/revision 更新只更新页脚和灯光；TileState/连接页内容变化才重新合成页面。
  原 assignment 一秒后补刷已移除，诊断页不会被心跳强制退出。
- PSRAM 两幅帧必须一起分配成功；失败时释放局部成功的分配，报告 `DIRECT_FALLBACK`，
  保留原直接渲染和可靠的图像 RAM 行上传。现场须确认 `renderer=PSRAM_DIFF`。
- `STATUS` 增加 `[DISPLAY-PERF]`：累计非空提交、空提交、矩形、像素、完整帧数量，
  最近/最长差分比较与 SPI 提交耗时。耗时不包含上游页面合成，也不是面板扫描 FPS。
  原 RFID 读取、Tag 重试、HTTP 协议、LED cue、背光设置保持原实现。

## 构建与主机验证

- PlatformIO 正常 `tile_esp32s3` Release 构建成功；RAM 69844/327680，
  program Flash 2683773/6553600。运行时双帧 PSRAM 另占 307200 字节。
- 候选 bin SHA-256：
  `e60991a2f126111b7b256275184aa69f8e610ec3046ab3a6a82d24e444369829`。
- HostRegression Debug 六套共 44 用例通过：既有 37，新增差分六例和生产重绘判断一例。
  差分覆盖首帧/强制重画、零变化、合并及保留周边、边缘/空行、失败后重试、
  60 帧随机变化与模拟面板逐像素一致性；生产提取测试验证 revision/cue 不触发整页重画。
- `git diff --check` 无空白错误。主任务统一审核和提交，本任务未操作共享 Git 索引。

## 回退与设备窗口

本机临时目录前缀：`C:/Users/kicof/AppData/Local/Temp/`。产物/凭据不提交 Git。

- `gridopoly-tile-pre-v029/` 保存 V0.28 完整构建目录、main.cpp、board_config.h、
  platformio.ini 及本地配置副本。固件 SHA-256：
  `05e30833522479ec27ed7d95b4306d586f9a9bb03a1c602fd982ac97d13ec855`，与已知 V0.28 一致。
- `gridopoly-tile-v029-build/tile_esp32s3/` 为独立候选构建目录，未覆盖回退产物。
- COM6 PNP 为 `USB\VID_303A&PID_1001&MI_00\7&160B3F33&0&0000`，MAC
  `28:84:85:ba:9f:e8`。基线串口 60 秒，STATUS 间隔 15 秒，结束正常释放。
- `gridopoly-tile-pre-v029/baseline.jsonl`：当前 CARD-CF-1/map2/rev12、HTTP200、
  Tag `8EFA24DF` 在位、TXDIS OFF。无针对旧固件添加计时，本轮不捏造旧版实测耗时。
- `pre-upload-assignments.json` 保存烧录前只读 HTTP 分配快照，仍为 CARD-CF-1/rev12。

## 已部署与实机观察

- 已烧录上述 V0.29 正常候选，bin 长度 2684144 字节；esptool 核对目标 MAC，
  写入后 `Hash of data verified`，最后明确由 RTS 重启。上传任务耗时 35.04 秒，
  app 写入 12.7 秒。`gridopoly-tile-v029-build/upload.log` 保存原始输出。
- 烧录跨越 15 秒租约，服务器分配自动变为 CORNER-START/map0/rev14。
  本端只读记录并告知主任务；主任务恢复原 CARD-CF-1/map2/manual，新的 revision15，
  updatedAtMs 1788909674656。最终 HTTP 确认 rev15 在线且 TagReader stable。
- 候选串口窗口 `candidate.jsonl` 起止 epochMs：1788909606867～1788909786938，
  180.071 秒，95 行，正常 CLOSED，COM6 已释放。STATUS 每 15 秒，时间为主机收取时间。
  没有观察到窗口内 reset/fault，13 个 Tag 状态样本均为 PRESENT `8EFA24DF`、TXDIS OFF，
  网络状态报告 HTTP200。不是整个三分钟的物理层无故障证明。
- 运行时报告 `renderer=PSRAM_DIFF`。首次全屏提交最长 172480us，完整帧累计为1。
  窗口捕获一次恢复分配后的 TILE 页面重画日志，完整帧计数仍为1，确认以差分提交。
  未独立采到该次换页的准确单次耗时，不能用下一条 STATUS 的 last_us 代替它。
- 首末统计样本跨度165.377秒（与完整采集时长不同），增加323次非空提交、8次零像素提交、
  2002矩形、181803像素；完整帧增量0。包含一次换页，约2199字节/秒。
- 恢复 CARD-CF-1 后的稳定统计区间：1788909682034～1788909772309，90.275秒，
  176次非空提交、4次空提交、76216像素，约1689字节/秒；这一段所采 last_us
  为7587～8780us。均为差分比较+SPI提交时间，不含页面合成，不代表显示器真实FPS。
- 旧源码固定页脚每500ms发送8160像素，代码计算预算32640字节/秒；上述稳定新路径
  像素量约低95%。这是新实测对旧源码预算的比较，旧版未插入计时/传输计数，不能写成
  同一场景旧固件实测加速比。原60秒基线也未发生网络语义变化，未记录整页重画日志。
- `candidate.summary.json` 和三份分配快照保留在候选临时目录。没有新增自动化。

## 剩余验收与交接

主任务已经向用户询问刷新线改善和图文完整性。为保持正常页面供观察，拟定的45秒
DIAG/REDRAW窗口**没有执行**，未向用户重复提问。本次已经完成源码、主机测试、正常
构建、部署和有界串口验证；源码提交由主任务统一完成，目视效果仍待用户确认。

- 主任务：汇总用户目视结果并审核本轮7个本端文件；如用户确认画面异常，继续定位。
- 本端：如主任务后续安排，再验证 REDRAW 后强制全量提交、控制器恢复和诊断页往返。
  当前该边界只有源代码审查和差分 force 路径的主机测试，不能标记为实机通过。
- 内存不足的 DIRECT_FALLBACK 有实现和源码审查，本轮实机实际走 PSRAM_DIFF；
  没有人为耗尽 PSRAM 验证故障回退。
- 无 TE 条件下，真实大面积换页仍有扫描交错的可能。减少清屏和冗余传输不等于保证零撕裂。
