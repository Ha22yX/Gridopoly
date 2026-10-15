# Hi3D 狐狸棋子：30 mm 圆形平底

已通过 Blender 5.2.2 的 Blender MCP 导入并编辑用户提供的 Hi3D.3mf。原文件保留，现已归档至项目原始输入（本地 `Mechanical/PlayerTokens/sources/hi3d-originals/Hi3D.3mf`）。

## 文件

- Hi3D_30mm.blend：可编辑 Blender 工程，打开后使用 Hi3D_30mm_EDIT 场景；可见打印对象为 Fox_30mm_Print。隐藏对象 REFERENCE_Original_Hi3D 保留原始几何参考；原来的 Blender 场景也保留。
- Hi3D_30mm.3mf：单对象、毫米单位、无预设切片参数的打印模型。
- Hi3D_30mm.stl：毫米坐标的打印模型，导入切片软件时按 mm、100% 比例。
- Hi3D_30mm_preview.png / Hi3D_30mm_bottom.png：结果预览。
- independent_mesh_check.json：从最终 STL 独立测量的检查结果。

## 改动和尺寸

原模型高102.4 mm，底座倾斜约11.56°且略呈椭圆。先扶正主体、均匀缩放为原来的0.49倍，再切除下部旧底座，以直径30 mm、高4.09 mm的新圆柱底座做布尔合并。保留狐狸主体、脚和尾巴连接。

- 底部圆面：直径30.000 mm，Z=0，平整。
- 模型总高：47.710 mm。
- 整体包围盒：30.133 × 30.000 × 47.710 mm。尾巴侧缘略超出圆底投影，底座本身不超出直径30 mm。
- 圆周采用720边离散网格，顶点半径浮点误差小于0.000001 mm。

## 检查

最终STL：1个连通封闭实体，1,543,296个三角面；边界边、非流形边、退化面、共享边方向不一致均为0。底面718个三角面、720个边界顶点，全部Z=0，法线朝下。3MF使用同一最终网格，已检查包CRC和顶点/面计数。

这是网格与尺寸检查，尚未实际打印；未验证特定打印机的最小细节、支撑或成品公差。3MF不沿用原文件中关闭支撑的配置，打印前按实际机器做切片预览。底部留作粘贴tag，未新增凹槽。

## 本目录脚本

inspect_blender.py 用于后台导入与原始模型观察；edit_in_blender.py 记录通过实时MCP执行的修改；render_result.py 为最终模型输出预览。正式交付文件位于项目内 Hi3D_30mm（本地 `Mechanical/PlayerTokens/models/hi3d-30mm/deliverables/Hi3D_30mm`），包含 Blender、3MF、STL 和预览。2026-10-15 仅补齐旧交付的项目归档，没有重新设计或新增实物验收；原下载副本保留。
