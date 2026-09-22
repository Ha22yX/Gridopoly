# 硬件文档入口

更新：2026-09-22。当前有长边 PCB2_1 与角落 PCB2_5 两种棋盘板。角落采用外接稳压24V，USB-PD→24V及PD配置需求已经取消；旧资料只供追溯。

| 阅读顺序 | 文档与用途 |
| --- | --- |
| 1 | [开发汇报与待办](development-handoff-2026-09-22.md)：本任务开发范围、证据、责任和未完成事项 |
| 2 | [硬件基线](hardware-baseline.md)、[两种PCB](pcb-variants.md)：版本、器件和工程身份 |
| 3 | [直接24V入口](corner-dc24-input-2026-09-21.md)、[两板供电原理图复核](power-schematic-audit-2026-09-21.md) |
| 4 | [GPIO/接口](esp32-s3-pin-map.md)、[模块互连](module-interconnect-5wire.md)、[接口几何/闭环核查](interconnect-audit-2026-09-21.md) |
| 5 | [BOM提示与缺料](bom-review-2026-09-21.md)、[外接电源选型](external-24v-supply.md) |
| 6 | [首板调试](bring-up-checklist.md)、[供电与制造验收](power-system-acceptance.md)、[固件要求](../firmware/module-power-firmware.md) |

## 当前结论

- 新增供电范围的181项引脚网络断言与参数检查通过，条件是合格24V单点输入和受控负载；不是全板、任意负载或实物稳定性保证。
- 四种板间连接组合36针的二维针序/方向核对通过，名义矩形可闭合；机械对插公差和数据断点尚待落实。
- 当前原理图快照角落152件、长边141件。角落导入重复ID问题已修复；不能把器件同步成功当作布线、DRC和制造通过。
- 建议采购24V/5A稳压适配器，5.5×2.1mm中心正极；120W是电源铭牌能力，整盘60W仍是待验收目标，每板约1W是设计预算。
- R26/R30的C165751在用户截图中缺货；尚未替换。最新角落13张BOM提示已逐项核对，保留全部位号，不因重复料号删件。

## 历史文档

`corner-usbc-pd-*`、`corner-pd-esp32-programming-*`、`unified-power-theory-review-*`、旧218器件`corner-pcb-layout-candidate-*`及`../firmware/corner-pd-firmware.md`不再指导当前采购、编程或制造。`unified-module-hotplug-power-*`中的两板支路设计仍有沿革价值，其PD入口段已失效。旧连接器研究、PCB接任记录保留原日期和证据，不改写成新验收。

本次为文档同步，未操作EDA、修改PCB/原理图、替换BOM、烧录或采集新的实物数据。
