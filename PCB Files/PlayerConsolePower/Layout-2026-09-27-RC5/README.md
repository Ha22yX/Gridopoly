# 玩家电池底板 R1 RC5 — J3 极性匹配 EEMB 商品图

本版保留用户已完成的 J3 翻转：**1 脚 GND/BAT−、2 脚 BAT_PACK/BAT+**。PCB 元件面朝向观察者、USB-C 在下方时，J3 左负右正，位置和旋转不变。器件仍为 JST B2B-PH-K-S(LF)(SN)/C131337，匹配 PHR-2 线端。

用户提供的 EEMB LP103454 3.7V/2000mAh、ASIN B08HQWXQ6Y 商品图，金属保持窗口朝向观察者、出线在左、插接端在右，红线上黑线下；依据 JST 官方图纸转换视角，黑线为1脚、红线为2脚，与本版相符。此结论是图纸与商品图核对，**尚未测量所购实物或实物对插**。接入前核实实物极性，本板没有电池反接保护。

翻转后旧 GND 铺铜接触正极焊盘，初查共6项铜到焊盘/孔间距错误。本轮已重铺铜、保存并通过严格 DRC；同时修正原理图说明、元件描述和版号。RC4制造文件保持冻结，不能用于这个极性的电池。

- 68件/243脚、536段走线、82个过孔；80×80mm、双层、器件全顶面。标准型SMT：67件SMT＋1件J3插件。
- PCB原生严格DRC 0项；34项布局/网表检查、11项J3极性/保持性检查通过。
- 独立解析最终Gerber的41个网络，未发现短路、断路、异常NC连接或缺失终端铜。
- 相比RC4，仅J3两脚网络互换、用户改接3段BAT_PACK末端走线、地铜重铺和文字变更；68件位置及82个过孔不变，其他脚网络不变。
- J3最终钻孔仍为2×0.80mm，孔距约2.0015mm（坐标量化）。原理图仍有41条警告待分类。

**制造候选，未放行**：沿用FPC机械/载流、实际电池、原理图警告、工厂DFM和首板电气验证门槛；本次没有下单、采购、实物测试或固件部署。

[复核记录](production-review.md) · [制造说明](manufacturing-notes.md) · [首板清单](bringup-checklist.md) · [顶面预览](pcb-top-fabrication-preview.png) · [极性证据](battery-polarity-review.json)

同版文件：[Gerber](Manufacturing-RC5/PlayerPower-R1-RC5-Gerber.zip)、[BOM](Manufacturing-RC5/PlayerPower-R1-RC5-BOM.csv)、[CPL](Manufacturing-RC5/PlayerPower-R1-RC5-CPL.csv)。NativeExports保留原生输出。RC1～RC4及原投产审查目录未改写；原理图/PCB源码仍含库引用，不能替代完整工程备份。共享索引由总指挥统一归档。
