# 玩家供电板 RC6-TP：单排通孔测试点

2026-10-15，按用户要求增加 8 个通孔测试点，并集中排成一行。顶、底丝印只显示实际网络名，隐藏 TP 位号；原理图保留 TP1–TP8 作为内部识别。当前嘉立创EDA工程已保存，板上版本标识为 `PLAYER POWER R1 RC6-TP`。

**本次测试点增量已验证。整板仍延续 [10 月 14 日审查](../ProductionReadiness-2026-10-14/README.md) 的 `PRODUCTION_RELEASE_HOLD / NO_PHYSICAL_VALIDATION`；本轮没有进行实板充电、烧录或负载测试，也不修复已收到的旧批 BAT 短路实物。**

## 位置与规格

从元件面看、USB-C 在下方；坐标原点为板框左上角，X 向右、Y 向下，单位 mm。8 个孔均在 Y=22.5，间距 8 mm。

| 从左到右 | 内部位号 | 丝印／网络 | X | 用途 |
| --- | --- | --- | ---: | --- |
| 1 | TP7 | GND | 8 | 测量参考地 |
| 2 | TP3 | 5V_SCREEN | 16 | 屏幕 5 V 输出 |
| 3 | TP4 | SCREEN_IO3V3 | 24 | 屏幕侧接口 3.3 V |
| 4 | TP8 | GND | 32 | 第二个测量参考地 |
| 5 | TP5 | USB_VBUS | 40 | USB 输入电压 |
| 6 | TP2 | SYS_RAW | 48 | 充电／电源路径输出 |
| 7 | TP1 | BAT_PACK | 56 | 电池接口正极 |
| 8 | TP6 | POWER_EN | 64 | 升压使能状态 |

全部为名义 **1.0 mm 金属化通孔、2.2 mm 圆铜盘**，双面阻焊开窗约 2.3 mm，无钢网开口。支线宽 0.30 mm，供万用表、示波器和调试引线测量使用，不作为负载供电接口。测试点没有实装器件，不增加采购料号。

新增 15 段测试支线，USB_VBUS 测试支路使用 2 枚 0.30 mm 孔／0.60 mm 铜盘的盖油过孔。板框、安装孔、USB-C、按钮、J2、J3 及原 68 个器件位置不变；原有全部走线几何、线宽、网络及 82 枚过孔均保留。部分旧线在分支接入点分段，总计为 76 个组件（68 个实装＋8 个裸测试点）、251 个组件焊盘、555 段线、84 枚过孔、41 个网络。

## 本轮检查

- 原生 PCB 完整 DRC：零错误，含原理图网表一致性检查，见 `drc-after.json`。
- 原理图 DRC：仍为原有 43 条警告，没有新增警告，见 `schematic-drc-after.json`；既有供应商属性警告的审查边界见 10 月 14 日记录。
- 独立核验实际 Gerber：8 个孔均在 PTH 钻孔表，孔径约 1.000001 mm；顶底铜盘约 2.200 mm、阻焊开窗约 2.301 mm，8 点均无钢网开口。
- 实际铜层几何分析覆盖 41 网、251 焊盘、84 过孔、555 段线，未检出断路、异网短路、NC 误接或缺失铜。
- 已查看实际顶底制造图，网络名完整、方向正确，不压孔、不重叠。
- TP1、TP4、TP6、TP7 的 PCB 使用实例局部封装 ID；其网络和实际导出的孔径、铜盘均已逐点检查，不以共享封装 ID 替代实体几何验证。

制造层复核详见 [独立报告](independent-testpoint-manufacturing-review.md)，[顶面预览](actual-testpoints-top.png)和[底面预览](actual-testpoints-bottom.png)均从本包实际 Gerber 渲染。

## 文件使用

- `current-Gerber.zip`：本次最终铜层、阻焊、丝印、钻孔；SHA256 为 `bff4f65a4de8f07b44090f04ccfda05d3c57241564201dea600e5bc1f85e09d3`。
- `BOM-manufacturing.csv`、`CPL-manufacturing.csv`：68 个原实装器件；CPL 已明确移除 TP1–TP8。保留原生 TSV 作为对照，不使用含测试点的 `CPL-native.tsv` 下贴装单。
- `PlayerPower-R1-RC6-TP-20261015.epro2`：PCB 原生文档与关联库数据。
- `PlayerPower-R1-RC6-TP-P1-20261015.epro2`、`PlayerPower-R1-RC6-TP-P2-20261015.epro2`：两页原理图的分别导出，并非整个 Gridopoly 工程包；完整工程仍保存在上级 `Gridopoly.eprj2`。
- `PCB1-after.source.jsonl`、`P1-after.source.jsonl`、`P2-after.source.jsonl`、两份 netlist、`snapshot-after.json`：最终源数据和连接证据。
- `testpoints.csv`：测试点位置及网络表；`manifest.json`、`SHA256SUMS.txt`：版本边界和文件校验。

本目录不包含试错阶段的中间制造包，不覆盖此前冻结审查或订单包。原批短路板的处理仍以 [订单包检查](../OrderPackageAudit-2026-10-14/README.md) 为准。
