# 玩家电池底板 R1 RC4 — J3 改为 PH2.0 插座

按用户要求，将 J3 裸焊线孔替换为 JST B2B-PH-K-S(LF)(SN)，立创 C131337，2.0 mm 间距、2P、直插、向上插线。原理图和 PCB 已同步、保存。J3 中心保持 (40,5) mm；PCB 元件面朝向观察者、USB-C 在下方时，左侧 1 脚 BAT+，右侧 2 脚 GND。电池端使用匹配 PHR-2 插壳/端子，不根据商家“正向/反向”命名推断极性。

本版是未放行的制造候选。既有 FPC、电池规格、原理图警告明细及首板验证门槛仍在；没有生产、采购或实物插接测试。

- 68 件/243 脚，536 段走线、82 个过孔。双层、器件全顶面；装配为 67 件 SMT 加 1 件 J3 插件。
- 原生 PCB DRC 0 项；原理图仍返回 41 条 warning 总数。
- 34 项布局/网表/制造清单检查、9 项 J3 更换检查通过。原始电气网络与 RC3 全部一致，其他 67 件位置和所有过孔未变。
- 实际 Gerber 铜层 41 个电气网络，未发现短路、断路、异常 NC 连接或缺失终端铜。
- J3 最终钻孔文件为两孔 0.80 mm，孔距约 2.0015 mm（坐标量化），外壳包络无器件碰撞。
- 装配 BOM/CPL 均含 68 件、30 个料号；J3 不再排除。直插件需要工厂插件焊接或后焊，不能当作纯 SMT 件。

[制造说明](manufacturing-notes.md) · [更换复核](production-review.md) · [顶面预览](pcb-top-fabrication-preview.png) · [J3 检查证据](j3-connector-verification.json)

同版文件： [Gerber](Manufacturing-RC4/PlayerPower-R1-RC4-Gerber.zip)、[BOM](Manufacturing-RC4/PlayerPower-R1-RC4-BOM.csv)、[CPL](Manufacturing-RC4/PlayerPower-R1-RC4-CPL.csv)。NativeExports 为原生输出，Manufacturing-RC4 是去重复钻孔、规范 CSV 后的输出。

RC1、RC2、RC3 和原审查目录冻结保留。此目录的两页原理图源码与 PCB 源码保留库引用，不能替代含全部库资源的原工程备份。共享索引由总指挥统一提交。
