# Gridopoly 单格模块正式固件

这是格子模块的正式固件工程。当前源码版本为 V0.31 本机验证显示速率（保留 Tag 发布重试）：模块上电后连接树莓派专用热点，
通过 HTTP 心跳自动注册、认领格子并接收显示状态。RS485 和 ORDER 仍未启用。
测试程序仍独立保存在 `Firmware/TileModuleTests/`，不得把循环跑灯、纯色屏幕等
首板测试流程并入本工程。

正式 ESP32-S3 构建明确排除 `demo_engine.cpp`；该文件只保留给 native 状态模型测试，
不会进入格子模块正式固件，也不会在未分配时生成任何本地地产页面。

## 当前功能

- 自动连接 gridopoly 专用热点，默认服务器为 10.42.0.1:80。
- 模块不保存 BSSID 或频道，使用全信道 SSID 扫描；未关联满 10 秒时先取消失效的底层连接尝试再重新扫描，同时关闭 modem sleep、使用 19.5 dBm 发射功率。
- Wi-Fi 关联、DHCP 和 HTTP 是三个独立恢复阶段：关联每 10 秒重试，关联成功后给 DHCP 30 秒完整窗口；HTTP 不可达只重建 TCP 请求，不再拆掉健康的 Wi-Fi 与租约。
- 设备 ID 与模块 ID 始终使用 ESP32-S3 工厂 MAC；每次启动只生成一次本地无线会话 MAC，避免实体 RST 后被 AP 中残留的旧 station 阻塞，且不影响服务器手动分配。
- 每 2 秒通过 HTTP/1.1 持久连接向 /api/tile-modules/heartbeat 续租，避免反复 TCP/ARP 握手；服务器租约为 15 秒。
- 心跳 JSON 携带当前完整 Tag 报告：`tagReaderState`、本地 `tagRevision`、最多 6 个规范化 UID 和 `overflow`；稳定集合或读卡状态变化时立即补发一次，不等待下一个 2 秒周期。
- 网络缓存锁暂时不可用时，主循环会重试最新完整 Tag 报告；相同本地 UID 集合不会抑制失败重试，成功后的重复扫描也不会额外增加 revision。该修复不改变 HTTP wire 或读卡场时序。
- 只有 `stable` 且未溢出的完整集合可参与服务器自动到达；`scanning`、`fault` 和 `overflow=true` 只用于诊断，不会误触发游戏动作。
- 心跳响应携带 `movementCue`：目标格整圈绿色双闪，出发格整圈橙色呼吸；同一移动只在 revision 或 cue 内容变化时重置动画。
- 未被手动分配时，由服务器原子认领 mapIndex 最小的空闲格（00、01、02……）。
- 网页手动改派后立即显示新格，旧格同时释放；目标被其他在线模块占用时服务器返回冲突。
- Wi-Fi 或服务器暂时不可达时保留最后一次有效分配；首次收到服务器分配前只显示联网状态页，不显示本地默认地产。
- ST7789 240×320 竖屏正式界面，以 160×160 格子图片为视觉主体。
- INA226 实时显示 5V 电压、电流和功率。
- HTRC110 未检测到标签时每 400ms 发起一次只读 HITAG S Advanced UID REQUEST；单标签最多五次尝试内三次响应一致后，首页底部显示 32 位 UID。
- UID 响应发生碰撞时，固件选择时间上最早的完整 Advanced SOF，在同一载波会话中按真实首碰撞位展开 HITAG S AC SEQUENCE 前缀树；命令包含协议 CRC-8，结果去重、排序并最多保留 6 个 UID。
- 每个模拟采样相位连续确认两次碰撞；一次多标签复检最多执行三轮同相位枚举并合并 UID 集合，避免强耦合标签暂时遮蔽另一枚标签时把 `TAGS 2` 降成 `TAG 1`。
- 多标签集合每 500ms 复检；底栏显示总数并每 1.5 秒轮播一个 UID，例如 `TAGS 2  1/2 8EFA259D`，串口 `STATUS` 一次列出全部 UID。
- 每个已确认 UID 独立累计缺失证据：一次漏读继续保留，连续两次漏读即可缩减集合或显示 `NO TAG`；偶发边缘读数只降低证据一级，不再重新开始长计时。多标签移除通常在 0.5～1 秒内生效。
- 防碰撞树在单次开场 300ms 工作预算内运行，绝对场持续时间上限 350ms；每次扫描都检查 `ANTFAIL`、350mA 总电流、180mA 场增量、4.65V 欠压和 `TXDIS=1` 关场回读。
- 所有 RFID 命令仅包含 UID REQUEST 和 AC SEQUENCE，不发送 SELECT、READ、WRITE 或任何标签存储器写命令。
- 10 颗 WS2812 作为一个格子的整体灯环：正常状态下未购买显示地区色、已购买显示所有者色；移动提示优先于这些常态灯效。
- 服务器将 Tag 与玩家一对一绑定；目标格稳定读到该玩家 Tag 时，由服务器执行与玩家屏手动 `ConfirmPosition` 相同的权威事务，玩家屏手动确认入口始终保留。
- 正常占用状态仍使用整圈同步呼吸，不使用单颗玩家槽位灯。
- 内置完整 36 张 Grid City Street V3 设备图片：22 个地产、4 个交通、2 个设施、
  2 个卡片、2 个税费和4个角落；服务器分配任一正式 artworkKey 都可直接显示。
