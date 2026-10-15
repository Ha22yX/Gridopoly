# 四个 30 mm 圆底棋子

四个角色的原始模型、可编辑工程和打印模型均已保存在项目内。底面直径 30 mm、平整，用于粘贴 tag；角色局部可能伸出圆底投影。模型按毫米导出，切片使用 100% 比例。

| 角色 | 打印 3MF | 可编辑 Blender | 总高 |
| --- | --- | --- | ---: |
| 狐狸 | Hi3D_30mm.3mf（本地 `Mechanical/PlayerTokens/models/hi3d-30mm/deliverables/Hi3D_30mm/Hi3D_30mm.3mf`） | Hi3D_30mm.blend（本地 `Mechanical/PlayerTokens/models/hi3d-30mm/deliverables/Hi3D_30mm/Hi3D_30mm.blend`） | 47.710 mm |
| 河狸 | Beaver_30mm.3mf（本地 `Mechanical/PlayerTokens/models/hi3d-remaining-30mm/deliverables/Gridopoly_30mm_Remaining/Beaver_30mm.3mf`） | Beaver_30mm.blend（本地 `Mechanical/PlayerTokens/models/hi3d-remaining-30mm/deliverables/Gridopoly_30mm_Remaining/Beaver_30mm.blend`） | 38.882 mm |
| 猫头鹰 | Owl_30mm.3mf（本地 `Mechanical/PlayerTokens/models/hi3d-remaining-30mm/deliverables/Gridopoly_30mm_Remaining/Owl_30mm.3mf`） | Owl_30mm.blend（本地 `Mechanical/PlayerTokens/models/hi3d-remaining-30mm/deliverables/Gridopoly_30mm_Remaining/Owl_30mm.blend`） | 43.923 mm |
| 乌龟 | Turtle_30mm.3mf（本地 `Mechanical/PlayerTokens/models/hi3d-remaining-30mm/deliverables/Gridopoly_30mm_Remaining/Turtle_30mm.3mf`） | Turtle_30mm.blend（本地 `Mechanical/PlayerTokens/models/hi3d-remaining-30mm/deliverables/Gridopoly_30mm_Remaining/Turtle_30mm.blend`） | 36.530 mm |

各目录同时保存同名 STL、正面/底面预览和独立网格检查报告。详细设计记录见[狐狸](models/hi3d-30mm/README.md)和[其余三角色](models/hi3d-remaining-30mm/README.md)。四件均已交付，几何报告记录为单一封闭实体；尚未获得实物打印验证。

原始输入（本地 `Mechanical/PlayerTokens/sources/hi3d-originals`）保留 `Hi3D.3mf`、`Hi3D(2).3mf`、`Hi3D(3).3mf`、`Hi3D(4).3mf` 和 `Hi3D(5).3mf`。其中 `(4)` 与 `(3)` 完全重复，最终乌龟采用 `(5)`。

`Gridopoly_Three_Tokens_30mm.blend` 为河狸、猫头鹰、乌龟总览；`Gridopoly_Three_Tokens_Working.blend` 是编辑检查点。打印使用各角色独立 3MF/STL，避免导出 Blender 中隐藏的原始参考模型。

2026-10-15 从下载目录原样归档，校验值见[总清单](../model-assets-inventory.json)。原始下载副本保留，后续模型以项目内文件为正式交付。
