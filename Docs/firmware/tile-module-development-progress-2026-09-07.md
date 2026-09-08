# 格子模块开发进度 — 2026-09-07

本报告区分源码、构建、烧录与实机结果。时间使用 America/New_York。

## 工作范围与现场

- 当前工作目录：`Z:/Files/我的项目/Gridopoly`。
- 本端独占 COM6；未操作 COM7、树莓派配置、线上房间、分配或 Tag 绑定。
- COM6 PnP：`USB VID_303A&PID_1001&MI_00`；串口启动标识为
  `GRIDOPOLY TILE MODULE V0.27 - TAG PLAYER LINK`。
- 模块身份：`tile-288485ba9fe8` / `28:84:85:ba:9f:e8`。
- 当前 HTTP 心跳已在线，服务器租约 15 秒，多个样本剩余约 13～14 秒。
- 首次样本为 B2/Tideway Drive，之后看到 A2/Copper Lane；本会话没有修改分配。
  用户也在现场操作，不据此反推其此前报告错误。

## 本次联网与 RFID 证据

约 21:35 的一次串口采集使用 .NET SerialPort，Open 前设置 DTR=false、RTS=false，
仅发送 STATUS。开始读到 HTTP 200、RSSI -58、4.773V/196.0mA、TXDIS=OFF、
Tag count=0，随后日志出现 `USB_UART_CHIP_RESET`。
不能排除串口打开触发复位；也不能仅凭这一段日志证明因果。本会话没有发送 RESTART。
已关闭句柄并停止重复打开，避免干扰现场。

该启动日志记录：

- HTRC110 `TXDIS=1 verify=PASS`。
- INA226 `manufacturer=0x5449 die=0x2260 calibration=OK`。
- WS2812 `count=10 RMT=OK`。
- 初始功耗 4.900V / 104.3mA。
- 启动后 `t=1987ms state=NO_TAG sampling=0x2F field_off=PASS`。
- Wi-Fi 关联、DHCP、HTTP 心跳随后恢复。
- 服务器随后显示 `tagReaderState=stable / tagRevision=1 / overflow=false`，
  `/api/tile-tags` 的 `tags=[]`，HARRY/P1 绑定 UID 为 `8EFA24DF`。
- 后续只读 HTTP 样本持续在线，没有复现历史 `fault`。

这些证据只说明本次观测时为“稳定无标签”；不能否定用户此前看到标签已识别，
也没有验证线圈谐振、波形或场强。未刷入任何隔离线圈测试固件，未增加场持续时间、
改标签存储或变更保护阈值。

## 已确认的软件问题与修复

原流程先提交本地显示使用的 UID 集合，再调用 `updateTagObservation()`。
该函数拿网络缓存锁失败时直接返回；下一次扫描若集合相同，`publishTagInventory()`
提前返回，不再发布。因此一次锁失败可以造成“本地有 UID、网络仍是旧集合”。

已用生产函数的隔离编译与锁失败注入复现此条件。**尚无现场锁失败证据，不能认定它
就是用户此次未自动到达的根因。**

V0.28 源码修复：

- `updateTagObservation()` 返回是否已接受完整报告；相同已接受报告也返回成功。
- 主循环保存发布 pending 标志，失败后重试当前最新的完整 readerState/UID/overflow。
- 成功后清除 pending；相同集合不增加额外 revision。
- 新本地状态替换旧 pending，不重放已过时 UID。
- 不改变 HTTP wire、RFID 扫描/关场保护或 LED 行为。
- 启动版本改为 `V0.28 - TAG REPORT RETRY`，便于候选设备核验。

## LED 软件与实机边界

已核对并测试生产代码：

- `none/departure/destination` 解析与标签名称。
- 10 颗灯同色、各颜色通道不超过板级上限。
- departure：整圈橙色呼吸；destination：每 900ms 两次绿色闪光。
- 只有 movement mode/player/revision 变化才更新动画起点；普通 heartbeat/resync
  的 server/assignment revision 变化不会重启动画。
