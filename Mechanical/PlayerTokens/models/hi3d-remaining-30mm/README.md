# Gridopoly：三个棋子的 30 mm 圆底版本

完成日期：2026-10-01。通过 Blender MCP 在独立 Blender 窗口中实际导入、旋正并编辑；原始3MF保留。

| 输入 | 角色 / 输出名 | 底面直径 | 总高 | 重建底座高度 |
| --- | --- | --- | --- | --- |
| Hi3D(2).3mf | 河狸 / Beaver_30mm | 30 mm | 38.882 mm | 4.55 mm |
| Hi3D(3).3mf | 猫头鹰 / Owl_30mm | 30 mm | 43.923 mm | 4.03 mm |
| Hi3D(5).3mf | 乌龟 / Turtle_30mm | 30 mm | 36.530 mm | 5.20 mm |

Hi3D(4).3mf 与 Hi3D(3).3mf 完全相同，按用户后续指示以Hi3D(5)作为第三个角色。

每个角色均包含 .blend 可编辑工程、.3mf 和 .stl 打印网格、_preview.png 正面预览、_bottom.png 底面预览和 _mesh_check.json 验证报告。单件Blender场景中隐藏的 REFERENCE_Hi3D_*_Original 对象保留原始几何，打印对象名为 *_30mm_Print。

[Gridopoly_Three_Tokens_30mm.blend](deliverables/Gridopoly_30mm_Remaining/Gridopoly_Three_Tokens_30mm.blend) 是三件总览及独立场景集合，包含三件并排的总览。独立打印请使用各自3MF或STL，避免误导出隐藏参考模型。Working文件是编辑过程检查点。

三个模型分别拟合原始底平面及底座尺寸，旋正后均匀缩放，切去原底座下部，再与精确直径30 mm、720边的圆柱布尔合并。保留主体、脚和尾巴，底面全部位于Z=0。底座高度根据各原模型的连接过渡分别设置，不扭曲主体比例。

独立读取最终STL检查：每个模型均为一个连通封闭实体；边界边、非流形边、退化面和共享边方向不一致均为0。底圆周顶点半径15 mm，浮点误差小于0.000001 mm。3MF均为毫米单位、单对象，已检查包CRC和顶点/面计数，没有沿用原文件切片配置。

底面尺寸不等于整个角色都在同一个直径30 mm的圆柱内；河狸尾部有斜向伸出，不影响底部贴tag。

导入STL时使用毫米和100%比例。尚未实际打印，需按打印机检查支撑、薄壁和切片预览。底面保持平整用于粘贴tag，没有新增凹槽。

项目交付目录：[Gridopoly_30mm_Remaining](deliverables/Gridopoly_30mm_Remaining/)，含三个角色的 Blender、3MF、STL、预览、总览工程和报告。[原始输入](../../sources/hi3d-originals/)保留全部 5 个文件，其中 `(4)` 是 `(3)` 的重复副本。本项目目录同时保存实时编辑脚本、实测参数和网格验证报告。2026-10-15 仅补齐既有交付的归档，没有重新设计模型或新增实物验收；原下载副本保留。


## 下载入口

| 角色 | 打印3MF | STL | 可编辑Blender |
| --- | --- | --- | --- |
| 河狸 | [Beaver_30mm.3mf](deliverables/Gridopoly_30mm_Remaining/Beaver_30mm.3mf) | [STL](deliverables/Gridopoly_30mm_Remaining/Beaver_30mm.stl) | [Blender](deliverables/Gridopoly_30mm_Remaining/Beaver_30mm.blend) |
| 猫头鹰 | [Owl_30mm.3mf](deliverables/Gridopoly_30mm_Remaining/Owl_30mm.3mf) | [STL](deliverables/Gridopoly_30mm_Remaining/Owl_30mm.stl) | [Blender](deliverables/Gridopoly_30mm_Remaining/Owl_30mm.blend) |
| 乌龟 | [Turtle_30mm.3mf](deliverables/Gridopoly_30mm_Remaining/Turtle_30mm.3mf) | [STL](deliverables/Gridopoly_30mm_Remaining/Turtle_30mm.stl) | [Blender](deliverables/Gridopoly_30mm_Remaining/Turtle_30mm.blend) |

[三角色打印ZIP](deliverables/Gridopoly_30mm_Remaining/Gridopoly_Three_Tokens_30mm_Print.zip)包含上述三个角色的3MF和报告，不包含狐狸。全部四角色见[棋子总览](../../README.md)，其他机械设计见[3D模型公开入口](../../../README.md)。

2026-10-16仅整理既有模型的下载入口，没有重新设计三个模型。Git LFS文件在GitHub文件页使用 **Download raw file**，或克隆后执行 `git lfs pull`；不要保存以 `version https://git-lfs.github.com/spec/v1` 开头的指针文本作为模型。本说明不表示远端上传或下载验收已完成。
