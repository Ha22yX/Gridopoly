# 玩家屏 Home 建房入口与底板电量候选，2026-10-16

本轮范围为 `Firmware/PlayerConsole` 和本文。原理图、PCB及电气验收由PCB设计会话负责；串口身份、刷写、实际电量验收、共享文档与Git提交由项目总指挥负责。本会话未操作串口、设备、线上游戏或Git索引。

## 功能与状态边界

Home 在本人的稳定 `AwaitRoll` / `TurnEnd` 阶段、当前位置的地产归自己且完整同色组归自己时增加 BUILD。取值来自已确认的棋盘位置与完整资产投影，不使用移动目标或会被 `clearRollFlow` 清空的到达演出标记。Home 的数量、绘制和输入共同使用 `appHomeActions`，新增/移除入口时按动作身份保留当前焦点和导航栈内的 Home 焦点。

紧凑快照和完整快照必须具有与当前状态相同的版本，才允许提交。在房间、棋盘、玩家、位置和行动玩家没有变化的短暂分包期间，保留上一份完整投影所确定的 Home 入口，避免 BUILD 突然移除后误选 ASSETS；按钮等待两种快照齐全后恢复。同上下文 resync 保留页面与导航。离线保留建房页并禁用动作；已确认的换房间、换棋盘、换位置、换回合或失去整组所有权会退出失效的建房上下文。

入口默认选中当前位置的地产，可用属性选择器在同色组内切换以满足均匀建造。页面显示当前等级→下一等级、单级费用、现金前后和租金前后；四房到旅馆、旅馆满级、现金不足、抵押、均匀建造、银行缺房/旅馆、等待状态、离线和请求待确认均有明确文本。每次仅提交一个现有 `BuildRequest`，pending 期间防重，不在本地预扣现金或预加建筑。Assets 原有 Build 动作共用相同校验；其既有稳定阶段规则保留，Home 快捷入口额外要求本人回合与当前位置条件。

建房页面沿用已提交的房屋/旅馆按需图片路径。源码不更改服务器规则、协议、共享库、手写、旋钮解码、LVGL绑核、mode3双缓冲或40行bounce。当前仍为core1；COM27/28维持vendor默认16MHz，COM29维持12MHz。候选是普通Wi-Fi production，`DisplayDiagnostic=none`，不运行SelfTest；已有SelfTest中的资产夹具补齐新增权威版本字段，但本轮没有实板执行整套SelfTest。

## 底板电量与诊断

两份候选均显式使用 `BatteryBoard=viewe-md80et-v10`，编译宏 `-DGRIDOPOLY_BATTERY_BOARD=1`。构建规则允许普通production的pclk12与电池组合，继续拒绝电池与显示诊断counters混用。候选清单新增直接记录 `batteryBoard`，与 `inputs.json` / 源码清单一致。

复用原有UART1 9600、TX43/RX44遥测实现与独立底部电量条，增加MODE、CONFIG回读和原始GPIO到状态记录。数据来自底板电量计，百分比仍是电量计估算值；读取失败/过期显示未知，USB插入或CHG未激活都不会被当成100%。外部电源和充电指示与SOC分别表示，CHG也不等于测得电池净充电电流。具体板级连接和极性验收由PCB设计会话及主会话实测负责。

`BatteryDiagnostics` 在启动后120秒内输出，首样本约1秒，随后间隔5秒，最多24组。每组固定5行，以同一 `id` 关联，每行最多63字节。每次tick仅在USB缓冲能容纳完整一行时一次写出；不分段续写半行、不flush、不等待、不分配动态内存。阻塞样本1秒后丢弃，启动窗口结束后不重开。该有界电量日志与显示性能诊断开关是两个不同功能。

| 行 | 字段 | 含义 |
| --- | --- | --- |
| 1 | `GP_BAT id ms link mv_ok io_ok e i` | 采样时刻、链路就绪、测量/GPIO有效、错误枚举、I²C状态 |
| 2 | `GP_BAT id rv rs mv pct age` | 原始电压/SOC、毫伏、整数百分比、测量年龄ms |
| 3 | `GP_BAT id gpio b0 b1 age` | GPIO原值、bit0/bit1、GPIO年龄ms |
| 4 | `GP_BAT id bridge gauge` | 桥接版本字符串、电量计版本 |
| 5 | `GP_BAT id mode config` | MODE和CONFIG实际回读值 |

