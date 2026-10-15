# 格子线圈包围屏幕时的 RFID 故障调查

日期：2026-10-15（America/New_York）。本记录区分用户装配观察、真实串口结果、源码判断及尚待完成的诊断。不能用空处读卡成功代替屏幕装配后的验收。

## 最新装配结果：16块均可识别（用户确认）

2026-10-15，用户将线圈从与LCD同平面包围屏幕的位置，改为略高于屏幕，随后确认16块模块均能正常识别棋子。本次继续工作转为USB热点迁移，不再为解决旧装配问题改低保护阈值或重刷16板。

这是用户对新装配的实物确认；不等于服务器已经取得16块同时联网、配对、自动到达或整局验收。下文保留COM8单板在原同平面装配下的故障和背光对照，不能再将其Fault结果概括为当前16块的状态。COM8诊断板是否也已调整，需独立观察，不由16板反馈推定。最终壳体应保持用户验证过的线圈与屏幕相对位置，具体高度尚未量化。

## 用户观察与本轮目标（原同平面装配）

用户给16格接上24V后发现：线圈紧贴LCD外周、与屏幕处于同一平面时，各板显示 `RFID ERROR`。照片中一块板显示约4.93V、202mA。用户随后确认，同一线圈移到空处、线圈内部没有屏幕等物体时可以正常识别Tag。

这组对照优先指向装配环境造成的电磁影响，但没有测得天线电感、损耗、谐振点或RX波形，不能直接记为某一硬件故障已证实。用户提出软件判定过于保守的可能，并将一块独立测试板、屏幕、线圈接到电脑，保持线圈包围屏幕且屏幕上放有Tag，授权实际诊断和尝试读取。

## 设备与原始基线

- 串口COM8；实际设备报告完整MAC `68:ee:8f:54:11:a4`，模块 `tile-68ee8f5411a4`。COM29为玩家屏，本轮不操作。
- 现场原固件为 **V0.32**，LCD SPI 8MHz；不是16格批量部署的V0.34。历史应用SHA与设备记录见[设备台账](current-device-connections.md)及[双板ORDER记录](tile-order-development-progress-2026-09-09.md)，升级前仍需重新核验实际Flash。
- 用户已确认本轮独立测试板**仅USB供电，没有另接24V**；此前整盘24V供电的观察不能代替本次场下电源测量。
- 主任务于19:51:40～19:52:00 UTC进行20秒有界串口采集，显式每5秒发送STATUS，DTR/RTS预置false，结束已释放串口。
- 四次STATUS均 `state=4`（Fault）、count0；4.822～4.849V、121.8～145.3mA，历史峰值196.8mA；`TXDIS=1/OFF`。当前显示的电源值与关场状态不等于触发Fault那一瞬间的场下测量。
- boot保持 `0F73FC0D2F4AD0BA`；无有效ORDER上游；HTTP200、未分配格子。该窗口未捕获重启或有效UID。
- 服务器只读快照：room993580113、version14、配对cancelled、移动闸门inactive。调试不注入游戏动作、Tag绑定或分配。

本机原始证据目录：`C:/Users/kicof/AppData/Local/GridopolyRfidAudit/screen-coil-20261015-155140/`，包括 `baseline-com8.jsonl` 与 `state-before.json`。Flash备份和诊断产物保留本机，不入Git。

随后格子任务于约19:56:56～19:57:26 UTC采集原固件30秒 `RFID DEBUG ON` 窗口，设备确认ON和最终OFF，串口已释放。七次STATUS均Fault/count0，4.839～4.849V、124.0～135.0mA，boot未变。没有出现RFID-RAW或TAG-INV，说明未走到现有原始接收诊断的位置，不能据此区分数字链路、prepare或电源门控。证据为同目录 `debug30-com8.jsonl` 与逐块保留收发原始字节和生命周期的 `debug30-com8-raw.jsonl`。

## 判定边界

当前V0.34源码的 `RFID ERROR` 是聚合Fault：数字接口/配置回读、INA有效性、低压、总电流或电流增量、ANTFAIL、接收边沿溢出、场持续时间和关场回读均可能触发。仅无UID或解码质量不足，在其余检查正常时走NoTag/Unstable。

