# 玩家屏电池遥测固件接入

## 当前状态补充（2026-10-15）

本文主体保留 2026-09-27 的实现、失败修复、构建和校验证据。当前部署已是 COM7 `22ca08e6…`、COM27/28/29 `dfc4b7e5…`，冻结包为 `trial16-2026-10-15-v4`；四台均 `BatteryBoard=none`。这表示遥测软件关闭，不表示没有物理电池。COM7/27/28 底板已获用户返修确认并带电池完成 USB 升级；COM29 底板/电池状态尚无独立确认。USB 烧录和页面运行不等于 UART、电量、充电、欠压或温升通过，详见[当前交接](../CURRENT-DEVELOPMENT-HANDOFF.md)及[玩家基线](../player-console/current-player-console-baseline.md)。

后续启用遥测须从包含当前输入/显示修复的源码构建新候选，由玩家屏与 PCB 会话协调实物窗口；不能直接部署下文旧 `b40e1701…` 候选而丢失后续修复。启用驱动会配置桥接 GPIO/I²C/超时、将 MAX17048 MODE 写零，并在需要时清 CONFIG 睡眠位，因此不属于纯只读采集；它不写充电器配置。本次文档整合没有启用或部署该功能。

## 2026-09-27 实施记录（历史）

当日范围：SC18IM704 UART桥、MAX17048电量计与BQ24074两路状态输入，以及独立简洁状态显示。**该轮未接入实物底板、未打开COM7、未刷机；当时运行9c4e1825普通固件，旋钮现场验收待反馈。** 当时的 `batch32-2026-09-26-v4`、9c4e与efbe源码及四段回退产物均保留。下文未带日期的“本轮”“v4”均指这次历史实现，不指10月试玩包。

## 板型与接线边界

依据[硬件接口约定](../hardware/player-battery-telemetry-interface-2026-09-27.md)及[厂家GPIO证据](../hardware/player-screen-uart-gpio-evidence-2026-09-27.md)，显式 `viewe-md80et-v10` 板型使用屏幕TX=GPIO43、RX=GPIO44；UART1通过GPIO矩阵映射这两个脚，USB HWCDC继续承担日志/上传。编号来自厂家原图UARTTX/UARTRX与乐鑫模块引脚表交叉核对，不是将FPC6/7作为GPIO。实物版本与端到端通断仍待首板验证。

用户随后要求底板J2焊盘1–10网络反转并纳入PCB/BOM，最新接线区分两套编号：屏幕逻辑FPC7 TX经底板J2.4到U9.10 RX；U9.11 TX经底板J2.5到屏幕逻辑FPC6 RX。屏幕GPIO43/44、厂家逻辑FPC6/7和UART协议不变，因此不修改固件板型配置或重建；该映射仍需实物排线通断验证。

`battery_board_config.h` 默认 `GRIDOPOLY_BATTERY_BOARD=0`，没有UART对象、初始化或轮询，没有额外串口输出、假电量或UI占位。仅明确的板型1启用，其他值编译报错。构建入口为 `Firmware/PlayerConsole/tools/compile-isolated.ps1 -Mode production -BatteryBoard none` 或 `-BatteryBoard viewe-md80et-v10`；选择同时进入实际编译flags、inputs和source manifest。两种构建各用全新run，不覆盖既有成功候选，也不替换v4发布包。

## 驱动与数据含义

`battery_telemetry.h/.cpp` 是独立纯C++状态机，使用固定缓冲，不分配内存、不delay、不打印。`battery_hardware.h/.cpp` 是UART适配。普通setup仅初始化一次；tick在LVGL锁外使用当前millis。一次tick最多读取32字节、最多发送一条不超过16字节的命令；只有TX缓冲足以容纳整条命令才写入，不flush、不等待空间。禁用路径连Arduino/HardwareSerial依赖也不需要。

