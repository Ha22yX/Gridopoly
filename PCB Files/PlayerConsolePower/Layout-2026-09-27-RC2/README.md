# 玩家屏幕电池底板 R1 RC2

2026-09-27。**完整布局、完整布线的制造候选，尚未制造放行。** 当前嘉立创EDA 的 PCB1 已保存为本版；原理图两页及其 68 件 / 243 脚网络与 PCB 一致。原理图 41 条告警明细、排线/电池规格、板厂工艺及实板功能验收仍需闭合。

本版在已留档的 [RC1](../Layout-2026-09-27/README.md) 上复核并改善 USB 局部交叉和回流。RC1 原文件保持不变。用户指定的板框、四孔、USB-C、POWER/RST 和 J2 坐标/方向全部保留；J2 反转后的物理引脚定义保留。其余器件沿用已完成的功能分区，68 个器件全部顶面。

| 项目 | RC2 结果 |
| --- | --- |
| 外形、层数 | 80×80 mm，R5；2 层，标称 1.6 mm / 1 oz |
| 器件与贴装 | 68 个封装、243 个焊盘；67 件贴装、29 种 BOM 料号；J3 裸孔焊电池线 |
| 布线 | 533 段走线、83 个过孔、上下 GND 铺铜 |
| 原生 PCB 严格 DRC | 0 项，包含连接检查 |
| 布局/网表/制造引用核对 | 34/34 通过 |
| 本轮 USB 变更与路径核对 | 10/10 通过 |
| 导出几何核对 | 10 个制造文件；93 个 PTH 孔/槽对象、2 个 NPTH 定位孔 |

USB-C 的一支 D− 改为顶层直连 ESD，移除一个不必要的信号过孔；两处底层跨线缩短，并新增 5 个地过孔。实际导出的铜层证明这些地过孔连接了上下 GND，且两处顶层地铜可跨过底层电源走线形成短连接。仍有局部不成对走线、层切换和参考地中断，详见 [USB 复核](usb-layout-review.md)。这不是控阻、时延或 USB 波形测试通过的证明。

## 文件入口

- [制造说明](manufacturing-notes.md)：板厚、铜厚、阻焊、钢网、旋转和排线约束。
- [Gerber](Manufacturing-RC2/PlayerPower-R1-RC2-Gerber.zip)、[BOM](Manufacturing-RC2/PlayerPower-R1-RC2-BOM.csv)、[CPL](Manufacturing-RC2/PlayerPower-R1-RC2-CPL.csv)：同版规范化文件，暂不可跳过待办直接量产。
- [顶面预览](pcb-top-fabrication-preview.png)、[底面预览](pcb-bottom-fabrication-preview.png)：从本版实际制造层生成，已检查 RC2 丝印及修改区域。
- [最终原生源文件](PCB1-routed.source.jsonl)、[PCB 网表](PCB1-netlist.json)、[原理图网表](schematic-netlist.json)、[最终读回及 DRC](final-live-audit.json)。
- [34 项检查](layout-verification.json)、[USB 路径及 10 项检查](usb-routing-review.json)、[实际铜层复核](usb-reference-copper-review.json)、[制造导出解析](gerber-parse-review.json)。
- [主任务复核](independent-review.md)：在副本重现布局、USB 路径与实际铜层计算；[独立制造文件审查](independent-manufacturing-audit.json)另核对钻孔、BOM/CPL和固定几何，保留未完成的电气验收。
- [首板测试表](bringup-checklist.md)、[机器可读放行状态](release-status.json)、[文件校验和](SHA256SUMS.json)。测试表未执行。

`before-usb-review.*` 是修改前 RC1 读回。`usb-change-plan.json`、`usb-change-live.json` 为首次尝试及其发现的间隙错误；随后修正记录为 `usb-refinement-drc.json`。**最终坐标、网络和检查结果以 final-live-audit.json、PCB1-routed.source.jsonl 和 usb-routing-review.json 为准，勿将尝试方案用于制造。** NativeExports 是原生导出核对留档；仅 Manufacturing-RC2 为规范化制造候选。

`verify-layout.cjs`、`verify-usb-routing.cjs` 可用 Node 重现静态检查；会重写各自 JSON 报告，复核留档时应在副本内运行。`verify-reference-copper.py` 需要 gerbonara、shapely，直接读取相邻 RC1 与本版 Gerber ZIP，重算二维铜层报告；也应在副本内运行。校验和仅覆盖本目录冻结产物，不包含会继续更新的 Docs 页面。

## 尚未闭合

1. J2 候选下接点母座需要实际排线尾端 0.30±0.03 mm、插入方向、端到端针序和载流核实；用户仅已确认 10 针 / 0.5 mm 与可视针序。
2. 电池须是适用于 4.2 V 终止充电的受保护单节电池，允许充放电电流和环境需核实。本板按用户要求不测电池温度，也没有电池反接保护。
3. 原理图原生 DRC 尚有 41 条告警；API 仅返回数量，明细未分类。PCB 0 项不能替代此项。
4. 板厂确认 USB 叠层/阻抗、QFN EP 散热孔和钢网工艺、装配旋转及 DFM；库存须在下单时重查。
5. 首板测充电、启动/切换、关机静态电流、欠压、温升、USB 正反插/烧写/恢复，以及 UART 电量和状态查询。

普通 USB 来源默认 100 mA 输入限流，运行时可能需要电池补充，不能保证电脑口边运行边净充电。底板无待烧录 MCU，本轮未修改或烧录屏幕固件、未操作 COM7、未下单。
