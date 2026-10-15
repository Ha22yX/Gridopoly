# 玩家屏 16 格试运行进度（2026-10-15）

本轮由主会话协调，玩家端负责 `Firmware/PlayerConsole`、所属测试及本文。目标是 4 角落＋12 长模块试运行的普通固件候选。原理图、PCB 和电池底板检查仍由「Gridopoly · PCB设计」负责；本轮没有操作 EDA 或硬件设计文件。

## 当前结论

输入排队与头像焦点重绘已修改，256 项主机回归通过，普通候选 `22ca08e6` 已构建并完成源清单和烧录段哈希复核。尚未刷写玩家屏，不能将主机测试或候选构建当作实机输入、显示或整盘验收。

保留可信停点解码、旋钮方向、1 ms 采样、core1、mode3 双缓冲、40 行 RGB bounce 以及已获用户认可的手写行为。电池遥测默认关闭。本轮没有修改 PSRAM/绑核/帧票据，也未恢复曾发生闪烁错位的 core0 方案。

## 已复现并修复

1. **逐帧消费造成输入积压。** 原主循环消费一个旋钮步进即进入其余服务与渲染；快转队列和后续按键要跨多轮处理。现在每轮最多处理 16 个输入，保持逐步方向、顺序及原时间戳，不跨反向或按键合并。遇到松开即让出，先服务手写请求、页面和网络，后续输入留到下一轮。
2. **积压短按误触发长按。** 主会话复审指出第 16 个事件按下、第 17 个旋转、第 18 个松开的边界。原计时逻辑可能在尚未消费松开时按当前时间触发长按。实测主机用例在上述排列、较早按下后跨多批旋转两种情况下，首页与弹窗共 4 项失败。新增锁内队列非空查询，`appTick` 在有积压时推迟两处长按入口，其他页面、提示和命令超时继续运行；生产循环传入新鲜 `millis()`。最终仍严格限于 16 个事件，不靠无限排空或多读一个事件规避。真实首页 3000 ms、弹窗 1200 ms 持续按住仍通过。
3. **头像焦点更新重绘无关行。** 原 `refreshSamePageFocus` 每次重新设置全部五行和确认按钮的样式，LVGL 即使收到相同值也标记重绘。现在只更新旧、新焦点涉及的行，确认按钮仅在进出确认焦点时更新。真实 LVGL 回归的“无关行不应失效”在旧代码失败，修复后通过；同时验证各行与确认按钮清除旧高亮、编辑箭头保持正确。
4. **逐步日志默认关闭。** `GRIDOPOLY_INPUT` / `GRIDOPOLY_AVATAR_INPUT` 跟随已有的 `INPUT TRACE ON` 诊断窗口输出，90 秒自动结束，亦可 `INPUT TRACE OFF` 提前结束。正常旋转不再每步向 CDC 写两行诊断；命令和连接日志保留。现有 CDC 发送超时已为 0，本轮未把它描述为新增的阻塞超时修复。

## 验证证据

证据根目录：`C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/player-16-20261015/`。`accepted-tests.json` 汇总最终 7 套 256 项及日志 SHA256；`host-tests.json` 与 `hold-regression-and-final-tests.json` 保留阶段记录。失败基线与中间修复失败保留，没有覆盖或改门槛。

| 最终主机套件 | 结果 | 范围 |
| --- | --- | --- |
| input session / hold-final | 23/23 | 顺序、反转、批次公平性、跨批短按、真实长按、非输入超时 |
| input runtime / runtime-final | 45/45 | 真实 LVGL、头像行失效/高亮、手写保留、删除与 DONE 顺序 |
| rotary / rotary-final | 30/30 | 可信停点、非法跃迁、快转、合法反转、头像字段 |
| setup / setup-final | 83/83 | 等待新局、配对、头像姓名、恢复、终局和旁观 |
| movement cue / cue-accepted | 58/58 | 16/24 格各 29 项：目录校验、首帧门槛、Action 17、手动到达、重连 |
| join transport | 10/10 | 生产入局报文解析和端点约束 |
| tag transport | 7/7 | 配对投影、修订与 epoch 顺序 |

合计 256 项通过。16 格用例使用共享 `BoardCatalog` 的实际 16 格定义及 CRC，不冒充真实格子上报。手写几何样例保留已有覆盖水平，不把本轮通过解释为所有笔迹都可识别。

失败日志：`input-before.log`（4 项失败）、`runtime-before.log`（1 项失败）、`hold-before.log`（4 项失败）、`hold-after.log`（仅保护尾部长按分支时仍有 2 项弹窗失败）。最终保护同时覆盖前段弹窗进度与尾部按键计时。