- 未分配状态明确显示启动、连接 Wi-Fi、注册服务器、在线等待分配或自动重连；整圈 LED 同步显示相应状态色。
- 连接页只在联网语义状态变化时整页绘制；500ms 动画仅更新省略号小区域，34px 底栏先在 RGB565 内存画布中完整合成，再一次性传到屏幕，避免“先清黑、后逐项写字”产生频闪。
- V0.29 起页面在 PSRAM 中合成，再与已提交影像比较，只传变化区域；相邻相同跨度行共用地址窗口。取消 assignment 一秒后补刷。分配失败明确报告 DIRECT_FALLBACK。
- ST7789 默认保持 8 MHz；所有像素传输均先复制到对齐内部 RAM 行缓冲。历史首板 Wi-Fi 活跃时 20MHz 不可靠，不能把新的 RAM 路径或无崩溃当作高频画面验证通过。
- `config/display.local.h` 为本机忽略配置，未配置时正常运行8MHz。当前模块 tile-288485ba9fe8 的40MHz实际换页已获用户“明显改善，图文完整”确认，故本机配置40MHz；通用ST7789V资料不保证此速率，其他板应单独验证。格式参见 `config/display.example.h`。
- `DISPLAY SPEED hz seconds` 仅开启 5～300 秒有界时钟实验，支持 8000000、16000000、20000000、26666667、32000000、40000000 Hz。到期自动回独立8MHz恢复档、重置控制器已知配置并恢复正常页面，恢复档不会随本机正常速率变化。实际硬件分频可能低于请求值，查看 DISPLAY-CLOCK/FRAME 日志。
- `DISPLAY FRAME` 不清屏、不重置，强制完整提交当前页面；`DISPLAY SAFE` 结束实验、以8MHz重新初始化控制器并恢复正常页面，重启才重新采用本机正常速率。超过16000像素的提交记录实际时钟、行范围、像素数和提交耗时。
- PlatformIO 串口监视器固定禁用 DTR/RTS，关闭监视器不会再复位 ESP32-S3、释放服务器租约或让屏幕回到 CONNECTING。
- USB Serial/JTAG 诊断输出使用 5ms 有限超时；服务器长期离线时即使没有打开串口监视器，日志缓冲满也只丢日志，绝不阻塞 Wi-Fi 重连与 HTTP 心跳任务。
- 未购买资产显示 PURCHASE PRICE 与黄色金额；已购买资产显示 OWNED BY、所有者名字、玩家席位色和建设状态。
- 顶部色带和图片边框始终显示资产所属地区颜色；include/tile_ui_theme.h 与玩家圆屏共享视觉 token 约定。
- 串口可打开诊断页、调节背光并查询当前状态。
- 上电时 RS485 DIR、ORDER_OUT 保持安全低电平。
- HTRC110 外部 4 MHz 时钟稳定 15 ms 后，正式固件立即三次尝试写入并读回
  `Config Page 1 TXDIS=1`；只有受保护的短时 UID 扫描才启用场，所有退出路径都重新验证关场。

