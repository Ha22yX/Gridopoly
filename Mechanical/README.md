# Gridopoly 3D 模型与装配资料

更新：2026-10-16。这里是 Gridopoly 的 3D 打印与机械设计入口，包含当前候选、历史打印版、可编辑 Blender 工程、原始输入和装配说明。模型按毫米导出，导入切片软件保持 100% 比例。

## 先从这些文件开始

| 设计 | 打印与编辑文件 | 说明及状态 |
| --- | --- | --- |
| 玩家屏幕底座 V2 R3 | [完整模型目录](PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R3_EAR_REINFORCED)、Blender [装配](PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R3_EAR_REINFORCED/Gridopoly_Player_Stand_V2_R3_EAR_REINFORCED.blend)、完整 [ZIP](PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R3_EAR_REINFORCED.zip) | [R3 版本说明](PlayerConsoleStandV2/revisions/R3_EAR_REINFORCED/README.md)。02 支架安装耳已加固，其余四种零件沿用 R2；新版整机实装待验 |
| 图文装配说明 | [玩家屏幕底座 R3 中文 PDF](PlayerConsoleStandV2/revisions/R3_EAR_REINFORCED/assembly_guide/Gridopoly_Player_Stand_R3_Assembly_Guide_CN.pdf) | 6 页，零件、紧固件、装配顺序、排线与电池位置 |
| 16 模块环形棋盘底板 | [完整模型目录](BoardBase/deliverables/Gridopoly_BoardBase_Full16_R1)、Blender [装配](BoardBase/deliverables/Gridopoly_BoardBase_Full16_R1/Gridopoly_Full16_R1.blend)、[ZIP](BoardBase/deliverables/Gridopoly_BoardBase_Full16_R1.zip) | [打印与安装说明](BoardBase/Full16_R1/README.md)。4 个相同 L 主件＋8 片连接片，中央留空；长—长接缝及整环实装待验 |
| 三模块角落测试底板 R2 | [打印模型及工程](BoardBase/deliverables/Gridopoly_BoardBase_Test_R2_PRINT_FRIENDLY)、[ZIP](BoardBase/deliverables/Gridopoly_BoardBase_Test_R2_PRINT_FRIENDLY.zip) | [使用说明](BoardBase/README.md)。用户已确认该版本角落首件配合正常 |
| 旧屏幕接口 CW47.5 | [STL](PlayerConsoleStand/outputs/models/Gridopoly_Player_Console_Stand_CW47_5.stl)、[Blender](PlayerConsoleStand/outputs/models/Gridopoly_Player_Console_Stand_CW47_5.blend) | [旧版说明](PlayerConsoleStand/README.md)。用户已确认屏幕接口试装正常，作为新支架配合基线 |
| 四个 30 mm 圆底棋子 | [棋子总入口](PlayerTokens/README.md) | 狐狸、河狸、猫头鹰、乌龟，均含 STL、3MF、Blender；网格检查通过，实物打印未确认 |

R3 已打印其他零件时，只需替换 [02_Screen_Support_CW47_5_R3.3mf](PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R3_EAR_REINFORCED/02_Screen_Support_CW47_5_R3.3mf)。不要继续打印 R2 的旧 02 支架。

## 下载与打开

点击模型链接进入文件页，再选 **Download raw file** 下载。`.3mf`/`.stl` 用切片软件打开，`.blend` 用 Blender 打开；装配场景中的 PCB、螺丝、线缆等参考对象不要一起打印。3MF 不含 H2D 打印机、耗材或支撑预设。

大型文件由 Git LFS 管理。克隆仓库前安装 Git LFS，在仓库目录运行 `git lfs install` 和 `git lfs pull`；如果下载内容只有几行 `version https://git-lfs.github.com/spec/v1` 文本，拿到的是指针，需要获取真实对象。GitHub 自动生成的整仓 ZIP 是否包含 LFS 对象取决于仓库设置，不作为本项目完整下载的唯一入口。参见 [GitHub 的 Git LFS 说明](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-git-large-file-storage)。

## 历史版本与原始输入

- [棋盘底板全部版本](BoardBase/deliverables)：R0 独立底板方案和 R1 底部螺母槽方案保留溯源，当前三模块使用 R2，整盘使用 Full16 R1。
- [玩家屏幕底座全部版本](PlayerConsoleStandV2/deliverables)：R1 较高夹层结构不适合当前约 60 mm 排线；R2 的 02 支架由 R3 替代。
- [旧屏幕支架模型](PlayerConsoleStand/outputs/models)：CW50、M4_Pin_Down 和早期测试版本保留；当前已试装接口基线为 CW47.5。
- Hi3D [原始输入](PlayerTokens/sources/hi3d-originals)：5 个原始文件完整保留；`Hi3D(4).3mf` 与 `(3)` 内容相同，最后一个不同角色来自 `(5)`。
- [棋子参考资料](PlayerTokens/references)保留原有参考图与单色参考图包。

## 完整范围与校验

2026-10-15 本地归档补齐 **165 个文件，1,756,231,888 字节（约 1.76 GB）**，每个源文件与项目副本均核对大小及 SHA256；未删除下载目录原件，未重打包历史 ZIP。项目 `Mechanical/` 共收录 81 个模型文件（包括历史版、原始输入和 Blender 备份，不代表 81 个独立设计）。三张用户角落实装照片也已保存到 `BoardBase/references/fit-photos-2026-10-15/`。完整来源与校验值见 [归档清单](model-assets-inventory.json)。

分类目录内的脚本已改用项目内输入和输出路径，未重新运行建模或改变模型几何。冻结交付中的旧 README、参数、重建脚本和报告保留原字节，可能仍记录初次交付的 Downloads 路径；当前使用入口以本页和分类 README 为准。复现时先运行相应项目源目录的准备步骤生成路径配置，并为新修订指定独立目录，避免覆盖冻结交付。

此次公开清单覆盖全部 58 个 STL/3MF（含 5 个原始输入）、正式 Blender 工程、既有 ZIP 与必要资料。6 个 `.blend1` 自动备份和 2 个 `Working.blend` 中间工程留在本地恢复档案，不作为发布模型；它们的对应正式工程已经列入。逐文件路径、大小、SHA256 和例外见[完整公开清单](PUBLICATION.md)及[机器可读清单](public-model-inventory.json)。归档与公开模型不代表新增实物验收。

后续保存遵循[本目录约定](AGENTS.md)。历史报告保留当时状态，新实装反馈应另行记录。