`e` 对应 `BatteryError`：0=None、1=Timeout、2=Framing、3=BridgeVersion、4=BridgeConfig、5=GaugeVersion、6=I2cStatus、7=GaugeConfig、8=Voltage、9=TxIncomplete。原始寄存器和I²C状态字段为十六进制。判断百分比时必须同时检查 `link/mv_ok` 和年龄；原始寄存器保留值不能独立证明本次数据有效。测量超过3000ms、GPIO超过350ms会失效。硬件板型、供电/充电/欠压保护与真实百分比精度不由日志格式或主机测试认证。

## 验证与可复现证据

本机证据根目录为 `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/build-battery-20261016/`，不纳入Git。

| 验证 | 结果 | 文件 |
| --- | --- | --- |
| 真实LVGL输入与状态reducer | 109项通过，0失败 | `runtime-final.log` |
| 电池协议/错误/过期/原子日志 | 52/52通过 | `telemetry-build.log` |
| 真实LVGL页面与电量条 | 21/21通过 | `battery-ui-final.log` |
| 构建参数组合 | 22/22通过 | `display-profile-final.log` |

操作回归保留原71项并覆盖Home动作统一、当前地产默认值、同组选择、两种快照到达顺序、短暂分包与resync的焦点/导航栈、待确认去重、不乐观更新、现金/均匀建造/抵押/库存/满级、离线、换回合/位置/房间/棋盘和Assets共同校验。电量回归包含原44项及实际读回字段、24组×5行/行长、拥塞不写半行、过期丢弃、计时回绕与禁用零输出。

界面测试覆盖原31页以及新增Build页，保留手写画布、输入焦点和独立状态层。使用当前真实生产renderer、LVGL字体与发布RGB565像素生成以下四张图，并逐张目视检查；这些是主机渲染图，不是实板照片：

- `ui/battery-build-house.png`：0房→1房和底部78%/EXT/CHG。
- `ui/battery-build-hotel.png`：4房→旅馆。
- `ui/battery-build-even.png`：真实L1图片和均匀建造禁用说明。
- `ui/battery-build-maximum.png`：旅馆满级，费用0，不再提交。

旧版预览夹具曾将L1映射为L0，已改成真实L1像素并重新跑UI；最终以上PNG不再使用该替身。首次runtime编译因新导航守卫调用 `showToast` 时缺少前置声明失败，已补声明并回归通过，原 `runtime-initial.log` 保留；不把失败记录改成通过。

## 两份生产候选

两份独立构建均exit 0，构建后HWCDC检查通过，随后完成独立四段/分区/ELF与全部冻结源码核验。两者均为普通production、`BatteryBoard=viewe-md80et-v10`、`DisplayDiagnostic=none`。

输入基线Git均为 `c2cc32d3ca0f56d9cea3f0f1be2d85912c5f5c53`。每份2142项冻结输入逐文件SHA与当前全部来源文件相符；两份path/SHA集合完全相同，时钟差异只来自构建选项。后续Git提交不追溯改写构建基线。

| 配置 | 应用文件字节数 | 应用文件SHA-256 |
| --- | ---: | --- |
| vendor / 16MHz | 1934656 | `8ef6c051d1cb8422350db9659ceb715917d9bdfe456cfe10fcb1c9ce213aa6b5` |
| pclk12 / 12MHz | 1934816 | `83b8d53edbf9d183eb64757c872ec688c08af1787305004d0e06c805f27123db` |

候选位置及摘要：

