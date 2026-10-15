# 硬件文档入口

基线更新：2026-09-27；玩家供电PCB专项补充：2026-10-14。项目包含两种棋盘PCB，以及独立玩家圆屏和新增玩家电池底板。硬件审查统一由PCB任务负责；本页汇总PCB任务已交付的设计证据，主任务文档整合未操作在线工程或实物。

2026-10-05玩家底板补充：用户已下单、尚未收到。现场与RC5电气/机械图元一致，重新检查PCB DRC 0、243脚同步及41网制造铜层连通通过；订单文件尚待对应。新增BQ24074充电上限4.23V与EEMB公开电芯规格上限4.22V的容差缺口，普通电脑USB口100mA供电限制仍存在。原理图警告、接口/USB/充电实板验收未完成；[本轮全面复核](../../PCB%20Files/PlayerConsolePower/ProductionAudit-2026-10-05/README.md) · [到货测试](../../PCB%20Files/PlayerConsolePower/ProductionAudit-2026-10-05/arrival-tests.md)。RC5冻结文件未改动。

## 玩家电池底板：实物 BAT 端批量低阻，暂停通电

**2026-10-14 用户确认已收到嘉立创代工板，所有板 BAT+／BAT− 持续约 0.2Ω；5V_SCREEN 对地几 MΩ，C3 SYS_RAW 对地约 18.92kΩ。** 当前排查 BAT 直接支路 J3/C2/C19/U1/U10，故障未定位、未修复。现场 PCB DRC 0、243 脚同步和本次新 Gerber 41 网铜层检查通过，仅说明当前数字设计；实际订单文件尚未对应。历史 J3 翻转后旧地铜曾碰 BAT+、后于 RC5 修复，需重点核对是否用了中间版本。原理图 API 当前返回 43 条警告计数，待分类。[故障调查与实测证据](../../PCB%20Files/PlayerConsolePower/BatShortAudit-2026-10-14/README.md)。


当前候选 **R1-RC5**：80×80mm/R5、双层、1.6mm/1oz、器件顶面；68件/243脚、536线段/82过孔。J2焊盘1–10反转对应厂家排线逻辑针序；J3沿用PH2.0-2P并翻转极性，JST B2B-PH-K-S(LF)(SN)/C131337，1脚GND/BAT−、2脚BAT_PACK/BAT+，匹配PHR-2线端。

用户已选择标准型SMT，保留U5 TUSB320LAIRWBR；67件SMT＋1件插件、30料号，J3需插件焊接或后焊。PCB DRC 0、34项布局检查、11项J3检查及41网铜层连通审查通过，37份交付哈希核验通过；这些结论只覆盖归档文件，不代表实物装配已成功。

**已到货，BAT 端批量低阻故障未修复，禁止以旧静态检查作为通电放行依据**：实际订单版本和成品电池包充电电压容差待核对；当前43条原理图警告明细待分类；J2供应商500mA/触点与屏幕实际峰值、排线0.30±0.03mm尾端/接触面/通断、1S电池保护与电流、USB/充电/热/欠压首板验收待完成。电池遥测固件已构建，默认关闭，尚未部署或连接底板。

- [电路、接线与采购说明](player-usbc-battery-base-2026-09-27.md)
- [J3规格、观察方向与黑1红2接线速查](player-usbc-battery-base-2026-09-27.md#j3-battery-connector)
- [RC5制造归档](../../PCB%20Files/PlayerConsolePower/Layout-2026-09-27-RC5/README.md) / [投产复核](../../PCB%20Files/PlayerConsolePower/Layout-2026-09-27-RC5/production-review.md) / [首板清单](../../PCB%20Files/PlayerConsolePower/Layout-2026-09-27-RC5/bringup-checklist.md)
- [UART协议](player-battery-telemetry-interface-2026-09-27.md) / [厂家GPIO证据](player-screen-uart-gpio-evidence-2026-09-27.md) / [固件接入](../firmware/player-battery-telemetry-progress-2026-09-27.md)

旧DesignDraft及RC1～RC4原始文件/哈希保留追溯。RC4的J3极性与RC5相反，当前所选电池使用RC5同版资料。按键内部常通组错误已修复为1/2接控制、3/4接GND；旧97afc2d不作为制造版本，81项修复检查不等于41条原生警告已分类。

## 棋盘长边与角落

长边PCB2_1、角落PCB2_5；角落J301使用稳压24V、5.5×2.1mm中心正。PD电路/NVM已取消。两板支路保留TPS26621、LMR16030SDDAR，INA226只测本板5V。181项供电引脚断言及四种组合36触点二维核对是设计证据；最终制造revision、机械、射频、电气和整盘仍待验。

| 内容 | 文档 |
| --- | --- |
| 工程身份与基线 | [两种PCB](pcb-variants.md)、[硬件基线](hardware-baseline.md) |
| 当前电源 | [直接24V](corner-dc24-input-2026-09-21.md)、[供电复核](power-schematic-audit-2026-09-21.md)、[外接电源](external-24v-supply.md) |
| GPIO/对插 | [GPIO](esp32-s3-pin-map.md)、[9触点互连](module-interconnect-5wire.md)、[历史二维核查](interconnect-audit-2026-09-21.md) |
| 首板/整盘 | [首板](bring-up-checklist.md)、[供电验收](power-system-acceptance.md)、[功耗固件](../firmware/module-power-firmware.md) |
| 采购/制造 | [历史BOM提示](bom-review-2026-09-21.md)、[9月22日汇报](development-handoff-2026-09-22.md)、[批量固件板型审计](batch32-firmware-audit-2026-09-25.md) |

24V/5A是适配器选型建议，120W铭牌不等于棋盘额定；60W整盘/约1W每板是待验预算。R26/R30的C165751缺料是9月21日截图记录，本次未查当前库存或替料，不作为实时缺货结论。

V0.33显式角色profile已实现；完整32 ORDER环及四角规则已在服务端实现，实物待验。旧“ORDER必须断开”的方案不再作为当前软件限制；RS485仍未启用，A/B启用前仍需线型断点/终端。[现行拓扑契约](../firmware/ring-onboarding-contract-2026-09-26.md)

## 历史效力与发布

`corner-usbc-pd-*`、`corner-pd-esp32-programming-*`、旧218器件布局和PD固件规格不指导当前采购或制造；支路保护设计中未被改版替代的部分仍保留。订单制造revision没有与可核对的包绑定前，不能把本地PCB名字当已下单版本。

[当前项目交接](../CURRENT-DEVELOPMENT-HANDOFF.md)统一记录部署与待办。原理图、PCB、BOM/CPL、Gerber、固件和实机验收分别冻结，不能跨版本拼接通过结论。
