# Gridopoly 圆形旋钮玩家终端当前实现基线

状态：A 级当前实现规范

更新日期：2026-09-08

当前工作树包含尚未完成实机验收的候选改动；本页的性能数值是验收要求，不能当作已通过的测量结果。COM7 已恢复旧生产固件，新 Action 17 候选尚未部署。构建、测试及设备状态以 [开发进度记录](../firmware/player-console-development-progress-2026-09-07.md) 为准。

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

- 模组：VIEWESMART/Viewe `UEDX48480021-MD80ET`。
- 主控：ESP32-S3-WROOM-1-N16R8，16MB Flash、8MB Octal PSRAM。
- 显示：2.1 英寸 480×480 圆形 LCD。
- 输入：旋转外圈、旋钮按压、电容触摸。
- 调试：外置 USB 调试转接板，正式构建使用 USB HWCDC。
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
`hardware_input.cpp` 边界反转一次：左沿事件映射为 `Rotate +1`，右沿事件映射为
`Rotate -1`。页面、列表和数值编辑不得再二次反转。

旋转输入使用 32 项固定队列；同方向增量可合并，但 UI 每次只消费一个 detent，保证快速
旋转不丢步骤，也不把相邻预设乱序应用。

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

- 正式构建使用 vendor direct anti-tearing mode 3：
  `CONFIG_LVGL_PORT_AVOID_TEARING_MODE=3`。
- LVGL 使用 2 个显示缓冲。
- RGB DMA bounce buffer 为 `480×40` 像素并放在内部 SRAM，降低 Wi-Fi、PSRAM 解码和
  RGB 扫描同时发生时的左侧点状残影、行漂移和频闪。
- UI 帧间隔为 33ms，目标约 30 FPS；持续帧率不得低于 24 FPS。
- 首页轮盘 220ms 过渡的相邻有效帧间隔不得持续超过 42ms；首帧反馈目标小于 80ms。
- 场景切换或显存恢复时允许整屏 invalidate；后台素材完成只允许重建当前实际引用该素材的
  页面，不能因无关缓存完成反复整屏刷新。
- 静态弹窗更新必须幂等。样式、边框和几何未变化时，只更新真正变化的倒计时、进度或文字。

抗频闪不能只靠一次照片验收。发布前必须覆盖长时间静置、连续动画、Wi-Fi 下载、PSRAM
解码和弹窗长按等组合负载，并确认左侧圆弧没有闪烁短线、点阵或残影。

## 6. 正式通信

- 默认传输：树莓派专用热点上的认证 Wi-Fi/UDP。
- AP：`gridopoly`；服务地址 `10.42.0.1:4242`。口令只保存在 Git 忽略的本地 secrets，
  不得写入文档或提交。
- 一个 UDP datagram 承载一个现有 Gridopoly 二进制协议帧。
- 会话使用截断 HMAC-SHA256、64 位 packet sequence 和 64 包重放窗口。
- Heartbeat 周期 2 秒。
- 9 秒没有任何合法服务端帧：禁用权威动作并在所有页面最上层显示红色断线图标。
- 15 秒仍未恢复：丢弃旧会话，重新发现、配对并恢复原席位。
- 同一 request ID 的重发必须保持业务 payload 不变；权威服务端负责幂等和只执行一次。

ESP-NOW 仅保留为编译期回退；正式双向交易和当前身份流程以 Raspberry Pi Wi-Fi/UDP
服务端为验收对象。SelfTest 使用 `DemoTransport`，无需无线对端。

## 7. 当前页面与规则边界

- 等待首页只有 `ASSETS`、`PLAYERS`、`TRADE`；没有 `MORE`。
- 本人回合按权威阶段插入 `DICE`、`ROLL AGAIN` 或 `END TURN` 主操作。
- 掷骰只负责表现；实体棋子由玩家移动。到达页始终保留手动 `I'M THERE`，即使只有一个格子模块、
  RFID 未接入或 Tag 尚未绑定也不得隐藏、禁用。
- Tag 绑定只在服务端网页完成，圆屏不提供绑定入口，也不根据本地 Tag 或格子数据猜位置。
  RFID 自动到达和手动确认都必须由服务端推进同一个权威阶段；圆屏先显示
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
- 当前候选临时池上限 2304KiB；30 个压缩组件共 1,921,970B，另有两张 220×300 RGB565
  预览，每张 132,000B，用于合成时保留上一张完整预览。身份设置期间组件不做 LRU 淘汰。
- 素材达到 30/30 后必须自动进入 Avatar Setup；初始配方必须立即合成，不能等待用户先旋转
  一次预设才显示。
- 外貌、姓名、准备和倒计时共用该临时池；进入 Active 后统一释放。

## 9. 身份、头像和姓名

正式生命周期为：

```text
AvatarLoading -> AvatarSetup -> NameReview <-> NameHandwriting
              -> PlayerReady -> Countdown -> Active
```

Avatar Setup 使用 B. Focus Stack：左侧 5 个贴合圆弧的固定槽位和确认按钮，右侧是静态
半身预览，不使用圆形头像框。确认外貌后服务端只生成一次最终公共头像；Ready、Player Detail、
交易接收者、付款和活动页面都使用该版本化头像。

姓名手写当前规则：

- 只采集手写区域内的物理触摸点，容量 1024 点。
- 同一次接触中，相邻采样间隔不超过 100ms 时连接成线；不同接触不自动融合。
- 只要最后一次触摸距现在不足 1200ms，就保留画板并禁止识别。
- 画板有内容且连续 1200ms 没有触摸后才进入识别。
- 主识别器是本地 int8 EMNIST 神经网络，输入 28×28；低置信或歧义结果使用 28×40
  笔画模板回退。

## 10. 构建与烧录

在仓库根目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File Firmware/PlayerConsole/tools/compile.ps1 -SelfTest
powershell -ExecutionPolicy Bypass -File Firmware/PlayerConsole/tools/compile.ps1
powershell -ExecutionPolicy Bypass -File Firmware/PlayerConsole/tools/upload.ps1 -Port COMx
```

正式 FQBN 使用 16MB Flash、`app3M_fat9M_16MB`、Octal PSRAM、CDC on boot 和 HWCDC。
编译脚本会先执行字形、布局、棋盘素材检查，再编译并校验输出。发布时必须记录固件 Git
提交、构建尺寸、资源 manifest、端口、实机照片和服务端版本。

## 11. 发布前快速核对

1. 定位柱在六点钟，文字水平；诊断显示 mechanical 60°、firmware 0°。
2. 外环和内环上下左右间距一致，几何中心为 `(240,240)`。
3. 顺、逆时针连续旋转时，预设严格单调前进或后退，不出现 1,2,3,4,3 的回跳。
4. 短按、800ms 返回、1200ms 确认、3000ms Home/Demo Lab 均只触发一次。
5. Wi-Fi/PSRAM 压力、静置和弹窗动画下无左侧点状残影或频闪。
6. 新房间从 0/30 自动进入 Avatar Setup，初始预览无需手动切换即可出现。
7. 进入 Active 后身份临时池释放；棋盘 LRU 和最终头像仍在 1MiB 游戏期预算内。
8. 断开 AP 后 9 秒显示全局图标，恢复后自动消失；15 秒后可重新配对原席位。
