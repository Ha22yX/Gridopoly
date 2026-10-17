# Gridopoly 圆形旋钮玩家终端当前实现基线

状态：A 级当前实现规范。更新：2026-10-17；源码、已写入版本和现场验收分别记录。

冻结组合包仍为`trial16-2026-10-15-v4`，未回写后续更新。完整身份、服务器快照及原始分批记录见[设备台账](../firmware/current-device-connections.md)。最新实机工作对应10月16日晚EDT（原始日志10月17日UTC）；用户现让三台屏充电，先继续软件验证，暂停全部设备操作。

| 设备 | 完整 MAC | 最后实际应用 SHA-256 前缀 | 遥测及当前问题 |
| --- | --- | --- | --- |
| COM7 | dc:b4:d9:02:d1:dc | 22ca08e6…，16MHz | 电量关闭，本轮未升级 |
| COM27 | dc:b4:d9:02:bf:88 | 8ef6c051…，16MHz | 首版电量/建房入口；有效电压/SOC，偶发超时恢复 |
| COM28 | dc:b4:d9:07:81:b0 | 8ef6c051…，16MHz | 首版；用户报告手写卡顿、连笔、位置偏差，未闭合 |
| COM29 | 20:6e:f1:ba:c9:04 | df9f1282…，12MHz | 第二版独立电量采样；写验通过，最新120秒零有效SOC，尚未修复 |

最新服务器快照03:49:48 UTC为room993580116/version47，COM28/29/27席位1/2/3、三真人加一机器人，仍身份设置阶段；不代表充电时实时在线或完成整局。COM29首版POWER关开恢复电量属于旧窗口。当前二版运行任务间隔最大54ms仍失联，不能单用主循环卡顿解释。

当前源码保留core1/mode3、原显示时钟、输入/等待页优化及开发图片，并增加首页建房入口和底部电量。独立UART任务已主机回归，只有COM29安装二版。新增SC18兼容初始化避开07/08/09写入，并以输入模式寄存器检测复位；手写有界诊断已完成25项及真实输入/手写116项回归，16MHz联合候选91617134已构建核验、未部署，具体部署以[本轮记录](../firmware/player-build-battery-rollout-2026-10-16.md)为准。不能把候选构建或旧等待页视觉通过当作当前手写与电量验收。

COM27万用表3.640V与约3.646V遥测接近，仅支持电压通路，不认证SOC精度；未知数据必须显示未知。精确旋钮、Avatar/游戏页、≥24FPS与最大42ms、完整四真人实体对局及电气检查仍各自保留；9月确认只对应当时版本与场景。[电量接口](../hardware/player-battery-telemetry-interface-2026-09-27.md)、[兼容修改](../firmware/player-battery-bridge-compat-2026-10-17.md)、[手写调查](../firmware/player-handwriting-com28-2026-10-16.md)。

## 1. 适用范围

本文记录当前已实现并用于实机联调的 2.1 英寸圆形玩家终端。它不是单格模块上的
240×320 ST7789 屏幕，也不使用单格模块的 RFID、INA226、WS2812 或 RS485 引脚。

发生冲突时，按以下单一来源判断：

| 范围 | 单一来源 |
| --- | --- |
| 板级方向、按键和构建常量 | `Firmware/PlayerConsole/app_config.h`、`PlayerConsole.ino` |
| 页面几何 | `Firmware/PlayerConsole/ui_layout.h` |
| UI 行为 | [玩家圆屏 UI 规范](player-console-ui-spec.md) |
| Wi-Fi/UDP 帧和身份流程 | [玩家屏 Wi-Fi/UDP 协议](../firmware/wifi-udp-player-protocol.md) |
| 头像组件 | [头像组件协议](../firmware/avatar-component-protocol.md) |
| 游戏规则和合法动作 | `GridopolyCore`、树莓派权威服务端 |

## 2. 当前硬件

供电底板设计、返修和制造状态由PCB会话维护，见[硬件入口](../hardware/README.md)及[玩家屏USB-C电池底板](../hardware/player-usbc-battery-base-2026-09-27.md)。本页不复制旧RC5审查结论。COM7/27/28所用底板已获用户返修确认并带电池完成USB升级；COM29也获用户返修、接电池确认，其静止等待页在12MHz后获正常反馈。这些反馈不能替代充电、欠压和温升验收。软件接口与启用边界见[UART接口](../hardware/player-battery-telemetry-interface-2026-09-27.md)及[遥测实现记录](../firmware/player-battery-telemetry-progress-2026-09-27.md)。

