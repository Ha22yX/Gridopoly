# 长边模块与角落模块：当前 PCB 设计

更新：2026-09-21。本文是两种 PCB 的当前身份、接口及制造交接规范；原理图实现与 PCB 完成状态分别记录。

## 两种板型

| 项目 | 长边模块 | 角落模块 |
| --- | --- | --- |
| 工程树 | 长边模块 / Schematic1 / p1 | 角落模块 / Schematic1_1 / p1 |
| 原理图 UUID | `262b1ccbed23ba63` | `ae9c8047ce85d7f8` |
| PCB | PCB2_1 | PCB2_2 |
| PCB UUID | `7840864f79449b47` | `d03bb2d6c85bb69d` |
| 外形 | 长方形，尺寸以当前 PCB 板框及装配图为准 | 80 × 80 mm，圆角方板 |
| 板间公母接口 | 相对边 | 相邻边；俯视时母座在公针所在边顺时针 90° 的边，例如左公、上母 |
| 本地 24V→5V 保护 | TPS26621 + LMR16030SDDAR | 相同 |
| USB1 PD→24V 源 | 无 | STUSB4500 + TPS26632 + LM5176 + LM74700 |
| J1 USB | 本地 5V/调试 | 本地 5V/调试，也用于首次 PD 配置供电 |
| GPIO11/12 | INA226 | INA226 与 STUSB4500 共享 I²C |
| GPIO13 | 未连接 | PD_RST，高有效 |

旧名 Board1 对应长边开发沿革，不能再代表唯一 PCB。日期命名的备份不作为当前交付板。
两种棋盘板都不是独立的 480×480 玩家圆形终端；后者仍由 Firmware/PlayerConsole 管理。

## 共同核心与接口

共同核心为 ESP32-S3-WROOM-1-N16R8、J4 2×4 ST7789 接口、HTRC110、10 颗 WS2812B-B-V6、INA226、RS485 和 ORDER。J4 是屏幕 8Pin 母座，与已删除的 PD 编程排针 J101 无关。

2026-09-21 两份原理图导出网表一致采用每侧 **6Pin + 3Pin，共 9 触点**，不是旧版 15 触点磁吸结构：

| 位号 | 器件 | 立创编号 | 网表针序 |
| --- | --- | --- | --- |
| JIN1 | HX PZ2.54-1x6P WZ，弯插公针 | C32713265 | 1/2/3=24V_BUS，4=ORDER_IN，5=BUS_A，6=BUS_B |
| JOUT1 | LAIL-PM2.54-6P-W，弯插母座 | C54973819 | 1/2/3=24V_BUS，4=ORDER_OUT，5=BUS_A，6=BUS_B |
| JIN3 | PZ254R-11-03P，弯插公针 | C492411 | 1/2/3=GND |
| JOUT3 | LAIL-PM2.54-3P-W，弯插母座 | C54973822 | 1/2/3=GND |

数字相同是网表事实，不能代替实物对插验证。必须按实际封装 Pin1、正反面、弯脚方向、中心高度和插入行程核对：电源对电源、地对地、A 对 A、B 对 B、OUT 对 IN。禁止照旧文档把 JOUT1.3 当 ORDER。
每侧仅有 3 个正极及 3 个地触点；不得再按 6 个并联触点计算载流能力，也不得把额定单触点电流简单相加当长期能力。

## 已完成的角落 PCB 工作

以下是 USB PD 电源原理图扩展前的 PCB 记录，不代表后来的全部供电电路已经布线。

- `92291a2`：147 个独立焊盘转换为原生过孔，保留位置、网络、钻孔、铜径、阻焊等；原生板级过孔从 56 变为 203，独立焊盘剩余 0。元件封装内焊盘保持不变。
- `49ae287`：优化 C53 输入旁路与 C56 自举电容位置、局部电源回路及部分冗余走线；补充地过孔，重建 5 个铺铜。
- 将穿过 ESP32 天线下方的多层信号线绕开，建立约 18×6 mm 禁布区。受固定 J4 与接口限制，未满足完整 15 mm 周边净空，射频性能仍需实测。
- 保持 80×80 板框、J4、10 颗 LED、板间连接器和安装孔位置。用户取消 LED 整体重排后，最终没有把它写成已完成的 LED 重定位。
- 当时原生严格 DRC 错误列表为空；独立铜重叠和小于 4mil 间距检查为 0。独立 DSN 检查不包含铺铜，不能作为后来新增电源电路的验收。

记录：[过孔核对](../../PCB%20Files/CornerModule/pad-to-via-verification.json)、[布局布线核对](../../PCB%20Files/CornerModule/layout-routing-verification.json)、[当时 PCB 导出](../../PCB%20Files/CornerModule/PCB2_2.epcb)。

## 原理图已更新，PCB 待同步

两种板的本地电源区均为原有 17 件 + 新增 8 件，共 25 件；U23 替换为 S 型。角落另有 PD 源及最新 ESP32 配置连接。尚未完成这些新增/换网器件的 PCB 同步、布线、热设计和制造输出。

同步顺序：先核对长边的 U201 与 7 个阻容实际占地，再完成角落入口功率区；保留现有机械接口，按 Unique ID 更新原件，不重复新增原有 17 件。具体布局约束见[统一接入保护](unified-module-hotplug-power-2026-09-21.md)。

关键检查包括 HP_RTN 与 GND 分离、保护后储能、U23 SS、C128 靠近 VSYS、四开关高 di/dt 回路、自举与栅极回路、4mΩ Kelvin 采样、USB1 全电源焊尾载流、MOS/电感散热、天线净空和真实过孔载流。

## 证据优先级

| 范围 | 当前受版本控制的参考 |
| --- | --- |
| 最新角落原理图 | [PDProgramming](../../PCB%20Files/CornerModule/PDProgramming/corner-schematic.esch) 与同目录 net / verification.json |
| 最新长边电源原理图 | [PowerIntegrity 长边](../../PCB%20Files/ModulePower/PowerIntegrity/long-schematic.esch) 与同目录 net |
| 理论计算 | [PowerIntegrity](../../PCB%20Files/ModulePower/PowerIntegrity/power-integrity-results.json)，R120=5.6k |
| 器件采购快照 | [Hotplug BOM](../../PCB%20Files/ModulePower/Hotplug/BOM-stock.csv)，须结合最新 C128/R120 记录 |
| 编辑中的完整工程 | PCB Files/Gridopoly.eprj2；本地未提交变化须另核对，不能认为与上述导出自动相同 |

PD24 → Hotplug → PowerIntegrity → PDProgramming 是角落电源记录的先后顺序；旧文件保留用于追溯，不能相互覆盖。最新 PDProgramming 仅更新角落；长边仍参考 PowerIntegrity。下单前必须生成同一工程快照的原理图、PCB、BOM、坐标和 Gerber。