- **vendor**：`C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/runs/production-20261016-224442-6a8a2323c7c54fad843bc508e61e70ce/candidate.json`；candidate SHA `3acc15191b22ba4c27da394f9e991f5e1d316acd2f29d96718df803c7e3def1b`；源码清单SHA `6f0086739732044e8f9d45e3bc08d8622f508a8de4258636d81c6e05d1348aad`；ELF及应用内嵌ELF SHA `196484202a0c3c406b9997f6c4afdc5dbcd3abb433b9b3a9e48ba045e7c2b598`。
- **pclk12**：`C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/runs/production-20261016-224442-b6de235576c24e8fa56b9bd424eecd9f/candidate.json`；candidate SHA `9bf9ade9a04339cccf0b1c5b55a586cecda1b96775215791e1af5c569bf82deb`；源码清单SHA `465b855d0ee939db19f3dcb8a44c51536df85d49984a76c3d6f37715c5a1550d`；ELF及应用内嵌ELF SHA `cdacd4ffded79552ab67314fe52ecaaa4fece2fa6f641fe239206ac4dea6259e`。

两份实际应用均可放入3MiB app0，分区互不重叠且均在16MiB Flash内。SDK为v5.5.5，电池UART对象和有界电量日志已编入，HWCDC保留，Arduino/LVGL为core1，mode3及40行bounce源配置未变。清单的前三段均与前次普通候选一致，四个写入偏移为0x0000、0x8000、0xE000和0x10000。

| 公共段 | SHA-256 |
| --- | --- |
| 0x0000 | `31b3c1be45dc5a76aa85c82540d6787b675e711eaf11021a5eea7e36f469c6de` |
| 0x8000 | `ace02503447d0f470692e65fa76002f2d77a92dc81cd3813d8aa66718d716da9` |
| 0xE000 | `f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0` |

成功日志：`build-vendor.log`、`build-pclk12.log`；独立核验记录：`candidate-verification.json`。构建与主机测试通过不代表候选已经部署或板级电气验收通过。

## 交接及实机验收范围

本会话只交付源码、主机测试和固件候选。COM27/28/29已由用户确认底板返修、接电池并连接USB；身份、当前固件和刷写顺序由主会话再次确认。主会话计划先COM27确认真实桥接/电量计链路，再依次COM28、COM29。当前实时部署状态应看[本轮刷写记录](player-build-battery-rollout-2026-10-16.md)，本候选报告不代替刷写和实板证据。

后续验收需要分别记录：桥接/电量计版本与有效电压/SOC、原始GPIO及外电/充电指示、未知/过期恢复、Home当前地产建房与均匀建造、一次请求及权威反馈、房屋/旅馆真实下载与换图、三台各自显示。COM29原先仅12MHz静态等待页目视通过，不能继承为本次动态页面或≥24FPS/最大42ms通过；core1历史Retarget最大57ms等性能待验保持打开。完整实体游戏、电气与投产结论仍由对应验收负责方收口。

本轮21个文件的精确清单及SHA保存于本机证据目录 `handoff-files.json`，供总指挥按清单审阅与提交。未覆盖本报告范围之外的并行改动。

主会话已将20份PlayerConsole源码/测试提交为 `ac52ba75a4f7ad360bde14a3142470702dcf3081`；本文由主会话后续整合提交。该提交发生在输入冻结后，构建清单仍保留真实基准c2cc32d3，不改写为新HEAD。


## 2026-10-16 第二版：独立 UART 服务与初始化阶段诊断

主会话授权本轮实现仅限玩家屏固件及本报告；串口、烧录、线上对局、共享报告和 Git 索引均由主会话负责。原理图与 PCB 检查继续由 PCB 设计会话负责。第一版候选、失败和实机记录全部保留。

### 问题证据与修复边界

第一版依赖主循环调用 `BatteryTelemetry::tick()`。主循环进入界面锁或恢复流程时可能延迟 UART 服务；原协议在收齐字节后仍先核验 180ms 期限，迟服务会导致 Timeout。主会话 COM27 的 125 秒窗口记录两次 Timeout 并恢复，第二次在启动下载结束很久后、一次 resync 附近，不能仅归于启动下载。COM29 曾桥版本有效但始终没有接受 MAX 版本；`gauge0000`、`mode0000`、初始 `i00` 不能当芯片真实读回。

主会话后续转述用户保持电池、POWER 关五秒再开后电量恢复；其 45.094 秒恢复采集中九组完整、八组有效，一组 Timeout 后恢复。该结果说明链路可以工作，尚不能唯一判定是桥接芯片状态还是主控调度恢复。详细原始证据、实际设备版本和用户反馈见主会话维护的 [刷写记录](player-build-battery-rollout-2026-10-16.md)。本会话没有打开串口或改动设备。

