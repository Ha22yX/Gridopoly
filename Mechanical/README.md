# Gridopoly 3D 模型与装配资料

更新：2026-10-15。正式模型统一保存在本项目 `Mechanical/` 下；Downloads 中的旧副本继续保留。可编辑工程、打印模型、原始输入、预览、检查报告和装配 PDF 均可从此进入。

## 当前使用入口

| 设计 | 打印与编辑文件 | 说明及状态 |
| --- | --- | --- |
| 玩家屏幕底座 V2 R3 | 完整模型目录（本地 `Mechanical/PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R3_EAR_REINFORCED`）、Blender 装配（本地 `Mechanical/PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R3_EAR_REINFORCED/Gridopoly_Player_Stand_V2_R3_EAR_REINFORCED.blend`）、完整 ZIP（本地 `Mechanical/PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R3_EAR_REINFORCED.zip`） | [R3 版本说明](PlayerConsoleStandV2/revisions/R3_EAR_REINFORCED/README.md)。02 支架安装耳已加固，其余四种零件沿用 R2；新版整机实装待验 |
| 图文装配说明 | [玩家屏幕底座 R3 中文 PDF](PlayerConsoleStandV2/revisions/R3_EAR_REINFORCED/assembly_guide/Gridopoly_Player_Stand_R3_Assembly_Guide_CN.pdf) | 6 页，零件、紧固件、装配顺序、排线与电池位置 |
| 16 模块环形棋盘底板 | 完整模型目录（本地 `Mechanical/BoardBase/deliverables/Gridopoly_BoardBase_Full16_R1`）、Blender 装配（本地 `Mechanical/BoardBase/deliverables/Gridopoly_BoardBase_Full16_R1/Gridopoly_Full16_R1.blend`）、ZIP（本地 `Mechanical/BoardBase/deliverables/Gridopoly_BoardBase_Full16_R1.zip`） | [打印与安装说明](BoardBase/Full16_R1/README.md)。4 个相同 L 主件＋8 片连接片，中央留空；长—长接缝及整环实装待验 |
| 三模块角落测试底板 R2 | 打印模型及工程（本地 `Mechanical/BoardBase/deliverables/Gridopoly_BoardBase_Test_R2_PRINT_FRIENDLY`）、ZIP（本地 `Mechanical/BoardBase/deliverables/Gridopoly_BoardBase_Test_R2_PRINT_FRIENDLY.zip`） | [使用说明](BoardBase/README.md)。用户已确认该版本角落首件配合正常 |
| 旧屏幕接口 CW47.5 | STL（本地 `Mechanical/PlayerConsoleStand/outputs/models/Gridopoly_Player_Console_Stand_CW47_5.stl`）、Blender（本地 `Mechanical/PlayerConsoleStand/outputs/models/Gridopoly_Player_Console_Stand_CW47_5.blend`） | [旧版说明](PlayerConsoleStand/README.md)。用户已确认屏幕接口试装正常，作为新支架配合基线 |
| 四个 30 mm 圆底棋子 | [棋子总入口](PlayerTokens/README.md) | 狐狸、河狸、猫头鹰、乌龟，均含 STL、3MF、Blender；网格检查通过，实物打印未确认 |

R3 已打印其他零件时，只需替换 02_Screen_Support_CW47_5_R3.3mf（本地 `Mechanical/PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R3_EAR_REINFORCED/02_Screen_Support_CW47_5_R3.3mf`）。不要继续打印 R2 的旧 02 支架。

## 历史版本与原始输入

- 棋盘底板全部版本（本地 `Mechanical/BoardBase/deliverables`）：R0 独立底板方案和 R1 底部螺母槽方案保留溯源，当前三模块使用 R2，整盘使用 Full16 R1。
- 玩家屏幕底座全部版本（本地 `Mechanical/PlayerConsoleStandV2/deliverables`）：R1 较高夹层结构不适合当前约 60 mm 排线；R2 的 02 支架由 R3 替代。
- 旧屏幕支架模型（本地 `Mechanical/PlayerConsoleStand/outputs/models`）：CW50、M4_Pin_Down 和早期测试版本保留；当前已试装接口基线为 CW47.5。
- Hi3D 原始输入（本地 `Mechanical/PlayerTokens/sources/hi3d-originals`）：5 个原始文件完整保留；`Hi3D(4).3mf` 与 `(3)` 内容相同，最后一个不同角色来自 `(5)`。
- 棋子参考资料（本地 `Mechanical/PlayerTokens/references`）保留原有参考图与单色参考图包。

## 归档与后续保存

本次补齐 **165 个文件，1,756,231,888 字节（约 1.76 GB）**，每个源文件与项目副本均核对大小及 SHA256；未删除下载目录原件，未重打包历史 ZIP。项目 `Mechanical/` 共收录 81 个模型文件（包括历史版、原始输入和 Blender 备份，不代表 81 个独立设计）。三张用户角落实装照片也已保存到 `BoardBase/references/fit-photos-2026-10-15/`。完整来源与校验值见 [归档清单](model-assets-inventory.json)。

分类目录内的脚本已改用项目内输入和输出路径，未重新运行建模或改变模型几何。冻结交付中的旧 README、参数、重建脚本和报告保留原字节，可能仍记录初次交付的 Downloads 路径；当前使用入口以本页和分类 README 为准。复现时先运行相应项目源目录的准备步骤生成路径配置，并为新修订指定独立目录，避免覆盖冻结交付。

后续保存遵循 [本目录约定](AGENTS.md)。本次是项目文件归档，不代表已上传 GitHub 或完成新增实物验收。