- cue 回到 none 后恢复正常格子颜色。

上述为主机测试，不代表真实 LED 电气输出或用户目视验收。Action 17 在圆屏与服务器间
完成；格子无须增加该命令。联合窗口需主会话协调圆屏动画、gate、下行 cue 和实体 LED。

## 构建与测试

新增独立项目：`Firmware/TileModuleTests/HostRegression`。
它直接提取生产发布、缓存更新、LED 渲染与 movement 比较代码，替换 RTOS/Serial/RMT
边界进行故障注入。生成片段位于构建目录，不提交。完整 Arduino 编译仍单独执行。

- 修复前：新增 5 项中 2 项失败（相同集合未重试、最新完整状态未发布）。
- 修复后：新增 5/5 PASS，Release 与 Debug 均通过。
- 全部 Debug 原生回归：5 个套件 / 37 项 PASS：模型12、解码11、HITAG协议5、
  标签存在过滤4、固件边界5。
- 服务器开发会话只读复核本轮 pending/缓存提交边界，未发现阻塞问题；HTTP失败仍会
  按原心跳周期重试已提交缓存，同 revision 服务端也会重新评估自动到达。
- MSVC Release 对现有解码测试的优化耗时超过数分钟，已停止该编译并使用 Debug
  完成全套；不声称完整 Release 全套通过。
- ESP32 正式候选：PlatformIO `tile_esp32s3` 构建成功（espressif32 7.0.1 /
  Arduino 2.0.17），已在主会话安排的 COM6 窗口烧录。RAM 69,284 / 327,680 bytes；程序 Flash
  2,682,513 / 6,553,600 bytes；firmware.bin 2,682,880 bytes。
- 二进制内已核对 V0.28 标识；候选 SHA-256：
  `05e30833522479ec27ed7d95b4306d586f9a9bb03a1c602fd982ac97d13ec855`。

本机独立构建目录：`C:/Users/kicof/AppData/Local/Temp/gridopoly-tile-native`。
正式固件输出目录：`C:/Users/kicof/AppData/Local/Temp/gridopoly-tile-esp32-build`。
PlatformIO 工具缓存与 Python 依赖也在本端 Temp 目录；不得加入 Git。

已保留旧构建产物于上述 native 目录的 `pre-v028/`，包含 firmware、ELF、bootloader、
partitions；旧 firmware.bin 2,682,496 bytes，SHA-256：
`8465fb70488e17979bc9101b0e0dde159cb8c4e44bf2683ea22197bec8f5e27c`。
这是已有构建产物备份，没有读取设备 Flash 来证明它与现机完全一致。

## V0.28 烧录与恢复

- 21:50:05 烧前快照：room `993580098` / version `95` / gate inactive。
- 烧前再次检查 COM6 PnP ID，上传工具进一步确认 ESP32-S3 revision v0.2、
  MAC `28:84:85:ba:9f:e8`。
- 上传完成：firmware 2,682,880 bytes，应用写入地址 `0x10000`，Hash verified，
  PlatformIO SUCCESS；工具按正常流程复位启动。
- 21:52:20 打开一次 pyserial 持续会话，Open 前 DTR=false/RTS=false。
  后续 STATUS 正常返回，未看到该次打开产生新 USB reset。
- 串口启动前固件已启动，未重新捕获 V0.28 banner；以已核对的二进制标识、上传
  哈希校验和随后的运行状态作为此次部署证据，没有为补日志再次复位。
- 运行样本：HTTP 200，RSSI -65，4.804V / 195.3mA，TXDIS=OFF，NO_TAG/count0。
- 烧后快照：同 room / version `119` / gate inactive；用户现场仍在操作，
  本会话未提交游戏动作，不能把这段 version 变化归因于固件更新。
