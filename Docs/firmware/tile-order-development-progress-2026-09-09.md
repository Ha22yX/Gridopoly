# 格子 ORDER 开发与双板部署（2026-09-09 EDT）

## 已实现

V0.32 在每个模块 ORDER_OUT 上独立发送启动随机 bootId、16 位序号及 CRC16；ORDER_IN 只解码直接上游，HTTP 心跳携带实际接收证据。GPIO10 是 Q1 栅极，高电平拉低线路，低电平释放。独立 esp_timer 任务以 1ms 驱动纯状态机，6 秒发送周期，15 秒接收证据有效期。迟调度超过 6ms 放弃当前收发并释放输出；重复/旧序号不能续期，连续低电平超时撤销接收。完整格式与限制见 tile-order-protocol.md。

HTTP 保留 tag 数据并增加 order 对象，解析服务器 assignment 时保留 source=order。显示时钟通过出厂 MAC 白名单隔离：已目视验证的 COM6 保持 40MHz，COM8 及其它未验证模块默认 8MHz。本地 display.local.h 不提交。

## 源码、测试与构建

新增 include/order_link.h、include/order_link_io.h、src/order_link_io.cpp 和 HostRegression/order_link.cpp，修改 main.cpp、tile_network.cpp、board_config.h、display.example.h、CMakeLists.txt、firmware_boundaries.cpp 及 TileModule README。主任务已统一审核并提交固件与可复现测试 11 个文件，提交为 `487b26d`；报告与协议文档由主任务随后统一提交。

最终 HostRegression 全量 7 个 suite / 56 个测试通过，ORDER 8 项覆盖 CRC/全部单比特错误、直接邻接、序号重放/回绕、boot 变化、拔线 TTL、卡低恢复、畸形脉宽、调度丢帧和微秒计时回绕。首轮 decoder.exe 曾出现 Windows BAD_COMMAND 启动失败，单独重试通过；随后含 MAC 绑定测试的最终全量运行全部通过，保留初次异常。

PlatformIO espressif32 7.0.1 / Arduino 2.0.17 最终正常构建成功：RAM 69996 字节，Flash 2705233 字节，firmware.bin 2705600 字节。

最终 V0.32 SHA256：`176db3cc3ea78c920f235ebcffafeef423dbe07caf6db7bb89a9e4681e502760`。

## 实际部署与启动

COM8（68:ee:8f:54:11:a4）ROM 确认 ESP32-S3、16MB Flash、8MB PSRAM；先完整读取 16777216 字节 Flash，耗时 208.4 秒。备份 SHA256：`dffab0dd410657cb30c7b2fd7f2586a4792e8472e58882b3532581f8111a646d`。

COM8 和 COM6（28:84:85:ba:9f:e8）依次烧录相同最终 V0.32，esptool 写入哈希校验均成功；烧录都执行了硬复位。COM6 正常联网、显示实际 40MHz。COM8 初次 120 秒无任何设备串口字节；额外 run/hard reset 后 45 秒仍无字节。随后无前置复位即可读取 ROM，GPIO_STRAP_REG=0，说明最近启动 GPIO0 采样为低、进入下载模式。执行一次 esptool 内置 watchdog_reset 后，45 秒窗口取得正常 V0.32/8MHz/HTTP200 和 ORDER 发射。记录的是这一操作序列，若用户同期实体操作，不能独占归因软件复位。

COM6 回退产物为 V0.31 SHA256 `c56dfb084c536bf7e154248afd8103cf2a810d493802b6649d669dbcd3ea2f86`，仅作故障回退备份，未回刷。未操作 COM7/COM5。

## 真实 ORDER 与分配

初次 COM6 120 秒观察：boot `049DC51DD2157E4C`，tx_seq 1→21，timer OK，timing_drops 始终为启动时的 1，无有效上游；此时 COM8 未正常启动，不能视为链路失败或成功。

COM8 正常启动后 boot `426C020ABE7B4B34`。实际服务器 HTTP 图变为 `tile-68ee8f5411a4 → tile-288485ba9fe8`，两节点均在线、orderCapable=true、orderStatus=ready。COM6 是链内 index1，COM8 是 index0。这一顺序不同于注册先后（COM6 先注册），是接收证据形成的物理拓扑。

