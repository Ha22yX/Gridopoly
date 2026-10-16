# 机械与3D开发

更新：2026-10-16。这里汇总3D建模会话已交付的版本及未完成的实物验证。所有模型已归档在项目的 `Mechanical/` 目录，下方相对链接按公开仓库的目录组织；[3D模型公开入口](../../Mechanical/README.md)汇总当前版、历史版及原始输入，[模型资产清单](../../Mechanical/model-assets-inventory.json)记录来源与SHA256。本页提供下载路径与版本说明，不作为远端上传完成或下载验收记录。

大型模型通过Git LFS获取：在GitHub文件页使用 **Download raw file**，或克隆仓库后执行 `git lfs pull`。不要把只包含 `version https://git-lfs.github.com/spec/v1`、`oid`、`size` 的指针文本当作模型保存。此次整理复用既有文件，未重新生成模型或扩大实物验收范围。

## 当前交付

| 项目 | 已完成 | 尚待完成 |
| --- | --- | --- |
| 棋盘三模块底板 R2_PRINT_FRIENDLY | 一角两长，151×151×16.8mm；用户已提供照片并确认打印件配合正常 | 该反馈不覆盖全环、长—长接缝、螺纹及承载强度 |
| 旧玩家支架 CW47.5 | 用户已打印试装，圆环、定位柱及排线槽配合正常 | 这项反馈仅覆盖旧支架与屏幕接口 |
| 玩家底座 V2 R3 EAR_REINFORCED | 修复R2左螺丝耳根部过薄，新增实际STL截面检查；只替换02支架，其余4种打印件保持R2 | 切片、实际打印/受力、孔位、60mm排线松弛及整机试装/温升 |
| 16模块底板 Full16 R1 | 4个相同L主件＋8片连接片；4角12长、中央开口；布局/网格/3MF/参考PCB干涉检查通过 | 长—长接缝首件、完整切片打印、闭环共面及拼接承载 |
| 四角色棋子30mm圆底 | 狐狸、河狸、猫头鹰、乌龟均有STL、3MF、可编辑Blend、预览和检查报告；原始5个3MF已归档 | 各角色的切片支撑、薄壁、实际打印与标签粘贴试装 |

## 棋盘底板

[R2使用与安装说明](../../Mechanical/BoardBase/README.md)包括紧固件、试孔片、打印方向和边界条件；[参数](../../Mechanical/BoardBase/fixture-config.json)、[网格报告](../../Mechanical/BoardBase/verification-report.json)、[装配几何报告](../../Mechanical/BoardBase/assembly-verification.json)和[PCB冻结接口](../../Mechanical/BoardBase/interfaces/pcb-interface-2026-10-15/README.md)用于追溯。

R2主件4944顶点、9884三角面，单一闭合实体；底板与三块带孔PCB参考板材的相交量为数值舍入级。底部元件采用用户约8.1mm突出量，板间候选间距g=9mm由用户12/15mm量测推算，量测边界仍待确认。没有逐件实物高度或连接器受力验证。

交付目录与ZIP分别为[R2打印模型目录](../../Mechanical/BoardBase/deliverables/Gridopoly_BoardBase_Test_R2_PRINT_FRIENDLY/)及[R2完整ZIP](../../Mechanical/BoardBase/deliverables/Gridopoly_BoardBase_Test_R2_PRINT_FRIENDLY.zip)。主件STL SHA256为`73fc697ded0568f2a538dded4f091c5967b1cf053c343de166dee7ebb6297d9f`，3MF为`6368145f7cc50e049c4edf11f4377d656fe494df8a0eb85ac8d419c6830deeb5`。这些是既有交付记录，本次文档整合没有重新生成模型。

用户随后确认三模块角落首件配合正常，并允许分两块或四块打印。现已交付[Full16 R1](../../Mechanical/BoardBase/Full16_R1/README.md)：4个相同L主件，每件一角三长，配8片连接片。主框366×366mm，含外侧连接耳406×406mm；单主件171.35×234.35×16.8mm。中央名义190mm方形开口局部受内耳占用，不能当成完整净空。

交付目录与ZIP分别为[Full16打印模型目录](../../Mechanical/BoardBase/deliverables/Gridopoly_BoardBase_Full16_R1/)及[Full16完整ZIP](../../Mechanical/BoardBase/deliverables/Gridopoly_BoardBase_Full16_R1.zip)，ZIP SHA256 `4853d19975a7ea492aa121c104e5aa4d5f8d5e2a55a0872389e01e3f5bc9ae82`。主会话独立复核ZIP CRC和清单16项哈希，源脚本语法/JSON可解析；几何报告来自3D会话，未在主会话重新生成模型。长—长15mm间距仍需一个L首件试装，全环切片、打印和强度未验。旧R0独立底座/滑动连接及R1底部螺母槽已被后续要求覆盖。

## 玩家底座