- 模组：VIEWESMART/Viewe `UEDX48480021-MD80ET`。
- 主控：ESP32-S3-WROOM-1-N16R8，16MB Flash、8MB Octal PSRAM。
- 显示：2.1 英寸 480×480 圆形 LCD。
- 输入：旋转外圈、旋钮按压、电容触摸。
- 调试：正式构建使用原生 USB HWCDC；现有返修底板 USB 通路已用于升级，外置转接板不是必需的固定配置。
- 板级宏：`BOARD_VIEWE_UEDX48480021_MD80ET`。

## 3. 物理方向和坐标

当前底座把完整连接面定位图案顺时针旋转 60°，使标记的第三根 M4 对位柱准确位于
六点钟方向。机械件已经完成补偿，固件不得再做 50°、60°或其他任意角旋转。

| 项目 | 当前值 |
| --- | --- |
| 机械补偿 | 顺时针 60° |
| 固件旋转 | 0°，面板原生方向 |
| 逻辑与物理画布 | 480×480 |
| 圆心 | `(240,240)` |
| 主内容安全圆 | 直径 384 px |
| 外环 | `{x=31,y=31,w=418,h=418}` |
| 内环 | `{x=44,y=44,w=392,h=392}` |

外环和内环的几何中心都必须是 `(240,240)`。旧版为了补偿安装误差加入的 Y 方向
`+8 px` 已废止。触摸坐标使用同一 480×480 正向坐标；镜像或轴交换只能在板级驱动中
处理一次，页面层不做角度变换。

## 4. 旋钮、按压和触摸

### 4.1 引脚与方向

| 功能 | GPIO |
| --- | --- |
| 旋钮 A | GPIO6 |
| 旋钮 B | GPIO5 |
| 按压 | GPIO0 |

安装后的物理手感要求顺时针和逆时针焦点移动与玩家视角一致。当前电气方向只在
硬件输入边界的 `RotaryQuadratureDecoder` 中反转一次。页面、列表和数值编辑不得再二次反转，也不使用单向冷却来掩盖反跳。

`hardware_input.cpp` 以 1ms 定时器联合读取 A/B；解码器保留完整合法路径确定的 00/11 可信定位点，非法双相跳变及半程恢复不得重置定位点并产生反向假步。32 项固定队列只合并相邻同方向旋转，不跨反向或按键合并；poll 将合并项拆为 ±1。`input_dispatch.h` 每轮最多顺序消费 16 个事件，遇 `ButtonUp` 提前结束该批，让页面和手写先处理释放结果。有输入积压时暂缓长按判定，避免尚未消费的释放被误判成长按。

合并项保留首项时间戳，拆出的步共享该时间戳，并非每个物理卡点各有独立时间。队列有限，不能保证任意速率无丢失。10 月 15 日粗略四组共 24 个解码步与消费对应，不替代精确物理卡点验收。周期身份快照保留同页本地焦点；见[输入修复报告](../firmware/player-16-trial-progress-2026-10-15.md)及[历史解码/手写修复](../firmware/player-input-handwriting-fix-2026-09-26.md)。

### 4.2 按压时长

| 时长 | 语义 |
| --- | --- |
| 0 至 499ms | 短按，激活当前焦点 |
| 500 至 799ms | 模糊区，不执行短按或返回 |
| 800ms | 返回一级 |
| 3000ms | 首页进入 Demo Lab；子页返回首页 |
| 1200ms | 危险、付款或不可逆操作长按确认 |

触摸和旋钮共享唯一焦点模型。触摸某项后，旋钮必须从该项继续；编辑数值时第一次短按
进入编辑、旋转改值、再次短按退出编辑。

## 5. 显示和抗频闪基线

- 正式构建的 LVGL 任务运行在 core1，使用 vendor direct anti-tearing mode 3：
  `CONFIG_LVGL_PORT_AVOID_TEARING_MODE=3`。
- LVGL 使用 2 个显示缓冲。
- RGB DMA bounce buffer 为 `480×40` 像素并放在内部 SRAM，降低 Wi-Fi、PSRAM 解码和
  RGB 扫描同时发生时的左侧点状残影、行漂移和频闪。
