# 机械与3D开发

更新：2026-10-15。这里汇总3D建模会话已交付的版本及未完成的实物验证。源参数、接口提取和检查报告纳入Git；本机Downloads中的打印包、Blender工作文件及自动备份不是GitHub发布附件。

## 当前交付

| 项目 | 已完成 | 尚待完成 |
| --- | --- | --- |
| 棋盘三模块底板 R2_PRINT_FRIENDLY | 一个连续L形底板固定一角两长；151×151×16.8mm；12柱、6颗塑料专用螺钉；取消R1底部螺母槽。导出网格、3MF回读、装配参考几何通过 | 螺钉试孔、自然插合孔距、切片、打印及实物安装 |
| 旧玩家支架 CW47.5 | 用户已打印试装，圆环、定位柱及排线槽配合正常 | 这项反馈仅覆盖旧支架与屏幕接口 |
| 玩家底座 V2 R1 | 首版模型与交付包完成；复用CW47.5接口，上支架＋电池托盘＋隔柱，下方固定80×80mm供电PCB | 排线有效长度、实物元件/电池/紧固件、切片打印、整机试装与温升 |
| 完整16模块整体底板 | 用户要求及三模块首件设计已记录 | 整体制造方案、最终模型与实物验证；不能把三模块首件当作完整16板交付 |

## 棋盘底板

[R2使用与安装说明](../../Mechanical/BoardBase/README.md)包括紧固件、试孔片、打印方向和边界条件；[参数](../../Mechanical/BoardBase/fixture-config.json)、[网格报告](../../Mechanical/BoardBase/verification-report.json)、[装配几何报告](../../Mechanical/BoardBase/assembly-verification.json)和[PCB冻结接口](../../Mechanical/BoardBase/interfaces/pcb-interface-2026-10-15/README.md)用于追溯。

R2主件4944顶点、9884三角面，单一闭合实体；底板与三块带孔PCB参考板材的相交量为数值舍入级。底部元件采用用户约8.1mm突出量，板间候选间距g=9mm由用户12/15mm量测推算，量测边界仍待确认。没有逐件实物高度或连接器受力验证。

本机交付目录为`C:/Users/kicof/Downloads/Gridopoly_BoardBase_Test_R2_PRINT_FRIENDLY/`。主件STL SHA256为`73fc697ded0568f2a538dded4f091c5967b1cf053c343de166dee7ebb6297d9f`，3MF为`6368145f7cc50e049c4edf11f4377d656fe494df8a0eb85ac8d419c6830deeb5`。这些是既有交付记录，本次文档整合没有重新生成模型。

按现候选间距，完整16板底座外宽约366mm，超过当前H2D常规平放范围；首件验证之后再确定整体制造方式。旧R0独立底座/滑动连接及R1底部螺母槽均已被后续要求覆盖。

## 玩家底座

[V2 R1说明](../../Mechanical/PlayerConsoleStandV2/README.md)、[设计参数](../../Mechanical/PlayerConsoleStandV2/design-parameters.json)、[RC6-TP机械接口](../../Mechanical/PlayerConsoleStandV2/interfaces/player-power-2026-10-15/README.md)、[网格报告](../../Mechanical/PlayerConsoleStandV2/outputs/verification-report.json)和[装配报告](../../Mechanical/PlayerConsoleStandV2/outputs/assembly-verification.json)构成本版资料；[旧CW47.5来源](../../Mechanical/PlayerConsoleStand/README.md)保留。

V2 R1采用94×82mm上支架与独立托盘，电池仓净60×38×14mm；PCB四孔中心距70×70mm。默认8mm隔柱配M3×35四颗及8mm贯通内螺纹绝缘底脚；选择12mm隔柱必须改用M3×40，不能混用尺寸链。屏幕继续采用旧版可拔重力定位，四螺丝固定支架、托盘与PCB，并不锁住屏幕本体。

四个独立打印网格通过检查，旧屏幕接口2004顶点保留；489对装配参考几何未发现超阈值相交。参考元件高度、排线示意及碰撞结果不能替代实物测量。排线实际有效长度尚待用户提供；既有走线路径长度不作为采购尺寸。

本机交付包为`C:/Users/kicof/Downloads/Gridopoly_Player_Stand_V2_R1.zip`，SHA256 `6a990f137abd7e299e5a29c362010bede4b99bd517f83860a417cc16b2c16644`。这是首版CAD交付，尚不是最终装配验收。用户已返修供电底板的状态按[当前硬件记录](../hardware/player-usbc-battery-base-2026-09-27.md)区分设备；模型完成不改变RC6-TP制造或电气验收状态。

## 后续闭合条件

1. 棋盘先打印试孔片，再确认三板自然插合、底板孔位与底部空间；通过后扩展整盘。
2. 玩家底座先量原排线、电池成品尺寸及PCB最高实装件，再切片和试装。
3. 真实装配完成后检查接口可操作性、旋钮自由转动、线材松弛、充放电与温升。

PlayerTokens本轮没有新的完成或验收证据，不随本次文档更新扩大其状态。其余项目责任见[当前交接](../CURRENT-DEVELOPMENT-HANDOFF.md)。