当前候选为[V2 R3安装耳加固版](../../Mechanical/PlayerConsoleStandV2/revisions/R3_EAR_REINFORCED/README.md)。用户在切片预览中发现R2左耳几乎断开，实际STL复查确认根部只剩微小搭接；旧版“网格连通”检查没有覆盖根部可打印宽度。**R2的02屏幕支架需换成R3，其他四种零件无需重打。**

R3左耳以4mm厚底脚绕开FPC槽，实际截面最小连续宽12.3565mm、面积55.02723mm²；右根14mm、56mm²。截面是几何证据，不是承载能力。两孔中心、Ø3.6、安装高度及CW47.5配合区保持；3D会话新增根部截面检查，并完成网格、3MF回读和参考装配检查。

交付目录与ZIP分别为[R3打印模型目录](../../Mechanical/PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R3_EAR_REINFORCED/)及[R3完整ZIP](../../Mechanical/PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R3_EAR_REINFORCED.zip)，ZIP SHA256 `a53e49f96728a5a599b9d9f58acf080ced5d289ea7b18482faafa261bdc868d1`。主会话独立核对ZIP CRC、清单22项哈希，以及四种沿用件共8个STL/3MF与R2冻结清单一致。源脚本语法/JSON可解析；没有重新执行Blender建模，也没有新增实物验收。

[R3中文图文装配PDF](../../Mechanical/PlayerConsoleStandV2/revisions/R3_EAR_REINFORCED/assembly_guide/Gridopoly_Player_Stand_R3_Assembly_Guide_CN.pdf) 已纳入本轮文档归档，已交付，共6页，包含零件清单、装配分解图、螺丝叠装顺序、排线与电池安装；PDF SHA256 `7b0e46071d452e26ab985766fbfee0fd17e8bb84ea0c6ce7b47b5f6eaa25fa62`。PDF单独归档，未改动冻结的R3模型ZIP；图文说明不新增实物验收结论。

其余尺寸和装配继续依据[V2 R2 FPC60](../../Mechanical/PlayerConsoleStandV2/revisions/R2_FPC60/README.md)：连接板116×111×4mm、支架74×50×41.8645mm、电池仓84×17×39mm、盖69×22×4mm及四颗6mm隔柱。PCB四孔中心距70×70mm，主固定M3×18，两个分件各用两颗M3×12；紧固件不可沿用R1长度。用户更正原排线为约60mm，中心线路径含假定4mm插入段约56.843mm，计算余量不代表实物必定装得下。

R3的[零件检查](../../Mechanical/PlayerConsoleStandV2/revisions/R3_EAR_REINFORCED/outputs/parts-verification.json)、[实际根部截面](../../Mechanical/PlayerConsoleStandV2/revisions/R3_EAR_REINFORCED/outputs/ear-root-verification.json)及[装配检查](../../Mechanical/PlayerConsoleStandV2/revisions/R3_EAR_REINFORCED/outputs/assembly-verification.json)已归档，仍待切片打印、承载和完整试装。

以下R2历史报告保留原检查范围，不能再据此声称02耳根合格：[零件检查](../../Mechanical/PlayerConsoleStandV2/revisions/R2_FPC60/outputs/parts-verification.json)、[装配检查](../../Mechanical/PlayerConsoleStandV2/revisions/R2_FPC60/outputs/assembly-verification.json)、[交付清单](../../Mechanical/PlayerConsoleStandV2/revisions/R2_FPC60/outputs/delivery-manifest.json)已归档。五件闭合网格和3MF回读通过，1300组装配、190组FPC、95组电池线包络相交检查未见超过0.001mm³的碰撞；真实接触面/逐针方向、线根位置、打印及受力仍待验。

历史交付目录与ZIP分别为[R2历史模型目录](../../Mechanical/PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R2_FPC60/)及[R2历史ZIP](../../Mechanical/PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R2_FPC60.zip)，ZIP SHA256 `e9423d0cfea4763f37fc5e61b87f293a546d00b152c9d93a735560699073b4d3`。主会话独立核对ZIP CRC及清单19项哈希；R2的02支架仅供历史追溯，当前打印使用R3。

### R1历史

[V2 R1说明](../../Mechanical/PlayerConsoleStandV2/README.md)、[设计参数](../../Mechanical/PlayerConsoleStandV2/design-parameters.json)、[RC6-TP机械接口](../../Mechanical/PlayerConsoleStandV2/interfaces/player-power-2026-10-15/README.md)、[网格报告](../../Mechanical/PlayerConsoleStandV2/outputs/verification-report.json)和[装配报告](../../Mechanical/PlayerConsoleStandV2/outputs/assembly-verification.json)构成本版资料；[旧CW47.5来源](../../Mechanical/PlayerConsoleStand/README.md)保留。