启动与恢复先排空输入，等700ms连续接收空闲，再按9600/8N1查询16字节、NUL结尾且前缀为SC18IM704的版本串；不依赖开机OK，也不锁定1.0.1版本号。GPIO保持输入、I²C约99kHz，I2CTO显式设置0x67并读回。默认0x66的TE位关闭，不能沿用。根据[NXP手册§9.2.6](https://www.nxp.com/docs/en/data-sheet/SC18IM704.pdf)，clock=19及TO字段51时连续SCL低电平超时约35.5ms；写后100ms保护等待覆盖本板正常写入与一次持续卡低，**不是任意反复clock stretching的整事务完成保证**。写后仍需status和寄存器读回，首板需验证时序。

所有MAX17048寄存器使用16位高字节在前。每个读取完整定长数据后，再单独读取I2CStat，仅0xF0接受；0xF1/F2/F8、短包、额外字节、超时等不发布数据并进入重新同步。MODE直接写0并核验，不把未知位或QuickStart位做读改写；CONFIG只清0x0080，保持RCOMP及其他配置，状态与读回必须匹配（允许ALRT在期间自主置位）。不QuickStart、不POR、不改HIBRT、不虚构温度补偿。芯片正常hibernate保留。[MAX17048手册，第10–12页](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX17048-MAX17049.pdf)

VCELL和SOC成对提交，正常约1秒一次，保留raw16、毫伏及采样时间；2.5–4.5V作为本单节电池板的合理值门槛，门槛外为未知，并不判定电池物理缺失。SOC显示截断整数并限制0–100%，原始模型估值保留。测量超过3秒、GPIO帧超过350ms未更新均显示未知；已核验0%与未知严格分开。

SC18IM704复位时发出的0x4F4B也是合法SOC编码，而复位后的I2CStat恰为0xF0。因此SOC/status之后，再核验非默认的I2CTO=0x67才能发布完整样本；不把合法SOC=0x4F4B加入黑名单。GPIO采用 `52 04 09 50` 同时读取IOState和I2CTO，固定2字节且第二字节必须为0x67，避免启动OK冒充单字节GPIO。桥接复位导致标记丢失时，立即失效并重新初始化。

GPIO约75ms调度；每个完整gauge+status结束后优先处理已到期GPIO并保存后继，不让VCELL/SOC连续链饿死状态采样。实际回包收齐且已到达的额外字节检查完毕即可推进，不额外占用一轮UI循环等待。PGOOD和CHG分别去抖，帧新鲜度取真实成功帧时间，不要求两个输入组合一直静止。PGOOD低仅表示有效外部输入，CHG低仅表示充电指示有效；CHG高不强制100%，不写“已充满”，也不以CHG低声明实测净充电。

约2Hz检测保留“故障候选”含义：以每次边沿前后采样时刻作为物理边沿可能区间，检查相邻半周期及最近8个半周期累计区间，观察至少2秒；真正超过175ms的采样间断清空历史。这样33/40ms主循环下量化得到的132/360ms边沿间隔不会直接误判节奏坏掉，也不以放宽真实缺采样门槛保留陈旧故障。停止翻转750ms后退出候选。GPIO采样不足或状态矛盾显示未知/需检查，不宣称安全定时器已经确诊。

## 独立状态显示与实际QA

`ui_battery.h/.cpp` 在LVGL顶层创建独立、无点击/焦点/滚动事件的单行状态条，坐标 `{172,450,136,16}`，整框位于半径238的圆内，使用完整16像素行高的Montserrat14。普通renderer、页面布局、外圈参数、手写、旋钮与RGB配置均未改。状态条在既有LVGL锁内更新；文字或颜色不变时不重复设置，页面重建不销毁该状态层，遥测变化不增加AppState revision或触发页面重建。

显示示例为 `78%  EXT / CHG`、`42%  EXT`、`0%  NO EXT`、`--%  POWER --`、`100%  CHECK CHG`；EXT和CHG含义分离，CHECK CHG表示需复核的候选异常。默认禁用时连状态条对象也不创建。

**初版视觉失败保留：** 顶部两行方案虽在圆形栅格内，但实图中第二行与蓝色外圈相交，主审及本端确认不可接受。随后迁至底部单行，不改圈或页面来遮盖。几何测试亦修正为检查祖先裁切后的可见标签/图像和真正绑定的输入按钮：装饰容器的默认CLICKABLE以及已被列表viewport裁掉的离屏行，不能当作可见按钮；最终结论同时有PNG目视依据，不只靠放宽断言。

真实LVGL、生产renderer与手写模块仅替换硬件/远端图片取得，最终18/18通过：全部31页、实际模态两按钮、标签全宽/字高、默认无对象、重复值不写label、未知/0%/满量程/故障候选、独立层生命周期和原手写画布/笔迹/revision保持。预览为 `battery-interface-20260927/ui/battery-handwriting.png`、`battery-home-fault.png`、`battery-assets-unknown.png`、`battery-modal-zero.png`；顶部失败截图为 `battery-handwriting-top-failed.png`。

## 回归与审查记录

`tools/test-battery-telemetry.cpp/.ps1` 编译实际纯C++驱动及默认硬件适配，字节/时序设备fake不替换生产状态机。最终44/44通过，覆盖固定长度/碎包/迟到/额外字节、桥接身份/配置错误、NACK/总线超时、异常电压、raw保留/0–100显示、睡眠位/RCOMP、单事务/每tick预算/TX满、断线恢复/陈旧/millis回绕，以及状态组合与闪烁保持/退出。

独立审查先发现VCELL/SOC查询链饿死GPIO、SOC被启动OK污染，后发现33/40ms下组合去抖错误使故障过期和边沿量化导致退出；这些都在构建前修复，未将原41项通过宣称为完结。最终独立保持复核4/4：33ms在3894ms首次识别，12秒内故障/PGOOD丢失0，停闪891ms退出；40ms在3880ms首次识别，丢失0，停闪960ms退出。证据为 `C:/Users/kicof/AppData/Local/Temp/gridopoly-battery-hold-final-audit-20260927/audit.log`；原审计失败分别在 `gridopoly-battery-protocol-audit-20260927` 与 `gridopoly-battery-final-audit-20260927`，不覆盖。

本端证据根目录 `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/battery-interface-20260927/` 保存driver-tests.log、ui-tests.log、早期编译/几何失败日志和PNG。未重复无变化的整套游戏/手写测试，没有实物测量或现场2Hz故障输入。源码交主任务统一Git操作。

## 实现清单与源码证据

主任务已将以下14份实现、构建入口及测试文件建立检查点 `8ee546be7f08dd9de17aa5560a0dbea0b5b5fc78`，本端仅补本报告，合计15份本轮交付文件。以下路径均相对 `Firmware/PlayerConsole/`：

| 范围 | 文件 |
| --- | --- |
| 正常入口 | `PlayerConsole.ino` |
| 板型及UART适配 | `battery_board_config.h`、`battery_hardware.h`、`battery_hardware.cpp` |
| 纯C++驱动 | `battery_telemetry.h`、`battery_telemetry.cpp` |
| 独立状态条 | `ui_battery.h`、`ui_battery.cpp` |
| 构建入口 | `tools/compile-isolated.ps1` |
| 驱动回归 | `tools/test-battery-telemetry.cpp`、`tools/test-battery-telemetry.ps1` |
| 真实LVGL回归 | `tools/test-battery-ui.ps1`、`tools/battery-ui-tests/CMakeLists.txt`、`tools/battery-ui-tests/ui.cpp` |

完整文件大小、工作区SHA256和提交blob SHA256存于证据目录 `source-inventory.json`。13份与实现提交逐字节一致；`compile-isolated.ps1`仅Git入库时将11处CRLF归一化为LF，文本内容相同，工作区字节未改。初次直接比较原始blob导致这一项不等的记录保留为 `source-inventory-initial-raw-git-comparison.json`，没有以修改工作区换行方式制造一致。驱动头文件SHA256为 `bf97de7f4e29f15dcd4dc10b95204c3b3a9fc05fd0c45e84b4ec2d3ac86dfe7f`，实现文件为 `039614b0a4385c74803ccab8a34af85e6723cbe1d1640fed6f898168bccc88a5`，与独立保持复核使用的源码一致。

## 普通固件构建记录

两份构建均使用ESP32核心3.3.11、16MB flash、OPI PSRAM、HWCDC和error日志等级，不带selftest开关。每次调用均新建run、冻结库与源码，不复用旧build目录。冻结时记录的Git基准保留原值，最终以每份实际源码哈希核对，后续HEAD变化不是重建理由。

默认 `none` 已完成：run为 `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/runs/production-20260927-043217-5cb9ccd03b1842e78bd87d7f4c23dce0/`。程序占用1,905,434字节、静态RAM 125,672字节，HWCDC检查通过；nm中无电池驱动、UART对象或状态条符号。应用SHA256为 `b480d72de1405bbc4c2500d874eb7ac222851a57b34813b307c74fa8cf1617e6`，sourceDigest为 `a2b7ba6879a89f338e3cc23308b1dda5ab4d8932025cf8503dabce664c23e31c`。2,134份冻结/当前源及四段产物逐项核验通过；主任务独立复核为 `C:/Users/kicof/GridopolyReleases/player-battery-20260927/default-candidate-verification.json`，时间UTC08:43:49。

启用 `viewe-md80et-v10` 已完成：run为 `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/runs/production-20260927-044309-7778911fdb9f4a4199df8dbb0eb9f606/`。程序占用1,921,138字节、静态RAM 125,992字节，较默认增加15,704字节程序和320字节静态RAM；不将此视为运行时总堆内存测量。应用SHA256为 `b40e1701be08e3ef8df010a083fee4df4ec8d576395aefec096fe8620bed7515`，sourceDigest为 `a15cec5898d9a9b36cd5968d046938651192c2ee0cc8c8c8989efc431f954988`。HWCDC检查通过，2,134份冻结/当前源和四段产物全部匹配，启用flags确为 `-DGRIDOPOLY_BATTERY_BOARD=1`。

主任务启用候选独立复核亦通过，记录为 `C:/Users/kicof/GridopolyReleases/player-battery-20260927/enabled-candidate-verification.json`，时间UTC08:54:53。主任务同时逐path/SHA对比两份manifest确认相同，编译flag仅增加 `-DGRIDOPOLY_BATTERY_BOARD=1`。后续硬件J2及PCB导入提交 `97afc2d` 不改变这些固件源码。

两份run均含 `candidate.json`、`candidate-sources.json`、`inputs.json` 和独立build/output；默认记录基准 `74f1230ed8ae2e5f764d8a9acdc4b10a6bb0d08c`，启用记录基准 `8ee546be7f08dd9de17aa5560a0dbea0b5b5fc78`，实际冻结源码内容相同。应用bin文件大小分别1,905,584和1,921,280字节，与Arduino报告的程序占用口径不同。四段offset为0、32768、57344、65536，完整SHA和大小均在证据目录 `build-verification.json`，两板型、普通生产模式及manifest/flags一致性总结果PASS。

`link-verification.json` 进一步验证最终ELF：默认没有电池相关符号和调用；启用版保留驱动/UART/UI，正常setup初始化、loop轮询和renderAppFrame更新均成立，两版均链接HWCDC。初次线性反汇编因Xtensa对齐填充失步而漏读setup调用，原失败记录保留为 `link-verification-initial-linear-disassembly.json`；检查setup目标文件重定位，并从实际分支目标0x42002ab4重新反汇编，在0x42002abf确认call8到 `batteryHardwareBegin`（0x4200a534）。依据为 `link-viewe-md80et-v10-relocations.txt`、`link-viewe-md80et-v10-setup-aligned.txt`，无需修改生产源码或重建。

同一最终核验亦读取旧9c4e与efbe各2,127份冻结源码、source manifest和四段产物，哈希全部通过；`batch32-2026-09-26-v4/release.json` SHA256仍为 `b852c45167777731475e17703f1100342c49f3cb33877d2df47909fc5ab574ef`。本轮两候选没有写入v4，也没有上传。构建原始日志为 `build-default.log`、`build-enabled.log`；核验脚本和JSON均保留在本轮证据目录。

## 2026-09-27 阶段结论（历史）

当日已完成源码、主机回归44/44和18/18、独立保持复核4/4、两种普通固件构建及产物/链接核验。功能默认关闭，当轮未打开或刷写COM7，仍使用当时的 batch32 v4。原计划是在底板到位后协调实物版本/通断、桥接版本/I²C状态、万用表电压对照、USB插拔与关机充电后的SOC变化及屏幕复位恢复；这些遥测实测门槛至10月15日仍未关闭，当前设备与后续候选要求以上方补充为准。