- 当前 `LV_DISP_DEF_REFR_PERIOD=30ms` 是LVGL刷新定时器配置，不是实测帧间隔；动画目标30 FPS，持续帧率不得低于24 FPS。COM7/27/28请求PCLK为16MHz，COM29当前为12MHz。
- 首页轮盘 220ms 过渡的相邻有效帧间隔不得持续超过 42ms；首帧反馈目标小于 80ms。
- 场景切换或显存恢复时允许整屏 invalidate；后台素材完成只允许重建当前实际引用该素材的
  页面，不能因无关缓存完成反复整屏刷新。
- 静态弹窗更新必须幂等。样式、边框和几何未变化时，只更新真正变化的倒计时、进度或文字。
- 重复 Connecting/Waiting 事件仍应用到状态机；`WaitingForGame` 可见内容相同时保留控件，不重复整屏重建。首次进入、真实断线/同步、遮罩或素材变化仍需更新。
- Avatar 只移动焦点时更新旧/新行，以及进入或离开确认按钮时的样式，不重新获取或重绘头像。

上述帧率和延迟是验收门槛，不是当前全部通过的实测值。COM27/28仅dfc4等待新局页获目视确认，COM7配对页稳定；COM29旧dfc4/16MHz失败，716ff623/12MHz静止等待页目视通过。COM29动态页面、24FPS及42ms门槛仍待测；core1历史Retarget max57ms尚未关闭，core0历史性能PASS已随显示异常方案撤回。

当前仍启用LVGL性能叠层（`LV_USE_PERF_MONITOR=1`），FPS/CPU标签约每300ms更新，因此等待页控件保持不变也可能出现flush。COM29有界诊断中的frame-complete频率是硬件回调计数，不是LCD可见FPS或欠载计数；60次重试的主机回归只证明未新增页面重建/脏区，不覆盖持续运行的性能叠层定时器。

抗频闪不能只靠一次照片验收。发布前必须覆盖长时间静置、连续动画、Wi-Fi 下载、PSRAM
解码和弹窗长按等组合负载，并确认左侧圆弧没有闪烁短线、点阵或残影。

## 6. 正式通信

- 默认传输：树莓派USB独立热点上的认证 Wi-Fi/UDP；当前ALFA AWUS036ACM使用 `gpusb0`、2.4GHz/US/信道6/HT20。
- AP：`gridopoly`；服务地址 `10.42.0.1:4242`。口令只保存在 Git 忽略的本地 secrets，
  不得写入文档或提交。
- 一个 UDP datagram 承载一个现有 Gridopoly 二进制协议帧。
- 会话使用截断 HMAC-SHA256、64 位 packet sequence 和 64 包重放窗口。
- Heartbeat 周期 2 秒。
- 9 秒没有任何合法服务端帧：禁用权威动作并在所有页面最上层显示红色断线图标。
- 15 秒仍未恢复：丢弃旧会话，重新发现、配对并恢复原席位。
- 同一 request ID 的重发必须保持业务 payload 不变；权威服务端负责幂等和只执行一次。

2026-10-15 22:07:58及22:18:49 UTC两次快照均确认16格＋4屏共20台关联/认证/注册。随后COM7由用户关闭，Pi软件重启后按16格＋COM27/28/29持续观察；唯一16节点环、自动映射及reader stable已取得证据。22:47 UTC三真人Tag配对已完成。旧7关联瓶颈、16环和基本RFID不再列为当前阻塞；未来36台、无家庭网断电冷启动、四真人完整身份流程和实体整局仍待验，见[USB部署](../firmware/usb-ap-deployment-2026-10-15.md)及[当前交接](../CURRENT-DEVELOPMENT-HANDOFF.md)。

ESP-NOW 仅保留为编译期回退；正式双向交易和当前身份流程以 Raspberry Pi Wi-Fi/UDP
服务端为验收对象。SelfTest 使用 `DemoTransport`，无需无线对端。

## 7. 当前页面与规则边界

- 等待首页只有 `ASSETS`、`PLAYERS`、`TRADE`；没有 `MORE`。
- 本人回合按权威阶段插入 `DICE`、`ROLL AGAIN` 或 `END TURN` 主操作。
- 掷骰只负责表现；实体棋子由玩家移动。到达页始终保留手动 `I'M THERE`，即使只有一个格子模块、
  RFID 未接入或 Tag 尚未绑定也不得隐藏、禁用。