当前界面使用 Adafruit GFX 内置 ASCII 字体和项目已有的完整 StreetV3 正式图片。服务器 DTO
已经接入 TileState；尚未内置的地图图片会显示 ARTWORK PENDING，但名称、类型、地区色、
价格和所有者仍可正常同步。中文字体和完整 40 格图片集合将在后续资源版本补齐。
## 构建与烧录

```powershell
pio test -d Firmware/TileModule -e native
pio run -d Firmware/TileModule -e tile_esp32s3
pio run -d Firmware/TileModule -e tile_esp32s3 -t upload
pio device monitor -d Firmware/TileModule -b 115200
```

目标硬件为 ESP32-S3-WROOM-1-N16R8；PlatformIO 已配置 16MB Flash、OPI PSRAM、
USB CDC、Arduino 框架、Adafruit GFX 和 Adafruit ST7789。

## 联网配置与状态

Wi-Fi 密码不得写入仓库。格子固件优先读取自己的 config/secrets.local.h；若不存在，
开发工作区会复用 Firmware/PlayerConsole/config/secrets.local.h 中已经配置且被 Git
忽略的 GRIDOPOLY_WIFI_UDP_SSID、GRIDOPOLY_WIFI_UDP_PASSWORD 和 channel。

屏幕左下角标签状态：

| 标签 | 含义 |
| --- | --- |
| `TAG SCANNING` | 正在等待首次稳定 UID |
| `TAG 8EFA259D` | 已确认一个 HITAG S256 32 位 UID（示例） |
| `TAGS 2  1/2 8EFA259D` | 同时检测到 2 个标签，当前显示第 1 个；每 1.5 秒轮播 |
| `TAG NO TAG` | 已确认的 UID 连续两次缺失，通常 0.5～1 秒内完成离场确认 |
| `TAG UNSTABLE` | 仅首次启动尚无稳定状态时可能显示；运行中不再覆盖上一组稳定 UID |
| `TAG RFID ERROR` | 数字接口、天线、电源或关场安全检查失败 |

屏幕右下角状态：

| 标签 | 含义 |
| --- | --- |
| WIFI | 正在连接专用热点 |
| SERVER | 已连 Wi-Fi，正在建立服务器心跳 |
| AUTO | 已由服务器自动认领最低空闲格 |
| MANUAL | 当前格来自网页手动分配 |
| NO TILE | 当前地图没有可认领的空闲格 |
| NET ERR | HTTP/JSON/服务器暂时不可用 |

串口 STATUS 会输出 moduleId、deviceId、HTTP 状态、RSSI、服务器 revision、当前分配、完整稳定 UID 集合以及 movement cue/player/revision。

## 标签相对强度与相邻格定位边界

HTRC110 没有可直接读取的连续 RSSI/dBm 寄存器。`READ_PHASE` 测得的是天线谐振相位，
用于选择解调采样时间；`AMPCOMP` 只能比较当前解调幅度是否高于芯片先前保存的一个参考值。
当前 PCB 也只把 HTRC110 的 `SCLK`、`DIN`、`DOUT` 数字接口连接到 ESP32，没有模拟幅度 ADC 通道，
因此正式固件不得把相位值、功耗或单次成功读取伪装成真实信号强度。

后续相邻格定位可以实现一个经过实机校准的 `qualityScore`，但它表示“本格读取可信度”，不是物理 RSSI：