### 实现

- 启用电池配置时创建一个 core0、priority2、3072 字节栈、5ms 相对休眠的 UART 服务任务。任务内部构造 UART1、安装驱动并独占协议状态机；没有 HTTP、LVGL、缓存锁或 USB 打印。UART RX/TX 缓冲各 256 字节，GPIO44/43、9600 baud 不变。
- 主循环以短临界区复制完整值快照，锁内只复制；快照读取后采样时钟，界面也先取快照再取渲染时钟，防止跨核发布较新时间戳造成无符号 age 下溢。主循环只负责有界诊断输出。
- 任务创建、UART 缓冲或驱动初始化失败发布 enabled=true、有效位全 false、`ServiceUnavailable=10`。UART 初始化失败先释放驱动再退出。关闭电池配置时没有 UART 对象、任务、I/O 或日志。
- 180ms 协议期限、700ms 安静同步、100ms 写后保护、3000ms 测量/350ms GPIO 新鲜度不变。完整但迟到的响应仍拒绝；没有电压转百分比、QuickStart、模型更改或新增寄存器读取。
- 记录真实服务间隔最大值、累计 Timeout、最近失败阶段及 RX 数量；实际收到 I2cStat 时才置 `i2cStatusSeen` 并更新时间。失败恢复不清除历史诊断证据。

### 八行诊断 schema

每组仍使用同一 `id` 和完整快照，最早启动约一秒、随后每五秒、最多 24 组、启动后 120 秒停止。单行最多 63 字节，只有 USB 空间足够整行才写，每次主循环最多一行；拥塞一秒则丢弃旧组，可能只有部分完整行，解析器不能假定每组总齐全。停止打印不停止采样。

原五行字段保留，第五行 MODE/CONFIG 加 `cached=1`，标明初始化/重新同步的已核验缓存，非每五秒重新读取。

| 行 | 新字段 | 语义 |
| --- | --- | --- |
| 6 | `svc gap to` | 服务至少执行过一次、最大相邻服务间隔 ms、累计 Timeout 次数（UINT32_MAX 饱和） |
| 7 | `fail op rx at` | 最近失败线事务、其关联操作、已接收/期望字节、失败 boot-ms |
| 8 | `svc_age i_seen i_at` | 冻结本组时距上次服务的 ms、是否曾真实收到 I2cStat、该读回的最近 boot-ms |

`svc` 是曾启动标志，当前服务新鲜度看 `svc_age`；`to` 是累计计数，旧 `e` 是最近协议错误，不能把每行 e=1 都当一次新失败。`i_seen=0` 时初值 `i=00` 不代表芯片读回；i_seen=1 后也必须结合 i_at 判断距离最近实际读取的时间。未有失败时 fail/op=255，rx=0/0。

| 代码 | fail/op 动作 | 代码 | fail/op 动作 |
| ---: | --- | ---: | --- |
| 0 | Sync | 8 | ClearSleep |
| 1 | BridgeVersion | 9 | VerifyConfig |
| 2 | ConfigureBridge | 10 | Voltage |
| 3 | VerifyBridge | 11 | Soc |
| 4 | GaugeVersion | 12 | VerifySample |
| 5 | ClearMode | 13 | Gpio |
| 6 | VerifyMode | 14 | Idle |
| 7 | ReadConfig | 15 | I2cStatus |

fail=15 时 op 指正在检查状态的 MAX 操作；其他情况下 op=fail。`e` 编码保留 0 None、1 Timeout、2 Framing、3 BridgeVersion、4 BridgeConfig、5 GaugeVersion、6 I2cStatus、7 GaugeConfig、8 Voltage、9 TxIncomplete，追加 10 ServiceUnavailable。

### 本轮主机验证与构建

本机证据目录：`C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/battery-service-20261016/`。