- 自动新局由服务端逐人主持Tag配对，圆屏显示指定reader、放入/拿回及异常，可提交受权限和版本约束的重试/取消；不在本地决定绑定或猜位置。成功绑定者可先设置外貌/姓名，其余玩家继续配对。游戏移动中的
  RFID自动到达和手动确认仍由服务端推进同一个权威阶段；圆屏先显示
  `TILE ARRIVAL CONFIRMED`，随后进入完全相同的购买、债务、拍卖、抽卡或回合结束流程。
- 购买、拍卖、Chance、Community Chest、租金、税费、债务、抵押、建房、卖房和破产
  都由服务端决定合法动作；圆屏不自行修改现金、位置或所有权。
- 玩家详情按需查询位置、现金、最多 28 个资产和最近 10 条财务记录，不做高频订阅。
- 被动活动列表保留最近 20 条，不抢占当前页面；新活动替换横幅，旧活动可在 `ACT` 查看。
- 拍卖介绍对同一 `room + generation + asset` 只播放一次 1.8 秒，随后固定留在 Live 舞台。
- Chance/Community Chest 使用两阶段 CardDrawn、Continue、EffectApplied 契约。
- 现金不足进入无返回按钮的强制筹资页：先按权威规则卖建筑，再抵押空地；只有服务端
  开放 `DeclareBankruptcy` 时才显示破产。

## 8. 素材和缓存

### 8.1 游戏期 1MiB 素材预算

| 缓存 | 预算 | 策略 |
| --- | --- | --- |
| 棋盘格图片 | 640KiB | 最多 20 张 128×128 RGB565，LRU 淘汰 |
| 最终玩家头像 | 384KiB | 最多 6 张 128×128 RGB565 |

骰子目标确定后立即预取目标格图片。未加载完成时显示 loading ring，不显示无关占位图。

### 8.2 身份设置临时池

- 进入身份流程先显示统一 `PREPARING AVATAR` 页面。
- 当前配方三层优先，4 个低优先级 HTTP worker 预载全部 30 个中性 GAVC 组件。
- Hair 10、Hair Color 20、Face 10、Skin Tone 8、Outfit 10。
- 组件合成顺序固定为 `face -> outfit -> hair`；发色和肤色使用 canonical 整数算法在本地
  着色，不为每种颜色下载位图。
- 当前实现临时池上限 2304KiB；30 个压缩组件共 1,921,970B，另有两张 220×300 RGB565
  预览，每张 132,000B，用于合成时保留上一张完整预览。身份设置期间组件不做 LRU 淘汰。
- 30 个组件校验齐全后准备状态保持完成并自动进入 Avatar Setup，初始合成自动请求；首次预览可以稍后完成，不能依赖用户旋转才能出现。
- 配方变化后后台等待 45ms 稳定期再合成完整预览。双缓冲的前台图和待交付完整图不可被 worker 改写；LVGL 所属任务取得完成图后才切换显示。无完整图时显示 `LOADING PREVIEW` 和转圈；已有完整图时保留它并标注 `UPDATING PREVIEW`，不能把旧图冒充当前配方。
- 外貌、姓名、准备和倒计时共用该临时池；进入 Active、LVGL 不再引用临时图后统一释放。

## 9. 身份、头像和姓名

正式生命周期为：

```text
WaitingForServer/NewGame -> TagPairing -> AvatarLoading -> AvatarSetup
                                    -> NameReview <-> NameHandwriting
                                    -> PlayerReady -> Countdown -> Active
```

Avatar Setup 使用 B. Focus Stack：左侧 5 个贴合圆弧的固定槽位和确认按钮，右侧是静态
半身预览，不使用圆形头像框。每次合法外貌确认由服务端生成对应版本的公共头像，同请求重发保持幂等；姓名确认前 Name Review 可返回修改外貌。Ready、Player Detail、
交易接收者、付款和活动页面都使用该版本化头像。

姓名手写当前规则：