电源阈值为4.65V、350mA总电流、180mA场前后增量；照片和本轮STATUS数值没有直接越限，但不能代替场下采样。现有STATUS和RFID DEBUG未完整记录扫描入口/prepare提前退出原因，所以“当前已经确认ANTFAIL”或“只是软件误报”均没有足够证据。

HTRC110的ANTFAIL是芯片根据RX相对QGND的幅度生成的状态位，不是软件可随意设置的“识别强度阈值”。软件决定是否因该状态提前停止扫描。NXP要求在稳定场时判断；关场、调制和建立过程中该位可能置位或不确定。现有代码在关场前读取，没有发现确定的“先关场再判断无场故障”错误。约200µs显式等待之后还存在串行寄存器读事务，不能仅用该常量证明建立时间不足。

依据：[HTRC110数据手册](https://www.nxp.com/docs/en/data-sheet/037031.pdf) §9.3、§10.8、§12；[AN98080](https://www.nxp.com/docs/en/application-note/AN98080.pdf) §6.2.4、§7、§8.1、§11.2。附近金属损耗、失谐和检测电压有物理依据，但本次具体根因仍由现场数据判定。

## 本轮诊断原则与待完成项

先采集原固件诊断，再补充分阶段原因，保持正常扫描保护不变。若实际证据表明仅ANTFAIL阻止读取，可通过默认关闭、显式单次、有时间限制的只读试验检验是否仍能解码；保留数字链路、低压/过流、溢出、场时限和关场检查，试读UID不发布到服务器，不把旁路结果写成正式读卡成功。

## stage1候选与读取过程

分阶段诊断默认关闭，`RFID DEBUG ON`仅启用30秒，最多每秒记录一次扫描；记录缓冲到扫描关场路径返回后输出。未执行的检查标为-1，寄存器有效页由page_mask指示。没有改动原阈值或正常扫描门控，没有包含告警旁路。

unknown角色的`tile_esp32s3`构建通过（61.46秒），应用SHA256为`339de78d7de00090526654692d3a5268e36af873dd5fce9dc1fc9982af149224`。源清单digest为`43cf300f9a31f15b33b37d36058bedaf3b0b14f220410a23640d8442102b4a77`，本机证据为`stage1-candidate.json`、`stage1-sources.json`和`stage1-review.diff`。主任务已审查门控保持及缓冲记录。7项定向host测试通过，包含生产prepare函数在诊断开/关时的硬件调用一致性、配置/ANTFAIL/采样/脉宽/关场失败、原电源边界及诊断时限/回绕/容量。

备份过程保留失败证据：esptool5.3.1成功核对ROM完整MAC、16MB、BOOT已释放，读取前64KiB并确认分区匹配，但随后读取app0分区发生`Packet content transfer stopped`；该次未写Flash。4.11读取在父进程120秒截止时终止；其日志因Python缓冲停留在Connecting，但已生成前64KiB备份，不能据此断言无握手。其后5.3.1显式USB复位在约0.9秒内重新确认同MAC，并执行watchdog恢复。各次日志分别为`com8-backup.log`、`com8-backup-v411.log`、`com8-after-read-recovery.log/json`。后续改为显式USB握手和128KiB分块读取；分块脚本首次因既有文件而在开串口前拒绝覆盖，重试使用新文件名和无缓冲输出。不把恢复复位遗漏为“无扰动采集”。

分块读取到5,111,808／6,553,600字节时达到120秒总上限，已终止并释放；没有完整读完app0槽。离线解析ESP镜像后确认，实际应用只有2,705,600字节，完整包含在已保存的部分数据内，内置SHA校验通过；提取应用SHA256精确匹配历史V0.32的`176db3cc3ea78c920f235ebcffafeef423dbe07caf6db7bb89a9e4681e502760`。因此原应用和前64KiB/分区已有有效回滚备份，但不能记为完整Flash或完整app0槽备份。

## stage1实机结果

20:13:26.750～20:13:49.828 UTC完成四段写入、读回校验和watchdog重启，esptool5.3.1回报verified，同连接完整MAC不变；保留NVS及原布局。COM8现为V0.34基础的stage1诊断候选、unknown角色、LCD8MHz。首次启动采集脚本因本地脚本UTF-8路径误解码而在打开串口前失败，修正后再采集，没有为该脚本错误额外复位设备。

20:14:22.947～20:14:53.014 UTC的30秒采集取得25次完整trace，主任务独立读取JSONL复核：

- 25次入口digital=1、INA configured/valid=1、safe=1；入口关场基线为4.850～4.864V、119.5～133.8mA。
- 25次均在`prepare_config`：config=1、antenna=0，原始page0/1/2/3恒为`7B,70,30,30`。page2/3的0x10位确实置位，触发既有early ANTFAIL退出分支。
- 每次field_ms为5～6，关场回读off=1；没有进入prepare_settled或capture，未取得UID。尚未测得该装配下的稳定场内电压、电流。
- 窗口结束收到`diagnostics=OFF`和最终TXDIS=1/OFF、Fault/count0；boot为`4F3A8974B0CB67B7`，HTTP200、未分配格子。

证据：`com8-stage1-flash.log`、`stage1-debug30-com8.jsonl`及对应原始字节日志。已证实“程序因芯片ANTFAIL提前退出”，尚未证实“告警误报且可可靠识别”。下一步为单次本地只读试验：仅越过ANTFAIL，先检查稳定场内功耗，再尝试UID，所有其他保护保留。

审查还发现电源测量的时间边界：源码INA226配置为0x0527，16次平均，bus和shunt转换时间各1.1ms，完整平均周期名义35.2ms。因此在短场内调用samplePower，不能直接声称得到全程在场内的瞬时值；可能包含较早关场数据。stage2试验应在单独的有界稳定场窗口等待测量更新，正常扫描配置不在本次定位中自动改变。依据：[TI INA226数据手册](https://www.ti.com/lit/ds/symlink/ina226.pdf) §7.1.1表7-3～7-5。

## stage2单次试读与背光对照

单次试读由显式`RFID PROBE ONCE`触发，最多12次请求、6秒工作预算、60秒冷却，结束另有按最后场时计算的关场休息。只允许该路径忽略ANTFAIL，保留配置/采样/电源/溢出/350ms场时/关场检查；正常扫描仍Strict。关场基线等待80ms，prepare额外等待80ms，每次capture先保持原UID请求和24ms边沿捕获时序，再在无溢出时补足80ms开场后读INA。场间及结束后休息至少150ms或前一场时的4倍。UID仅供串口诊断，不发布到服务器或写入正常Tag缓存。

最终unknown构建通过，15项定向host测试通过（10项诊断/门控、5项实际probe编排）；初次fixture遗漏常量的构建失败已修正并保留日志。源码提交`d1915349`；75文件构建清单经主任务逐项核验无差异。sourceDigest为`accaf0be635ec11a75eb3d014a2141f141c16cfbc5f2ba624d23f6323fbed660`，stage2应用SHA256为`4fe2e35a2e314b90cf3b2187c3878de1d871abbf9cef5bdeae2940bc3faf8f11`。COM8已四段写验成功、watchdog重启并确认stage2；角色unknown、LCD8MHz，其余16板和玩家屏未更新。

### 原亮度150：忽略告警仍无UID

20:25:40.890～20:26:02.003UTC窗口，在用户原摆放及亮度150下执行唯一一次probe。三个采样值0x0D/0x15/0x05各试4次，12次均NoResponse/bits0、无有效UID。prepare稳定场为105ms、4.862V/136.5mA，入口118.3mA；capture各83ms、4.862～4.864V/136.0～136.8mA、113～145个边沿，无溢出，配置、采样、电源、关场检查全部通过，但page2始终0x30（ANTFAIL仍置位）。最终ATTEMPT_LIMIT、confirmed0、5651ms、off1、publishedNO/normalSTRICT。boot为`78981BC7006E9C6E`，6条STATUS仍Fault/count0，采集内未见reset/panic。证据为`stage2-result.json`及`stage2-probe-once-com8.jsonl/raw.jsonl`。

### 背光0：连续读到相同UID

保持固件和摆放不变，20:27:49.231～20:28:12.322UTC将背光临时设为0。一次probe连续3次解码Present、32bits、UID均为`7EF9FD7E`，采样0x0D；边沿199/183/199，prepare105ms、capture83ms，场下约4.879V、125.3～125.8mA，其他保护通过，ANTFAIL仍置位。最终UID_CONFIRMED、1882ms、off1、publishedNO/normalSTRICT。尾段收到恢复亮度150及DEBUG OFF确认，串口关闭。主任务独立读取日志复核三次UID及恢复确认。证据为`stage2-backlight0-result.json`及`stage2-probe-backlight0-com8.jsonl/raw.jsonl`。

该结果证实：当前装配在关闭背光且仅对本地试读越过ANTFAIL时，可以读取Tag；不能声称屏幕已断电、正常游戏已可读卡，或线圈电气状态合格。已部署框架`esp32-hal-ledc.c`的analogWrite默认1kHz/8位，150为PWM；255会转换为FULL ON。

### 全亮255与恢复150复现

20:29:24.503～20:29:47.663UTC，背光255下再次确认UID `7EF9FD7E`：5次请求中3次Present且32bits一致，另1次NoResponse、1次Ambiguous，2720ms完成。电压4.835～4.840V、电流154.5～157.8mA，仍在原电源门槛内；page2仍0x30，所有其他检查通过。首个采样13无响应后，在采样21完成确认。采集结束恢复150、DEBUG OFF并关闭串口；主任务独立核对日志。

20:30:42.012～20:31:05.113UTC恢复150的复现试验：12次请求再次无完整UID，其中11次NoResponse、1次Collision(bits3)，0次Present。该部分碰撞解码不能证明现场真的存在多Tag。最终ATTEMPT_LIMIT、5651ms、off1、publishedNO；其他门控仍通过。尾段再次收到亮度150及DEBUG OFF确认，串口关闭。

| 设置及顺序 | 完整UID结果 | ANTFAIL | 对照范围 |
| --- | --- | --- | --- |
| 原150，1kHz PWM | 0/12 | 持续置位 | 原摆放、原亮度 |
| 0，背光关闭 | 3/3，均7EF9FD7E | 持续置位 | 屏幕电子部分仍供电 |
| 255，全亮无亮度PWM | 3/5，均7EF9FD7E | 持续置位 | 屏幕亮着仍可读，电流高于150 |
| 恢复150，1kHz PWM | 0/12 | 持续置位 | 复现原不可读结果 |

四次probe间隔均超过60秒，固件与物理摆放未由工具改变。证据为`stage2-backlight255-result.json`、`stage2-backlight150-result.json`及各自`stage2-probe-backlight255/150-com8.jsonl/raw.jsonl`。汇总中的时间含明确-04:00偏移，本报告统一转换为UTC。

## 结论、当前设备与后续开发

**本次已实际读到Tag，且屏幕全亮时也能读取。** 因此不能认定这只线圈完全不支持围绕屏幕使用。实测支持两项需要分别处理的问题：

1. 当前1kHz背光PWM明显影响该装配下的可读性；关背光/全亮可读，恢复PWM后再次读不到。尚未用示波器确定耦合经电源、地线或电磁场的具体路径，不把本试验写成完整EMC结论。
2. ANTFAIL在能解码和不能解码时都置位；正常代码一律因此提前退出，过于笼统地阻止了本次可读场景。告警不能直接等同“绝对无法识别”，但也不能因此宣布天线匹配/峰值电流已合格。

当前COM8保留stage2应用`4fe2e35a…`、unknown/8MHz、亮度150、正常Strict门控；所有诊断均结束、串口已释放。**正常游戏扫描仍会Fault，这次是定位与有界试读完成，不是长期修复已完成。** 没有永久关掉告警、修改供电阈值或发布诊断UID。

主任务只读服务器前后状态仅`identity.serverEpochMs`变化，room993580113/version14、配对cancelled、绑定和其他业务字段保持；证据`state-before.json`、`state-after.json`、`state-diff.json`。

下一步由主会话与格子任务负责：选择并验证不干扰RFID的亮屏背光策略；将天线状态与有效UID判定分开，设计保留电源/配置/场时/关场检查的受限异常路径，验证连续识别、移走/重放、不同Tag和正常游戏上报，再决定发布到16板。全亮对照只有3/5次有效，不能直接宣称读距和稳定性已达标；最终线圈/屏幕/外壳仍需独立验收。

本记录不构成16板同时读卡、中心/边缘读距或最终外壳验收。
