# 机械与3D开发

更新：2026-10-15。这里汇总3D建模会话已交付的版本及未完成的实物验证。源参数、接口提取和检查报告纳入Git；本机Downloads中的打印包、Blender工作文件及自动备份不是GitHub发布附件。

## 当前交付

| 项目 | 已完成 | 尚待完成 |
| --- | --- | --- |
| 棋盘三模块底板 R2_PRINT_FRIENDLY | 一角两长，151×151×16.8mm；用户已提供照片并确认打印件配合正常 | 该反馈不覆盖全环、长—长接缝、螺纹及承载强度 |
| 旧玩家支架 CW47.5 | 用户已打印试装，圆环、定位柱及排线槽配合正常 | 这项反馈仅覆盖旧支架与屏幕接口 |
| 玩家底座 V2 R3 EAR_REINFORCED | 修复R2左螺丝耳根部过薄，新增实际STL截面检查；只替换02支架，其余4种打印件保持R2 | 切片、实际打印/受力、孔位、60mm排线松弛及整机试装/温升 |
| 16模块底板 Full16 R1 | 4个相同L主件＋8片连接片；4角12长、中央开口；布局/网格/3MF/参考PCB干涉检查通过 | 长—长接缝首件、完整切片打印、闭环共面及拼接承载 |

## 棋盘底板

[R2使用与安装说明](../../Mechanical/BoardBase/README.md)包括紧固件、试孔片、打印方向和边界条件；[参数](../../Mechanical/BoardBase/fixture-config.json)、[网格报告](../../Mechanical/BoardBase/verification-report.json)、[装配几何报告](../../Mechanical/BoardBase/assembly-verification.json)和[PCB冻结接口](../../Mechanical/BoardBase/interfaces/pcb-interface-2026-10-15/README.md)用于追溯。

R2主件4944顶点、9884三角面，单一闭合实体；底板与三块带孔PCB参考板材的相交量为数值舍入级。底部元件采用用户约8.1mm突出量，板间候选间距g=9mm由用户12/15mm量测推算，量测边界仍待确认。没有逐件实物高度或连接器受力验证。

本机交付目录为`C:/Users/kicof/Downloads/Gridopoly_BoardBase_Test_R2_PRINT_FRIENDLY/`。主件STL SHA256为`73fc697ded0568f2a538dded4f091c5967b1cf053c343de166dee7ebb6297d9f`，3MF为`6368145f7cc50e049c4edf11f4377d656fe494df8a0eb85ac8d419c6830deeb5`。这些是既有交付记录，本次文档整合没有重新生成模型。

用户随后确认三模块角落首件配合正常，并允许分两块或四块打印。现已交付[Full16 R1](../../Mechanical/BoardBase/Full16_R1/README.md)：4个相同L主件，每件一角三长，配8片连接片。主框366×366mm，含外侧连接耳406×406mm；单主件171.35×234.35×16.8mm。中央名义190mm方形开口局部受内耳占用，不能当成完整净空。

本机包 `C:/Users/kicof/Downloads/Gridopoly_BoardBase_Full16_R1.zip`，SHA256 `4853d19975a7ea492aa121c104e5aa4d5f8d5e2a55a0872389e01e3f5bc9ae82`。主会话独立复核ZIP CRC和清单16项哈希，源脚本语法/JSON可解析；几何报告来自3D会话，未在主会话重新生成模型。长—长15mm间距仍需一个L首件试装，全环切片、打印和强度未验。旧R0独立底座/滑动连接及R1底部螺母槽已被后续要求覆盖。

## 玩家底座

当前候选为[V2 R3安装耳加固版](../../Mechanical/PlayerConsoleStandV2/revisions/R3_EAR_REINFORCED/README.md)。用户在切片预览中发现R2左耳几乎断开，实际STL复查确认根部只剩微小搭接；旧版“网格连通”检查没有覆盖根部可打印宽度。**R2的02屏幕支架需换成R3，其他四种零件无需重打。**

R3左耳以4mm厚底脚绕开FPC槽，实际截面最小连续宽12.3565mm、面积55.02723mm²；右根14mm、56mm²。截面是几何证据，不是承载能力。两孔中心、Ø3.6、安装高度及CW47.5配合区保持；3D会话新增根部截面检查，并完成网格、3MF回读和参考装配检查。

本机包 `C:/Users/kicof/Downloads/Gridopoly_Player_Stand_V2_R3_EAR_REINFORCED.zip`，SHA256 `a53e49f96728a5a599b9d9f58acf080ced5d289ea7b18482faafa261bdc868d1`。主会话独立核对ZIP CRC、清单22项哈希，以及四种沿用件共8个STL/3MF与R2冻结清单一致。源脚本语法/JSON可解析；没有重新执行Blender建模，也没有新增实物验收。

