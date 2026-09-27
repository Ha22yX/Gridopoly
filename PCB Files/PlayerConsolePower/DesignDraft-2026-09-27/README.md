# 玩家电池底板原理图草稿产物

更新：2026-09-27。七项功能电路已接入同一Schematic2的P1/P2；PCB1未布线，未制造放行。

- P1-power.source.jsonl / P2-usb-interfaces.source.jsonl：嘉立创EDA原生源码快照，保留器件符号/封装引用，依赖原工程器件库，不是独立可下单工程包。
- 同名SVG/PNG：原生导出预览。导出器仍可能夹有已删除文字缓存，源码及网表才是连接核对依据；完整视觉QA未清除。
- native-netlist.json：68个器件，与68个原理图符号一致。J2候选母座已参与PCB/BOM，机械兼容性待实物确认。
- expected-connectivity.json / connectivity-audit.json：222个连接脚、21个空网络脚一致；另核对J2的12个符号引脚。位号/唯一编号无重复。
- PCB1-components.source.jsonl / pcb-component-import.json：实际PCB已包含68个器件，243个引脚网络与原理图一致；J2的1–10信号焊盘已反转。PCB仍待完成布局布线。
- electrical-review.json：79项料号、供电、引脚和既有电源/USB保留检查通过；不代表电气性能或实板测试通过。
- bom-draft.json / bom-draft.csv：67个拟贴装器件、29种料号。J3仅裸焊线孔，不装排针；J4已删除，R21=10k固定TS偏置。
- sourcing-snapshot.json：查询时库存覆盖10板基础用量；SC18IM704贴装库存25颗，余量有限。没有锁料。
- fpc-orientation.svg：用户确认的触点观察方向，不代表母座焊盘已完成机械匹配。
- design-contract.json：要求、实现和未完成项；design-overview.svg为功能框图，不是PCB封装布局。

新增U9 SC18IM704与U10 MAX17048；原排线RX/TX查询电量和CHG/PGOOD。U9随屏幕关闭，U10保留电池供电；MAX17048不要启用总线低电平触发的sleep。底板无独立固件，玩家端源码及主机回归已接入，构建与实物验收状态见[固件接入记录](../../../Docs/firmware/player-battery-telemetry-progress-2026-09-27.md)。本轮未烧录现场屏幕。

剩余：FPC尾端厚度/母座方向/载流，电池规格，40条原生DRC warning明细，屏幕UART集成，PCB及实板验证。无外置温度探头，也不检测电芯温度。USB上传先开屏，普通USB端口不保证运行时净充电。

完整说明：[硬件规格](../../../Docs/hardware/player-usbc-battery-base-2026-09-27.md)、[屏幕UART接口](../../../Docs/hardware/player-battery-telemetry-interface-2026-09-27.md)。

离线复核：在本目录运行 `node verify-schematic.cjs`，无需EDA、网络、外部包或设备；J2几何快照绑定P2源码SHA-256，源码变更后须重新导出。verification-baseline.json只保留遥测修改前的连接意图供差异检查，不是当前设计。当前原生P1/P2的SVG/PNG因文字显示缓存未清除，仅作本地诊断，建议不纳入Git提交；以本目录release-file-list.txt列出的文件为审核范围。