- 上传停心跳超过 15 秒，模块 lease 过期；重新上线自动认领 `CORNER-START/map0`，
  source=auto、serverRevision=8、registrationOrder=3。该自动重分配已通知主会话，
  本会话没有调用改派接口。
- 服务器 stable/tagRevision1/overflow=false、tags为空，与当前 STATUS 一致。
- 原始记录保存在本端 native 输出目录的 `upload-v028.log`、`serial-v028.log` 和
  `before-upload-*.json` / `after-upload-*.json`，不加入 Git。
- 主会话依据烧前快照恢复 A2/Copper Lane/map3/source manual。21:54:26 本端串口
  收到 revision9 的分配与显示更新，21:54:27 确认重绘；恢复后 HTTP200，Tag仍count0。

## 待联合验收与提交范围

1. 正式构建、COM6 身份、烧录与联网恢复已完成；原串口窗口已自动结束，后续联合窗口再协调采集。
2. 将已绑定的 `8EFA24DF` 棋子放在线圈中心，记录屏幕 UID 与服务器完整集合是否一致。
3. 圆屏 Action 17 就绪后，观察 gate 前后原格/目标格灯效；不自行改房间来制造场景。
4. 自动到达需要“目标格分配正确、Tag 绑定正确、稳定完整集合、gate ready”。
   RFID 硬件问题若再次出现，单独收集关场、电压/电流与数字链路证据。

本轮修改：TileModule 的 `src/main.cpp`、`src/tile_network.cpp`、
`include/tile_network.h`、`README.md`；新增 `TileModuleTests/HostRegression/`；本报告。
整个 TileModule/TileModuleTests 原先仍未跟踪，主会话需区分历史基线和本轮修复。
本会话没有 git add/commit/reset 等写操作；暂存与提交由主会话串行完成。

供主会话拆分提交的材料在本端 native 输出目录 `git-delivery/`：
`v028-tag-report-retry.patch`、`manifest.json`、`reconstructed-v027/`。
基线是按本轮编辑记录逆推的 UTF-8/LF 文本，不是编辑前原始文件快照。
已对当前工作树执行补丁 reverse/check 验证，仅检查，没有应用或改索引。

## 被动监控结束后补充（22:16）

21:52:20～22:12:20 的单次 pyserial 窗口已自动结束、句柄关闭，COM6 已释放。
没有为收尾重新打开串口。完整日志中未见新 USB reset、fault 或 departure/destination。

回查得到一次真实 UID 识别：

- 21:58:25.821：`t=381632ms state=PRESENT count=1 uids=8EFA24DF consistent=3
  sampling=0x2D field_off=PASS`。
- 21:58:31.709：`t=387521ms state=NO_TAG count=0 sampling=0x23 field_off=PASS`。
- 这证明设备曾识别绑定 UID，并在约 6 秒后判定不再存在；未同步采集该短窗的服务器
  完整 Tag 集合，也没有用户实际移入/移出时间证据，不能进一步声称心跳/自动到达通过，
  或据此测算真实移除响应延迟。服务器会话已确认没有相同时窗的抓包/HTTP记录，
  无法补证该次完整上报、gate/target 或自动到达结果。
- 窗口结束时格子为 `T-WEST/map6/source manual/revision10`，HTTP200，
  4.815V / 190.0mA，TXDIS=OFF，Tag count0，cue=none。本会话没有修改该分配。
- 主会话随后只读检查：模块 stable/local tagRevision3（刷后为1），全局 tagRevision42，
  当前 tags为空；同 room/version170/phase1/gate inactive。服务器已接收新的报告 revision，
  但当前投影不能还原历史 UID 具体值。仍以“真实本地 UID 识别已见；同步上报及自动到达
  尚未闭环验证”为结论。当前 T-WEST 分配是现场新状态，不恢复旧 A2 覆盖它。

主会话已完成 Git 提交：`5a81ceb` 保全 V0.27 历史基线，`31702c9` 提交 V0.28
修复、HostRegression 与此前报告。本节为之后新增证据，由主会话另行提交。