其余尺寸和装配继续依据[V2 R2 FPC60](../../Mechanical/PlayerConsoleStandV2/revisions/R2_FPC60/README.md)：连接板116×111×4mm、支架74×50×41.8645mm、电池仓84×17×39mm、盖69×22×4mm及四颗6mm隔柱。PCB四孔中心距70×70mm，主固定M3×18，两个分件各用两颗M3×12；紧固件不可沿用R1长度。用户更正原排线为约60mm，中心线路径含假定4mm插入段约56.843mm，计算余量不代表实物必定装得下。

R3的[零件检查](../../Mechanical/PlayerConsoleStandV2/revisions/R3_EAR_REINFORCED/outputs/parts-verification.json)、[实际根部截面](../../Mechanical/PlayerConsoleStandV2/revisions/R3_EAR_REINFORCED/outputs/ear-root-verification.json)及[装配检查](../../Mechanical/PlayerConsoleStandV2/revisions/R3_EAR_REINFORCED/outputs/assembly-verification.json)已归档，仍待切片打印、承载和完整试装。

以下R2历史报告保留原检查范围，不能再据此声称02耳根合格：[零件检查](../../Mechanical/PlayerConsoleStandV2/revisions/R2_FPC60/outputs/parts-verification.json)、[装配检查](../../Mechanical/PlayerConsoleStandV2/revisions/R2_FPC60/outputs/assembly-verification.json)、[交付清单](../../Mechanical/PlayerConsoleStandV2/revisions/R2_FPC60/outputs/delivery-manifest.json)已归档。五件闭合网格和3MF回读通过，1300组装配、190组FPC、95组电池线包络相交检查未见超过0.001mm³的碰撞；真实接触面/逐针方向、线根位置、打印及受力仍待验。

本机包为 `C:/Users/kicof/Downloads/Gridopoly_Player_Stand_V2_R2_FPC60.zip`，SHA256 `e9423d0cfea4763f37fc5e61b87f293a546d00b152c9d93a735560699073b4d3`。主会话独立核对ZIP CRC及清单19项哈希；源参数和报告入Git，打印二进制包仍在本机。

### R1历史

[V2 R1说明](../../Mechanical/PlayerConsoleStandV2/README.md)、[设计参数](../../Mechanical/PlayerConsoleStandV2/design-parameters.json)、[RC6-TP机械接口](../../Mechanical/PlayerConsoleStandV2/interfaces/player-power-2026-10-15/README.md)、[网格报告](../../Mechanical/PlayerConsoleStandV2/outputs/verification-report.json)和[装配报告](../../Mechanical/PlayerConsoleStandV2/outputs/assembly-verification.json)构成本版资料；[旧CW47.5来源](../../Mechanical/PlayerConsoleStand/README.md)保留。

V2 R1采用94×82mm上支架与独立托盘，电池仓净60×38×14mm；PCB四孔中心距70×70mm。默认8mm隔柱配M3×35四颗及8mm贯通内螺纹绝缘底脚；选择12mm隔柱必须改用M3×40，不能混用尺寸链。屏幕继续采用旧版可拔重力定位，四螺丝固定支架、托盘与PCB，并不锁住屏幕本体。

四个独立打印网格通过检查，旧屏幕接口2004顶点保留；489对装配参考几何未发现超阈值相交。参考元件高度、排线示意及碰撞结果不能替代实物测量。R1交付时排线有效长度尚待用户提供；后续用户更正约60mm，已由上方R2替代，R1不再用于该排线的打印推荐。

本机交付包为`C:/Users/kicof/Downloads/Gridopoly_Player_Stand_V2_R1.zip`，SHA256 `6a990f137abd7e299e5a29c362010bede4b99bd517f83860a417cc16b2c16644`。这是首版CAD交付，尚不是最终装配验收。用户已返修供电底板的状态按[当前硬件记录](../hardware/player-usbc-battery-base-2026-09-27.md)区分设备；模型完成不改变RC6-TP制造或电气验收状态。

## 后续闭合条件

1. 棋盘三模块首件已获用户确认；Full16先打印一个L主件验证新增的长—长接缝和四板自然落柱，再完成其余三块与八片连接片，检查闭环、平整和承载。
2. 玩家底座只将02换成R3支架，其余用R2；先检查切片中的耳根连续性，再打印核对孔位、60mm排线松弛和整机装配。
3. 真实装配完成后检查接口可操作性、旋钮自由转动、线材松弛、充放电与温升。

PlayerTokens本轮没有新的完成或验收证据，不随本次文档更新扩大其状态。其余项目责任见[当前交接](../CURRENT-DEVELOPMENT-HANDOFF.md)。