V2 R1采用94×82mm上支架与独立托盘，电池仓净60×38×14mm；PCB四孔中心距70×70mm。默认8mm隔柱配M3×35四颗及8mm贯通内螺纹绝缘底脚；选择12mm隔柱必须改用M3×40，不能混用尺寸链。屏幕继续采用旧版可拔重力定位，四螺丝固定支架、托盘与PCB，并不锁住屏幕本体。

四个独立打印网格通过检查，旧屏幕接口2004顶点保留；489对装配参考几何未发现超阈值相交。参考元件高度、排线示意及碰撞结果不能替代实物测量。R1交付时排线有效长度尚待用户提供；后续用户更正约60mm，已由上方R2替代，R1不再用于该排线的打印推荐。

历史交付目录与ZIP分别为[R1历史模型目录](../../Mechanical/PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R1/)及[R1历史ZIP](../../Mechanical/PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R1.zip)，ZIP SHA256 `6a990f137abd7e299e5a29c362010bede4b99bd517f83860a417cc16b2c16644`。这是首版CAD交付，尚不是最终装配验收。用户已返修供电底板的状态按[当前硬件记录](../hardware/player-usbc-battery-base-2026-09-27.md)区分设备；模型完成不改变RC6-TP制造或电气验收状态。

## 四角色棋子

本次为既有四角色交付整理公开下载入口，并核实历史文件；四个棋子不是今天重建的模型，没有修改几何。此前“PlayerTokens没有新的完成证据”的概括不能理解为没有旧交付。三角色说明记录完成于2026-10-01，其中乌龟来自用户当时后补的 `Hi3D(5).3mf`。

| 角色 | 原始输入 | 总高 | 打印3MF下载 |
| --- | --- | --- | --- |
| 狐狸 | `Hi3D.3mf` | 47.710mm | [Hi3D_30mm.3mf](../../Mechanical/PlayerTokens/models/hi3d-30mm/deliverables/Hi3D_30mm/Hi3D_30mm.3mf) |
| 河狸 | `Hi3D(2).3mf` | 38.882mm | [Beaver_30mm.3mf](../../Mechanical/PlayerTokens/models/hi3d-remaining-30mm/deliverables/Gridopoly_30mm_Remaining/Beaver_30mm.3mf) |
| 猫头鹰 | `Hi3D(3).3mf` | 43.923mm | [Owl_30mm.3mf](../../Mechanical/PlayerTokens/models/hi3d-remaining-30mm/deliverables/Gridopoly_30mm_Remaining/Owl_30mm.3mf) |
| 乌龟 | `Hi3D(5).3mf` | 36.530mm | [Turtle_30mm.3mf](../../Mechanical/PlayerTokens/models/hi3d-remaining-30mm/deliverables/Gridopoly_30mm_Remaining/Turtle_30mm.3mf) |

四个底面均为直径30mm的平圆面，供粘贴tag，未加凹槽；角色的尾巴等局部可以伸出圆底投影。每个角色的STL、可编辑Blend、正面/底面预览和检查报告与上表3MF同目录。设计记录见[狐狸说明](../../Mechanical/PlayerTokens/models/hi3d-30mm/README.md)和[其余三角色说明](../../Mechanical/PlayerTokens/models/hi3d-remaining-30mm/README.md)。[Gridopoly_Three_Tokens_30mm_Print.zip](../../Mechanical/PlayerTokens/models/hi3d-remaining-30mm/deliverables/Gridopoly_30mm_Remaining/Gridopoly_Three_Tokens_30mm_Print.zip)不包含狐狸；同目录 `Gridopoly_Three_Tokens_Working.blend` 是编辑检查点，独立打印优先使用各自3MF。

[Hi3D原始输入目录](../../Mechanical/PlayerTokens/sources/hi3d-originals/)完整保留5个3MF；`Hi3D(4).3mf` 与 `(3)` 的SHA256相同，属于重复输入，不是第五个角色。四个既有网格报告均记录单一连通封闭实体、30mm平圆底以及边界边/非流形边/退化面为0。本次再次验证四个3MF和三角色ZIP的CRC无误，四个STL文件长度与三角面数一致；归档与来源逐项SHA256相同。没有新增切片、实物打印、薄壁或标签粘贴验收证据。

## 后续闭合条件

1. 棋盘三模块首件已获用户确认；Full16先打印一个L主件验证新增的长—长接缝和四板自然落柱，再完成其余三块与八片连接片，检查闭环、平整和承载。
2. 玩家底座只将02换成R3支架，其余用R2；先检查切片中的耳根连续性，再打印核对孔位、60mm排线松弛和整机装配。
3. 真实装配完成后检查接口可操作性、旋钮自由转动、线材松弛、充放电与温升。
4. 四个棋子按各自模型检查支撑、薄壁与切片预览，再验证30mm底面贴tag和实物使用；数字模型交付不等于实物打印验收。

其余项目责任见[当前交接](../CURRENT-DEVELOPMENT-HANDOFF.md)。
