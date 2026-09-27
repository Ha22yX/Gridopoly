# 玩家屏幕电池底板 R1 RC3

2026-09-27。**当前编辑器已经保存的制造候选，尚未制造放行。** 本轮复核原理图和实际PCB，不涉及软件。

保留现场已有的 J3 上移及走线调整，修正随 J3 移动后留在旧位置的 BAT+/BAT− 极性丝印。板框、四孔、USB-C、J2及两个按键的位置不变，J2反向物理针序不变。原理图与BOM没有新增改动；RC1/RC2冻结目录不覆盖。

| 项目 | 当前 RC3 |
| --- | --- |
| 板与布局 | 80×80 mm/R5，2层、1.6mm、1oz，68件全部顶面 |
| 网表和走线 | 243脚与原理图一致；536段线、82过孔 |
| 原生PCB DRC | 0项；原理图仍有41条未分类警告 |
| 静态布局/制造核对 | 34/34通过 |
| 实际Gerber铜连通 | 41个网络无短断路或意外NC连接 |
| 装配/钻孔 | 67件、29种料号；92个PTH孔槽、2个NPTH；J3只焊线 |

**投产判断和边界以[全面硬件复核](production-review.md)为准**：J2仅有500mA/触点的供应商额定值，排线实物、电池参数、41条原理图警告明细和首件性能尚未闭合，不能承诺直接投产后无需验证即可使用。

- [制造说明](manufacturing-notes.md)、[首板测试表](bringup-checklist.md)（未执行）。
- 同版候选：[Gerber](Manufacturing-RC3/PlayerPower-R1-RC3-Gerber.zip)、[BOM](Manufacturing-RC3/PlayerPower-R1-RC3-BOM.csv)、[CPL](Manufacturing-RC3/PlayerPower-R1-RC3-CPL.csv)。未提交订单。
- 实际制造层预览：[顶面](pcb-top-fabrication-preview.png)、[底面](pcb-bottom-fabrication-preview.png)。
- [34项验证](layout-verification.json)、[实际铜层检查](copper-connectivity-review.json)、[导出核对](manufacturing-export-audit.json)、[当前变更边界](change-audit.json)、[放行状态](release-status.json)。
- 原生源、PCB/原理图网表、最终读回与DRC，以及字节校验和均保留本目录。`NativeExports`仅作原始证据；`Manufacturing-RC3`为去重钻孔、规范CSV后的制造候选。

复现脚本会重写对应报告，冻结后请在副本运行。`verify-layout.cjs`使用Node；`verify-copper-connectivity.py`、`prepare-manufacturing.py`使用Python及gerbonara1.6.3/shapely2.1.2，预览脚本另需PyMuPDF。铜层连通检查不是阻抗/热/物理接线验证。没有把RC2的USB路径检查次数沿用到本版。