- 在 HTRC110 的四档可编程接收增益下重复只读 UID；越低增益仍能稳定解码，通常表示耦合越强。
- 合并同一测量轮次的有效 UID 比例、可用采样相位宽度和 AC2K 脉冲时序抖动，并按每块板的中心/边缘样本归一化。
- 相邻线圈必须由服务器分时开启，不能同时持续发 125kHz 场；服务器只比较同一轮、同一 UID 的报告。
- 服务器使用领先分差和连续多轮滞回选择获胜格；接近边界且分数相近时上报 `AMBIGUOUS`，不得来回跳格。

若需要可跨模块校准的连续模拟幅度，下一版 PCB 应从安全分压后的接收路径增加缓冲、包络/峰值保持、
钳位保护和 ESP32 ADC 输入，或改用带 RSSI 输出的读卡前端。不得把 `ANT_TAP_HV` 直接接到 ESP32 ADC。
## 图片资源

当前固件内置 36 张 `Assets/GridCity/StreetV3/device/` 的 160×160 PNG，经构建工具
转换为 RGB565 并存入 Flash，不依赖 SD 卡或文件系统。源图片更新后运行：

```powershell
python Firmware/TileModule/tools/generate_tile_artwork.py
```

生成文件位于 `Firmware/TileModule/src/assets/`，每个文件带源图 SHA-256，便于确认
固件图片与项目美术资源一致。
## 串口命令

| 命令 | 作用 |
| --- | --- |
| `DIAG` | 打开或关闭硬件诊断页 |
| `REDRAW` | 重新初始化 ST7789 并强制重绘当前权威页面 |
| `RESTART` | 软件重启 ESP32-S3，用于完整启动时序验证 |
| `BRIGHTNESS 0..255` | 调节屏幕背光 PWM |
| `STATUS` | 输出当前格子状态和功耗 |
| `HELP` | 显示命令帮助 |
| `RFID DEBUG ON` | 输出原始边沿数、SOF 起点、碰撞位、采样相位和 AC 分支结果 |
| `RFID DEBUG OFF` | 关闭原始 RFID 诊断输出 |

## 实机验收重点

1. 上电后先显示 CONNECTING / WI-FI，不得短暂显示 Rivet Row 或其他本地地产。
2. Wi-Fi 成功后显示 WI-FI CONNECTED / REGISTERING；服务器无分配时显示 SERVER ONLINE / WAITING FOR TILE。
3. 服务器返回真实 assignment 后才切换资产页，且图片、价格、所有者与分配一致。
4. 服务器返回 `departure` 时 10 颗 LED 同步橙色呼吸；返回 `destination` 时同步绿色双闪，且不出现单颗玩家槽位灯。
5. LED 各颜色通道不超过约 25%，USB 调试时不进行全白满载。
6. 屏幕底部功耗值稳定刷新，切换 LED 场景时数值变化合理。
7. `DIAG` 页面显示 ST7789、INA226、WS2812 状态，并确认 RS485/ORDER 保持安全。
8. 同时放入两枚 HITAG S256 时，`STATUS` 连续至少 60 秒保持两个去重 UID；短暂弱帧不得让底栏跳成一个或零个标签。
9. 稳定集合变化后不等待常规周期即可在服务器全局 Tag 列表看到更新；把已绑定玩家 Tag 放到目标格后只自动确认一次，到达后 cue 回到 `none`。
10. `scanning`、`fault` 或 `overflow=true` 不得触发自动到达；玩家屏的 `I'M THERE` 仍可完成相同流程。

## 暂不包含

- RS485 格子总线协议。Wi-Fi/HTTP 当前承担首版服务器数据源。
- ORDER 物理顺序枚举。
- 相邻格同时读到同一 UID 时的强度仲裁；当前只有目标格的稳定完整报告参与到达判断。
- 中文字体、完整 40 格图片集合和运行时图片资源传输。

后续正式接入主控时，将 RS485 解码后的数据写入 `TileState`，显示和 LED 渲染继续
使用当前状态模型。
