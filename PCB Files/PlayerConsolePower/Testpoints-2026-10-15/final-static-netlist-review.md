# 单排测试点最终静态复核

2026-10-15；只读复核，未修改 EDA。

结论：PASS（静态网表、原布局保留、BOM及已绑定的 Gerber 尺寸证据）。正式 CPL/BOM 逗号 CSV 也已通过逐单元格验证。

- SCH/PCB 均为 76 个器件，251 个引脚端点的脚号及网名全部一致。
- 原 68 个器件在两端均无网络变化；位号、UID、PCB 实例 ID、坐标、旋转及层均保持不变。
- 8 个 TP 的 Description 已恢复一致，BOM=no、Convert to PCB=yes。原生 BOM 仍为 68 个实装器件。
- PCB 原生完整 DRC 返回空数组；SCH 原生结果仍为既有 43 条警告。
- TP1、TP4、TP6、TP7 的 PCB 使用实例局部封装 ID；SCH 使用共享封装 217839e496899ffc。此差异已明确保留，不声称两端封装 ID 字面完全一致。
- 独立制造审核与当前 Gerber、snapshot 的 SHA256 完全匹配，证明 8 个 TP 顶底铜盘均约 2.2 mm、金属化孔约 1.0 mm、双面开窗、无钢网开口；因此上述局部封装在本次交付尺寸及电气连接上等效。

| 位号 | 网络 | X/mm | Y 向下/mm |
|---|---|---:|---:|
| TP1 | BAT_PACK | 56.002 | 22.499 |
| TP2 | SYS_RAW | 48.001 | 22.499 |
| TP3 | 5V_SCREEN | 15.999 | 22.499 |
| TP4 | SCREEN_IO3V3 | 23.998 | 22.499 |
| TP5 | USB_VBUS | 40.000 | 22.499 |
| TP6 | POWER_EN | 64.000 | 22.499 |
| TP7 | GND | 8.001 | 22.499 |
| TP8 | GND | 31.999 | 22.499 |

8 个测试点处于同一横排（Y≈22.5 mm）。坐标基于板顶视图，X 向右、Y 向下。

该结果不代表整机功能已经实测，亦不替代此前充电/USB供电条件及实物装配审查。

证据：同目录 final-static-netlist-review.json、independent-testpoint-manufacturing-review.json、drc-after.json、manufacturing-csv-validation.json。正式 CPL-manufacturing.csv 仅移除 TP1–TP8，其他 68 行及所有值保持不变；BOM-manufacturing.csv 仅将原生 TSV 转为逗号 CSV，68 个实装器件不变。
