# 2026-09-27 全项目文档更新记录

> 后续更新：本记录对应提交`463bf18`时的RC4状态。之后用户在PCB任务中翻转J3，现行候选为**RC5，J3.1=GND、J3.2=BAT_PACK**，已购电池为EEMB LP103454。下表RC4针序/商品图选项仅保留为当时核查历史，不能作为RC5接线依据；最新资料见[玩家底板说明](hardware/player-usbc-battery-base-2026-09-27.md)与[当前交接](CURRENT-DEVELOPMENT-HANDOFF.md)。

本次按用户要求整理当前设计、开发进度和过时数据。开始时HEAD为`ad77384`，工作区已有协议/规则文档、AP脚本和PCB工程等未提交变更；此次仅归档明确核对的文档，不把原有内容全部算作本轮新增开发。

## 主要纠正

| 旧说明 | 现行说明与证据 |
| --- | --- |
| V0.32、ORDER拒绝所有环 | V0.33角色profile；服务端支持32节点28长边/4角落，V1波形不变；其他环尺寸未实现。[契约](firmware/ring-onboarding-contract-2026-09-26.md) |
| 所有格子按注册顺序认领 | 仅旧无ORDER模块兼容自动认领；当前按物理邻接/手动锚点/32环，单板无锚可未分配并用于配对 |
| PD/NVM仍需开发 | 棋盘角落直接24V，PD取消；玩家底板独立1S电池/5V，不接24V。[硬件入口](hardware/README.md) |
| 最后COM7 b53e、Pi旧IP/hash | 更新为COM7 9c4e、COM6 V0.33、Pi 2bc084d7；COM8未升级；注明最后观测时间与非实时状态。[台账](firmware/current-device-connections.md) |
| 仅网页手工绑Tag/所有人先做头像 | 自动发现1～4屏并补足4席，逐人配对、绑定后身份并行，单reader/开链兼容；自动局禁止手动接口绕过 |
| 手写100ms处理间隔断笔/DELETE仅删字 | 真实接触边界、保留画布和识别代次、1200ms静默、DELETE优先清墨迹、DONE等待结果；手写确认与旋钮待验分开 |
| 2MiB/单头像预览 | 2304KiB/双220×300 RGB565预览；保留游戏期1MiB预算。[缓存源码](../Firmware/PlayerConsole/remote_tile_cache_policy.h) |
| 旧桌游规则当作现实现 | 对照Core记录实际先手、地图数值、轮转拍卖/最低加价10、有限库存、整组清算及破产；地主收租窗口/牌堆等保留历史。付款区分圆屏本地倒计时与Core权威状态 |
| PCB原理图/布局阶段混在一起 | 当前RC4、J2反转、J3 PH2.0和商品图黑红方向、标准型SMT、67SMT＋1插件；待制造与首板事项保留 |

## 已核对的直接来源

- `Server/RaspberryPi/src/TileOrderTopology.h`、`TileOrderProjection.h`、`TileDebugAssignments.cpp`：32环、四角间距、角色、旧模块自动认领。
- `Firmware/TileModule/platformio.ini`：long/corner/unknown构建边界；[格子部署记录](firmware/tile-production-progress-2026-09-25.md)区分候选与COM6实际写验。
- `Firmware/PlayerConsole/rotary_input_filter.h`、`ui_handwriting.*`、`app_state.cpp`、`remote_tile_cache_policy.h`：可信定位点、接触/静默规则、本地付款弹窗、双预览预算。
- `Firmware/libraries/GridopolyCore/src/gridopoly/core/GameModel.h`、`GameEngine.cpp`、`BoardCatalog.cpp`：实际枚举、转移、规则、现金及库存。文档更新不改变规则代码。
- [批量发布](firmware/batch32-game-release-2026-09-26.md)、[输入部署](firmware/player-input-handwriting-fix-2026-09-26.md)、[电池构建](firmware/player-battery-telemetry-progress-2026-09-27.md)：区分源码HEAD、v4包、现场固件和用户验收。
- [RC4](../PCB%20Files/PlayerConsolePower/Layout-2026-09-27-RC4/README.md)及其J3/制造/哈希记录；冻结文件原字节保留。采购方向补充在[现行硬件说明](hardware/player-usbc-battery-base-2026-09-27.md)，未改冻结快照。

## 历史与未完成事项

原混合日期交接、早期游戏规则和设计状态图分别存入Docs/archive，保留设计沿革；早期进度页增加当前入口，日期性观察不改写成新实测。

本轮没有重新跑固件/服务器功能测试、连接串口、访问现场Pi、改变房间、修改PCB或新建自动化。文档中的测试成绩引用已有证据；最新旋钮、原游戏显示、32实体/4屏/36无线、整盘电气及RC4首板均仍待验。[当前责任清单](CURRENT-DEVELOPMENT-HANDOFF.md)

## 文档核查

提交前检查本轮49份文档、420个本地链接（无缺失）、源码字段/常量、残留过时当前态、Git空白与明确路径范围。对274个冻结硬件及既有非文档路径做哈希/存在状态对比：RC4等冻结资料保持不变；273项无变化，原本已修改的活动工程`PCB Files/Gridopoly.eprj2`在期间发生哈希变化，本任务未写入该文件，也不纳入提交或覆盖现状。详细工作区前后快照与检查结果保存在本机`C:/Users/kicof/AppData/Local/Temp/gridopoly-doc-refresh-20260927/`，不提交其中私有配置或备份。