主任务在无 pendingMove 的对局中将 COM6 设为 CORNER-START/Grid Central/map0 手动锚点。主保存的 /api/state 前后仅 identity.serverEpochMs 改变，其余业务与版本无变化。格子任务没有注入 HTTP ORDER 报告或写入手动分配。COM6 手动锚点保持，COM8 获得 E2/map23/source=order/orderOffset=-1/orderAnchorModuleId=COM6；COM8 串口同样明确 source=order id=E2 map=23。

COM8 未接屏，不作显示目视通过结论；其 Tag reader state=fault（即使 TXDIS OFF 回读正常），这是独立的待检查硬件/读卡状态，不阻止已观察的 ORDER 与网络运行。尚未现场验证拔线、重接和互换方向；相应状态机/服务端测试不能替代实际硬件操作。

## 证据位置

原始文件均在 `C:/Users/kicof/AppData/Local/Temp/gridopoly-tile-order-v032/`：

- candidate-manifest.json：最终 bin/bootloader/partition/回退/备份和源码 SHA。
- host-build.log、host-tests.log：最终主机测试。
- com8-before-v032-flash.bin、com8-backup.log、com6-v031-rollback.bin、v032-final.bin：本地备份和最终产物。
- immediate-before-upload.json：烧录前 COM6 legacy auto START，无手动锚点。
- com8-upload.log、com6-upload.log：写入、MAC、校验和复位。
- com6-observe.jsonl、com8-observe.jsonl、com8-run.log、com8-after-run.jsonl、com8-strap-watchdog.log、com8-after-watchdog.jsonl：启动排查和有界原始串口。
- after-com6-http.json、first-real-chain-http.json、com6-real-chain.jsonl：锚点、真实图和接收证据。

最终构建日志另在 `C:/Users/kicof/AppData/Local/Temp/gridopoly-tile-v032-build-final.log`，build 目录为同级 gridopoly-tile-v032-build/tile_esp32s3。备份、凭据、编译产物及工具缓存不提交。

## 最终有界观察与多锚点联调

COM6 追加 60 秒窗口已自然结束并释放串口，COM8 最后 45 秒窗口也结束。COM6 boot 保持 049DC51DD2157E4C，上游始终匹配 COM8 的 426C020ABE7B4B34；rx_seq 9→18，accepted 8→17，全部记录 valid=YES，rejected=0，timing_drops 固定为 1，证明持续接收完整 CRC 有效帧。窗口内同时捕捉实际 source/tile 从 manual START0 → manual A2/map3 → order B2/map8，boot 未改变。窗口未覆盖全部五步的每个显示响应，不将 HTTP 数据冒充所有串口响应。

主任务进行了五步真实 HTTP 锚点操作，每步给真实心跳 5 秒：COM6 manual A2/map3 后 COM8 order map2；COM8 manual B1/map7 后 COM6 manual3 保留；清除 COM6 手动指定后 COM6 order8、COM8 manual7；COM6 恢复 manual START0 后双方 manual0/7；清除 COM8 手动指定后恢复 COM6 manual0、COM8 order23。独立读取主证据 physical-anchor-sequence.json 的 5 步 expected/actual 一致、businessUnchanged=true；主比较排除动态 identity.serverEpochMs，其余业务不变（room993580100/v39）。原始文件位于 C:/Users/kicof/AppData/Local/Temp/gridopoly-root-order-review/。

最终本任务独立读取 final-real-chain-http.json：拓扑 epoch7/ready 仍 COM8→COM6，COM6 manual CORNER-START/map0，COM8 order E2/map23/offset-1。两板保留正常 V0.32，所有本任务串口/刷写进程结束。双板 ORDER 识别、前向/反向锚点派生、双方 manual 保留及清除后恢复已经真实验证；拔线恢复与 COM8 读卡故障保留各自范围。下一步由主任务完成 Git 审核提交及用户汇报，实体拔线验收如需要由主协调。本任务没有后台监控或自动化。
