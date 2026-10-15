# 硬件文档入口

更新：2026-10-15。项目包含长边与角落两种棋盘PCB，以及独立玩家圆屏和USB-C电池底板。下列状态区分当前数字设计、旧批实物、普通固件部署及尚未完成的电气验收；设备在线均为有时间的记录，不是实时保证。

## 玩家电池底板：当前候选与实物分别记录

当前数字候选为 **PLAYER POWER R1 RC6-TP**：80×80mm/R5、双层、1.6mm/1oz、实装器件顶面；76组件（68实装＋8裸测试点）、251焊盘、555线段、84过孔、41网。8测试点单排8mm间距，孔1.0mm/铜盘2.2mm、双面仅网络名；原68件、82旧过孔、原走线物理路径及板框/安装孔保持。完整PCB DRC 0、实际铜层连通及测试点制造层检查通过；原理图43条既有警告已分类，未清除。**静态候选仍为投产HOLD，不等于所有实板功能已验收。**

| 玩家屏最后记录 | 底板／电池与实际反馈 | 仍未确认 |
| --- | --- | --- |
| COM7，普通22ca | 用户报告已返修、带电池；经底板USB升级成功，屏幕稳定、无明显发热 | 定量充放电、热、保护及UART遥测 |
| COM27／COM28，普通dfc4 | 两块底板已返修、带电池；升级后用户确认等待新游戏页正常 | 其他页面、精确输入及电池全项验收 |
| COM29，普通dfc4 | 升级、启动联网通过；最新服务器快照曾有四屏online/eligible | 底板是否返修、是否接电池、画面目视均未获相同确认 |

以上普通固件全部保持 `BatteryBoard=none`，即遥测关闭，不表示实物没有电池。COM7/27/28的用户返修反馈不扩大到其他旧板；USB写验成功和无明显发热反馈不替代充电、放电、欠压、温升或UART实测。见[设备台账](../firmware/current-device-connections.md)和[16格试玩记录](../firmware/trial16-release-2026-10-15.md)。

| 数字文件／历史阶段 | 结论与SHA256 |
| --- | --- |
| [RC6-TP当前候选](../../PCB%20Files/PlayerConsolePower/Testpoints-2026-10-15/README.md) | Gerber `bff4f65a4de8f07b44090f04ccfda05d3c57241564201dea600e5bc1f85e09d3`；测试点增量通过，电气首件条件继续保留 |
| [10月14日全面审查基线](../../PCB%20Files/PlayerConsolePower/ProductionReadiness-2026-10-14/README.md) | Gerber `ff1d6610f86817b8abf856b8e372c66f574020b3824ed76b2c69375bd793d031`；68件/243脚/41网，J3孔0.9mm，静态审查通过范围见原报告 |
| [旧打样故障包](../../PCB%20Files/PlayerConsolePower/OrderPackageAudit-2026-10-14/README.md) | ZIP `2344f34c45307a879c9a3274bddd53681152dcf42d9fb47637cb690d554405b6`；9/27 20:44:56版J3.2保留三条地铜桥，不能重新用于投产 |

10/14查明旧包的BAT_PACK与GND实际铜短路，足以解释用户当时批量约0.2Ω；SYS_RAW约18.92kΩ、5V_SCREEN几MΩ不矛盾。原报告“未返修、断开电池/USB/屏幕”描述当时故障板，不能覆盖上表后续明确返修的板。当前编辑器DRC0不能替代旧订单包检查，数字设计修复也不会自动修复实物。

J2反向焊盘映射与厂家排线逻辑针序分开；J3为JST B2B-PH-K-S(LF)(SN)/C131337，配PHR-2，1脚GND/BAT−、2脚BAT_PACK/BAT+。用户选择标准型SMT并保留U5 TUSB320LAI；实装为67件SMT＋1件PH2.0插件、30料号。RC4的J3极性相反，RC6-TP沿用RC5修正极性，制造资料不得跨版本拼接。