- 协议/诊断：59 PASS、0 FAIL，日志 `telemetry.log`。新增真实 I2cStat 零与未读取区分、迟到但完整响应拒绝、失败 RX/阶段、服务间隔、恢复后累计证据、新鲜度副本以及最大诊断字段 63 字节限制。第一次编译因测试局部变量遮蔽触发 `/WX`，仅修正测试变量名后通过，未放宽警告。
- 实际 `battery_hardware.cpp` 双配置集成：24 PASS、0 FAIL，另 1 项实际 ino 时钟顺序守护，日志 `service.log`。使用独立 std::thread 执行实际服务函数和字节级桥/电量计模拟；主循环五秒不调用硬件 tick 仍有新鲜测量、并发快照一致、错误时间顺序回归、UART/USB 所有权及临界区无 I/O、服务迟到与恢复、350/3000ms 过期、创建/缓冲/UART 失败和 disabled 全覆盖。模拟验证不能代替 ESP32 栈余量/调度或板级实测。
- 实际 LVGL UI 回归：21 PASS、0 FAIL，日志 `ui.log`；覆盖未知/过期、31 页输入与文字避让、建房/旅馆预览及独立叠加层，界面代码未改。

生产源码在基准 `ac52ba75a4f7ad360bde14a3142470702dcf3081` 上冻结；两份正常固件均 BatteryBoard=viewe-md80et-v10、DisplayDiagnostic=none，保留 Arduino/LVGL core1、mode3 和 40 行 bounce。使用原构建审计流程全新快照，未复用旧对象绕过哈希。

- vendor：`runs/production-20261016-232809-80d12fa59f25438e9d38772bfd598b45`。
- pclk12：`runs/production-20261016-232809-fcac19da97e54700a2f0d399698e4eb3`。

两份正常固件均构建成功并通过独立核验，详情见本机 `candidate-verification.json`。每份 2142 项快照及当前输入逐项 SHA 一致，两档源内容相同；相对第一版仅五份生产源变化。四个烧录段完整，应用可放入 3MiB app0，分区无重叠且处于 16MiB Flash 内。实际 ELF 与应用内嵌 ELF 哈希相符；链接表确认独立服务、UART 与 HWCDC，应用包含新诊断字符串。

| 配置 | 应用字节数 | 应用 SHA-256 |
| --- | ---: | --- |
| vendor / 16MHz | 1935760 | `1344a9d12a6776c21b85e0f4b34c16de4f137ee53c24f9965cd65c19e8f85ee2` |
| pclk12 / 12MHz | 1935888 | `df9f1282b04e009d336fe10d186f3265446b8a1e8d2d7484a9a89ce209f4a631` |

候选摘要：

- **vendor**：candidate SHA `dcf63a62d142e67ee6ddd5cbc7d48317ce53f5f3498f1153cfbf30f9a99ed863`；源码清单 SHA `492f87efa620c9e20bc740a5583bd82354906535efa35b9a86e24dc062e45d3e`；ELF 及内嵌 SHA `70213029293daccb8ca713be8633eb194b1dc564ef43bdeaec7f1c9174225445`。SDK `v5.5.5`；各候选位于上述 run 目录的 `candidate.json`。
- **pclk12**：candidate SHA `c5aff010fa0b04cab9895e58b18cf919997170b993548806939b30f14476dd33`；源码清单 SHA `3689e10e668cdfc494c981e0017c49d18dbb669b26c665c2d61bb7c0ed16c462`；ELF 及内嵌 SHA `01d709dc8dc81197c7155605652922cca7bf3798fcf0801b61f6c16fc0189d27`。SDK `v5.5.5`；各候选位于上述 run 目录的 `candidate.json`。

本轮 13 份源码/测试由主会话提交为 `bb7281c8d98cd987e10f7d9dda90ecc617f57c58`。该提交发生在输入冻结之后，候选仍保留真实基准 ac52ba75，不改写历史。专属报告由主会话后续提交。精确文件及 SHA 见本机 `handoff-files.json`。

本会话完成源码、测试、候选核验和交接，未执行串口或烧录。主会话负责先 COM29 验证热复位后的初始化、有效读数和是否仍间歇超时，再安排另两台；实际部署状态以主会话刷写记录为准。ESP32 真实调度/栈余量、测量与显示稳定性、完整游戏/投产验收仍需各自实机证据，不能由主机测试或编译代替。