- 只采集手写区域内的物理触摸点，容量 1024 点。
- 从真实触摸驱动记录接触边界；同一次接触不因UI处理间隔超过100ms而断笔，真实抬起、离开书写区或不可用采样中断接触。短暂读失败不冒充可靠抬笔或触发提交。
- 只要最后一次触摸距现在不足 1200ms，就保留画板并禁止识别。
- 画板有内容，真实抬笔且连续1200ms静默后才提交后台识别；同页Tag/Identity快照保留画布。DONE等待最后墨迹结果后离页，DELETE优先清墨迹/撤销旧识别，空画布才删最后字符；拒识保留墨迹供补写。
- 识别先保留可信通用几何/笔画匹配，其余使用本地int8 EMNIST 28×28网络及28×40模板回退；未更换网络权重或0.45门槛。

## 10. 构建与烧录

在仓库根目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File Firmware/PlayerConsole/tools/compile-isolated.ps1 -Mode selftest -BatteryBoard none
powershell -ExecutionPolicy Bypass -File Firmware/PlayerConsole/tools/compile-isolated.ps1 -Mode production -BatteryBoard none
```

COM29已通过静止等待页目视的12MHz配置需显式选择，保留电量功能的后续普通构建使用：

```powershell
powershell -ExecutionPolicy Bypass -File Firmware/PlayerConsole/tools/compile-isolated.ps1 -Mode production -BatteryBoard viewe-md80et-v10 -DisplayClock pclk12 -DisplayDiagnostic none
```

默认 `DisplayClock=vendor` 仍是板型16MHz；`pclk12` 目前只验过COM29静止等待页，不代表所有页面或性能已通过。`DisplayDiagnostic=counters` 只用于120秒有界采样，其候选会被普通批量发布工具拒绝。`13e4de40` 已将时钟与诊断独立配置；旧普通12MHz候选3da4c493未单独部署；当前COM29已改为带电量二版df9f1282（正常生产、显示诊断关闭）。保留12MHz不等于该版电量或手写已验收。旧冻结v4仍为16MHz，不能当作包含本次COM29时钟调整的包；精确候选及部署见[COM29显示记录](../firmware/player-com29-display-progress-2026-10-15.md)。

既有验证包括诊断窗口15/15、真实渲染/输入51/51和时钟/诊断配置18/18；这些是对应源码、构建及主机验证，不能代替普通候选写验、动态页面目视或性能验收。本段旧测试次数不覆盖10月16日后电量/手写变更；最新软件回归见对应专项报告。

上述命令要求已配置的本机 ToolRoot，默认使用 esp32:esp32 3.3.11。脚本为每次构建冻结源码和依赖，输出独立候选及校验信息；构建完成不表示已经部署。正式 FQBN 使用 16MB Flash、`app3M_fat9M_16MB`、Octal PSRAM、CDC on boot 和 HWCDC。

批量写入以[试玩包记录](../firmware/trial16-release-2026-10-15.md)中的冻结产物、release 校验和固定工具流程为准，先确认完整 MAC、Flash 和分区，再写验并观察正常应用。既有布局升级与原厂首次迁移不同：COM27/28/29 首次迁移先完整备份，再初始化目标 20KiB NVS，不能称为保留全部原厂 NVS。不要用通用 COMx 示例绕过这些检查，也不要直接重刷旧的电池启用候选。记录源码/产物哈希、端口/MAC、服务端版本及实际页面证据。

## 11. 发布前快速核对

以下是门槛清单，不是全部 PASS 的声明。已通过和待验证项分别见[验收矩阵](player-console-acceptance-tests.md)末尾的当前状态表。

1. 定位柱在六点钟，文字水平；诊断显示 mechanical 60°、firmware 0°。
2. 外环和内环上下左右间距一致，几何中心为 `(240,240)`。
3. 顺、逆时针连续旋转时，预设严格单调前进或后退，不出现 1,2,3,4,3 的回跳。
4. 短按、800ms 返回、1200ms 确认、3000ms Home/Demo Lab 均只触发一次。
5. Wi-Fi/PSRAM 压力、静置和弹窗动画下无左侧点状残影或频闪。
6. 新房间从 0/30 自动进入 Avatar Setup，初始预览无需手动切换即可出现。
7. 进入 Active 后身份临时池释放；棋盘 LRU 和最终头像仍在 1MiB 游戏期预算内。
8. 断开 AP 后 9 秒显示全局图标，恢复后自动消失；15 秒后可重新配对原席位。