仍待闭合：成品电池包与BQ24074最高4.23V的容差、独立保护，J2实际尾端/接触/峰值，U1热孔与钢网及插件装配，以及充放电、热、欠压和UART首件验收。**无电池供电有条件可用**：当前默认100mA；电脑C-to-C须具备数据且CC宣告至少1.5A，才会进入约894mA输入档，并需按POWER开机；默认电流/A-to-C不保证启动。尚无无电池实测通过结论，详见[USB输入与上传边界](player-usbc-battery-base-2026-09-27.md#5-usb-输入电流与上传边界)。

- [电路、接线、采购与当前四屏状态](player-usbc-battery-base-2026-09-27.md)
- [J3观察方向与黑1红2接线](player-usbc-battery-base-2026-09-27.md#j3-battery-connector) / [测试点位置](player-usbc-battery-base-2026-09-27.md#rc6-tp-testpoints)
- [UART协议](player-battery-telemetry-interface-2026-09-27.md) / [GPIO证据](player-screen-uart-gpio-evidence-2026-09-27.md) / [固件接入](../firmware/player-battery-telemetry-progress-2026-09-27.md)
- [历史RC5制造归档](../../PCB%20Files/PlayerConsolePower/Layout-2026-09-27-RC5/README.md) / [10月5日复核](../../PCB%20Files/PlayerConsolePower/ProductionAudit-2026-10-05/README.md)

## 棋盘长边与角落

长边PCB2_1为141件/底面48件，角落PCB2_5为152件/底面46件；当前相关机械/布局图元与9/21归档一致。角落J301接稳压24V、5.5×2.1mm中心正极；PD/NVM取消，本地支路仍保留TPS26621与LMR16030SDDAR，INA226只测本板5V。24V/5A是适配器选型建议，120W铭牌不提高板端额定；60W整盘/约1W每板仍是待验证预算。

计划16块（12长边＋4角落）已分批完成V0.34写验、启动及联网，13板灯效获用户确认，COM16/17/18待单独目视。原故障COM19仍隔离，替换COM10正常不等于原板已修复；[全白故障原记录](corner-led-white-2026-10-15.md)保持冻结，后续状态以[当前试玩记录](../firmware/trial16-release-2026-10-15.md)为准。完整24V供电、RFID、16板同时在线实体环及整局未验收。服务端已支持目录16/24/32/40 ORDER环，RS485仍禁用，不能借ORDER闭环证明A/B闭环可用。

| 内容 | 文档 |
| --- | --- |
| 工程身份／基线 | [两种PCB](pcb-variants.md)、[硬件基线](hardware-baseline.md) |
| 电源 | [直接24V](corner-dc24-input-2026-09-21.md)、[供电复核](power-schematic-audit-2026-09-21.md)、[外接电源](external-24v-supply.md) |
| GPIO／对插 | [GPIO](esp32-s3-pin-map.md)、[9触点互连](module-interconnect-5wire.md)、[历史二维核查](interconnect-audit-2026-09-21.md) |
| 首板／整盘 | [首板清单](bring-up-checklist.md)、[供电验收](power-system-acceptance.md)、[功耗固件](../firmware/module-power-firmware.md) |
| 机械 | [整体底板首件R2](../../Mechanical/BoardBase/README.md)、[冻结接口](../../Mechanical/BoardBase/interfaces/pcb-interface-2026-10-15/mechanical-interface.json) |
| 历史采购／制造 | [BOM提示](bom-review-2026-09-21.md)、[9月22日汇报](development-handoff-2026-09-22.md)、[批量固件板型审计](batch32-firmware-audit-2026-09-25.md) |

当前R2为一角两长、151×151mm连续L形，PCB底面离桌16mm，Ø6/Ø3.6接触环；3.0×10mm塑料专用螺钉和Ø2.5mm候选盲孔需先试孔，尚未实际切片、打印或试装。用户自然共面、板厚约1.6mm与底面突出约8.1mm已记录；g=9mm仍以约12/15mm来自主体直边为条件。冻结包中的旧14mm架高/滑移提案不改写，现行安装以R2为准。

## 历史效力与发布

`corner-usbc-pd-*`、`corner-pd-esp32-programming-*`、旧218件候选及旧PD固件规格不指导当前采购。R26/R30的C165751缺料为9/21截图记录，本次未查库存，不作为实时缺货结论。10/5“已下单、未收到”及10/14“全部未返修”均按当时时点保留，后续实板状态见上述分设备记录。

[当前项目交接](../CURRENT-DEVELOPMENT-HANDOFF.md)统一记录部署与待办。原理图、PCB、BOM/CPL、Gerber、固件及实机验收分别冻结；棋盘当前实物批次未与制造文件哈希绑定前，不能把工程树名字当作订单版本，也不能跨版本拼接通过结论。