## 构建与设备

最终构建快照：`C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/runs/production-20261015-002138-823510fd6f664b72bc5151fb6d8d53d2/`，使用 `compile-isolated.ps1 -Mode production -BatteryBoard none`、ESP32 core 3.3.11。候选入口为该目录的 `candidate.json`，源码清单为 `candidate-sources.json`。

- 构建成功：program 1,905,642 bytes（60%），静态 RAM 125,672 bytes（38%）；应用文件 1,905,792 bytes。HWCDC 链接检查通过。
- 应用 SHA256：`22ca08e663a312db860bfd4c60443b9849f76a1e0de50e5976f417f2820392f6`。
- `candidate.json` SHA256：`4b56777946e42b322f6a7e5e100211a3392a28f41e04d24242c476eb37bdc2e9`。
- 源清单 SHA256 / sourceDigest：`b94941ce366d88f6dce897ea286e93f0e7338c5b79a2502981fc8982c654963c`。
- 构建原始 Git 基线 `f07573419714373097ce84ccd2147d2be485ec79`；本轮 11 个玩家源码/测试随后由主会话统一提交为 `2ea95b4cafbc56972ef676eb72f990efa16477d3`。保持已经验证的冻结候选，不因 HEAD 前进重新编译；精确构建内容以源清单文件哈希为准。
- 独立复核 2,135 份快照源文件及 4 个烧录段全部匹配；记录见证据目录 `source-verification.json`、`candidate-verification.json`、`normal-build.log`。实际参数为 ESP32-S3、DIO、80 MHz、16 MB，电池板配置 none。

| 烧录偏移 | 文件 | SHA256 |
| --- | --- | --- |
| 0x0000 | PlayerConsole.ino.bootloader.bin | `31b3c1be45dc5a76aa85c82540d6787b675e711eaf11021a5eea7e36f469c6de` |
| 0x8000 | PlayerConsole.ino.partitions.bin | `ace02503447d0f470692e65fa76002f2d77a92dc81cd3813d8aa66718d716da9` |
| 0xE000 | boot_app0.bin | `f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0` |
| 0x10000 | PlayerConsole.ino.bin | `22ca08e663a312db860bfd4c60443b9849f76a1e0de50e5976f417f2820392f6` |

先前 `production-20261015-001221-cc0605bb2dea41b09cc2608966d50af9` 在长按边界复审后主动停止；`production-20261015-001656-14944027be1f4314b375b0f588a70368` 在末尾渲染也改用锁后新鲜时钟后主动停止。两者都不是交付候选。生产循环的计时与渲染均不能再使用早于已消费输入/动画时间戳的循环开头时钟；SelfTest 的固定时序夹具保持原样。

本轮只做 USB/PnP 枚举。COM7 未枚举；随后出现的 COM12–15 已由主会话确认是首批四个格子模块，由格子端独占。COM5 的非 ESP USB 身份未猜测或打开。玩家屏尚未通过 USB 接到电脑。主会话随后同步用户“等一会再搞下面的”：现场 USB 换接和屏幕联调暂缓，本端完成软件交付，等用户准备好再由主会话安排。没有打开串口、复位、刷写或制造游戏输入。

最后有证据的玩家屏部署仍是 2026-09-26 的普通固件应用 SHA256 `9c4e182530b4f1e5043bd1180c3658664c9bba089f2f526972a8009a9d1d4437`，不是本轮在线读回结果。

## 剩余验收与交接

- 玩家端：待主会话明确玩家 USB 身份与窗口后，正常独立供电，核对实际版本、部署候选并有界采集；结束释放串口，保持正常页面。
- 主会话协调用户：实体慢转、快转与及时反转；头像/姓名和已认可手写；原游戏页是否仍闪烁或错位。物理一格对应的真实步数需由用户操作确认，不能仅数日志。
- 玩家端与主会话：core1 历史 Retarget 最大间隔 57 ms 仍超 42 ms 门槛。此次没有实机性能结果，不声称已经解决或降低门槛。
- 三端联调：真实 16 格分配、配对拿放、MoveGuide 实际呈现后 Action 17 接受、目标 cue、正确绑定 UID 到达及手动到达、断线恢复。主机状态机通过不能替代上述实体验收。
- 主会话统一审核共享 Git 索引与发布包。玩家端没有执行 git add/commit/reset/stash，没有新建自动化，也未修改旧发布包。
