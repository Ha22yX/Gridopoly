# 16格试玩：格子端兼容、候选与首批身份记录

> 晚间状态导航：16板normal V0.34已同时在线，线圈抬高后用户确认读Tag、服务器16 reader stable，真实唯一ORDER环及四角0/4/8/12已验；USB20台与软件重启恢复通过。下文保留各阶段当时的部署/失败/未验收事实，当前版本与待办见[设备台账](current-device-connections.md)及[路线图](development-roadmap-2026-09-21.md)。

本轮范围为4角落＋12长边的格子固件。主任务统筹服务器/玩家屏/批量包与实物窗口并统一Git索引；本任务只修改TileModuleTests及本报告。已读AGENTS、开发协调记录和当前交接。未修改生产固件、EDA、共享协议或Git索引，未创建后台/定时任务。

## 软件结论

现有V0.33 long/corner普通候选可继续使用，物理ORDER保持V1。帧仅含自身boot64、seq16和CRC16，每节点独立输出，输入只接收直接上游，不携带总节点数或地图格号。16格四角0/4/8/12、两角间3个long及默认START/手动旋转属于服务器拓扑投影。服务器任务已确认本轮扩展16/32不改heartbeat响应契约，也不增加单板/开放链配对门槛。

格子由服务端分配响应替换本地tile，不自行推算16/32位置；撤销分配时清空旧tile和响应中的旧cue。配对在未分配返回前解析，普通页面和LED均优先显示有效配对，周期连接动画不会盖掉配对。诊断页面是显式调试例外。Tag在扫描开始锁定room/session，完整扫描推进scanSeq，相同内容保持tagRevision；重试/旧库存不刷新扫描年龄，缺失消抖或不完整库存不成为新鲜绑定证据。绑定和到达事务幂等由服务端负责，模块不自行绑定或写Tag。

默认LCD仍为8MHz、240×320、rotation0，10颗LED；仅历史验证过的COM6身份`28:84:85:ba:9f:e8`可使用本地40MHz白名单。新16块不能套用旧COM6显示或双板ORDER实测结论。RS485保持未启用。

## 本轮回归

修改HostRegression的ORDER用例并新增network_contract.cpp、对应fixture提取和CMake配置。9套66项全部通过，无跳过：model12、decoder11、RFID protocol5、presence4、firmware boundaries11、display8、ORDER10、pairing JSON2、network contract3。

- 16及32个实际ORDER状态机同时运行，验证每点只接收直邻；断开一条输入仅该边过期，其他发送继续；重接后新完整序号恢复；单节点重启只改变其直接后继的上游身份。保留CRC、重复/过期帧、序号和时间回绕、stuck-low、迟调度释放测试。
- 使用实际BoardCatalog/视觉目录、服务端投影和HTTP序列化、固件parsePayload，验证16格全图编号/类别/图片/价格与四角。重锚点响应替换C2→A2，撤销后清旧格号/旧cue，恢复后接受START。该用例验证客户端消费，不代替服务器求解正确性的测试。
- 服务端10种active配对状态逐一通过实际序列化→解析，未分配也保留room/session/玩家名、抑制movement，并按lease过期；disabled清除。既有生产函数回归同时覆盖十灯双闪/呼吸、动画不随重复心跳重启、新鲜度、锁失败重试及旧scan不可覆盖成新session。

构建输出在本机`C:/Users/kicof/AppData/Local/GridopolyTileTools/trial-16-20261015/`。首次新增测试链接缺视觉目录实现，已补入实际grid_city_visual_catalog.cpp后成功；build.log保留首次失败，build-final.log和tests.log为最终成功。MSVC构建保留ArduinoJson std::ostream模板的C4180警告，不影响测试通过。没有因此修改设备固件或第三方库。

本轮未重编ESP32应用：所有生产输入与已构建候选一致，变更只在主机测试。主机PASS不证明实物接线、屏幕/灯效、RFID多板干扰、功耗、整盘游戏或16块同时稳定运行。

## 普通候选交付

两份candidate.json及candidate-sources.json保留9月26日冻结文件，不改历史gitRevision或验证说明。重新读取每份70个源文件和4个烧录段，全部大小/SHA匹配；sourceDigest均为`c064153da8105a4c04d0b93b7128bf89a987739ad9e4b195999d7c00ed150ba5`。本轮结果为trial目录中的candidate-validation.json。

| Profile | candidate.json | 应用SHA256 |
| --- | --- | --- |
| tile-long | `C:/Users/kicof/AppData/Local/GridopolyTileTools/build-v033/tile_esp32s3_long/candidate.json` | `19edea4ba388588f1ba6548e85043ae91bea70e0527c708d3c9c758212d4ebe4` |
| tile-corner | `C:/Users/kicof/AppData/Local/GridopolyTileTools/build-v033/tile_esp32s3_corner/candidate.json` | `761bd36ffb4b918d06ebf386a747623fb516cbb8ce68ee2a2d1f46b8e20023ff` |

均V0.33/normalFirmware=true、ESP32-S3、16MB、DIO/80m；LCD SPI与Flash时钟是不同配置。段地址0x0000 bootloader(15104)、0x8000 partitions(3072)、0xE000 boot_app0(8192)、0x10000 app(2710224字节)。源清单中本地凭据仅记录哈希，不复制内容。主任务负责将已核对候选纳入16格批量发布包和身份清单。

## 硬件资料与版本边界

9月21日DesignAudit中的long/corner U3均为ESP32-S3-WROOM-1-N16R8；逐一比较board_config.h的15个LCD/RFID/ORDER/INA226/LED/RS485_DIR引脚，两板均15/15相符，GPIO13均NC。原始比对保存在pinmap-review.json。这是历史设计资料，不能证明实际订单与该导出相同；制造revision仍unknown。已向PCB任务请求本批GPIO、屏幕、RFID和制造文件对应，不自行改EDA。

## 本轮设备识别

初次只读PnP未见ESP，只有蓝牙COM3/4与非ESP的COM5。用户随后用4根数据线连接四个相邻格子并主动进入下载模式，主任务将COM12–COM15交本任务独占并授权有界ROM身份/Flash读取。完整PnP随后显示4个VID303A/PID1001及串口，均无设备管理器错误；没有把原先的缺席当作驱动故障。

2026-10-15 04:07:35–04:07:37 UTC，固定esptool5.3.1逐个执行flash-id，参数no-reset/no-reset/no-stub/connect-attempts1，每进程50秒上限；四次均正常退出、串口全部释放。没有主动复位、载入stub、写入Flash或eFuse，仍留在用户选择的下载模式。工具识别为USB-Serial/JTAG，不把用户口述DFU当作另一种USB协议的实测。

| 串口 | ROM完整MAC（与PnP父ID相符） | 芯片/Flash/PSRAM | 实物角色 |
| --- | --- | --- | --- |
| COM12 | `a4:cb:8f:c2:98:e4` | ESP32-S3 v0.2 / 16MB / 8MB | 待用户对应 |
| COM13 | `a4:cb:8f:c2:98:f8` | ESP32-S3 v0.2 / 16MB / 8MB | 待用户对应 |
| COM14 | `a4:cb:8f:c2:98:ac` | ESP32-S3 v0.2 / 16MB / 8MB | 待用户对应 |
| COM15 | `a4:cb:8f:c1:e1:88` | ESP32-S3 v0.2 / 16MB / 8MB | 待用户对应 |

Flash manufacturer46/device4018、eFuse quad/3.3V；PSRAM为芯片识别的封装8MB AP_3v3，未运行内存压力测试。未读取四板现有应用版本，因此当前不能写成已部署V0.33。COM5及玩家屏未打开。原始pnp.json、esp-port-parents.json、rom-identification.json、COM12至COM15-rom-identify.log均在trial目录。

## 未完成与交接

软件和两份普通候选已交主任务；服务器16格环规则及隔离测试由服务器任务继续。主任务确认四块实物角/长边与端口对应后，建立完整MAC清单并协调首批普通固件写验/观察窗口。后续逐批覆盖12long＋4corner；启动日志、实际HTTP角色/版本、ORDER直邻与整个16环、重新锚定/断线重接、配对拿放、移动目标LED/自动到达，以及显示/RFID/电气验收逐项留证。

上述为初次只读识别阶段；随后获授权的首批写验和启动恢复见下。剩余整盘操作及发布责任由主任务持有，不把本阶段交付标作整个试玩目标完成。

## 首批4板实际写验与启动恢复

用户确认1角落＋3长边，并仅拔下角落；主任务PnP确认只有COM15消失，确定COM15/`a4:cb:8f:c1:e1:88`为corner，COM12/e4、COM13/f8、COM14/ac为long。用户接回角落进入下载模式后，主任务明确授权本批正式写验。烧录前再次核对四口PnP父身份和普通候选哈希，四行清单不含玩家。

采用当时最新Tools/release生成本机`trial-16-20261015/batch01-release/`，boardSize16/playerCount4。包内player为已冻结旧普通9c4e，仅满足三profile包结构，**没有烧录玩家屏**；本批清单是`batch01-inventory.csv`。release.json SHA256为`4449109ebff976704e7d86ad3b93e5f160ce3198d5a460357c400f07cbebb6a4`，dry-run成功。

固定Python312＋esptool5.3.1：全部四板先通过ROM MAC/16MB/分区预检，再逐板同连接写四段、Flash校验、MAC复核及hard-reset。命令正常exit0，未erase-all、未写NVS、未强行迁移布局。VERIFIED时间（America/New_York，日志保留时区偏移）：

| 串口/身份末尾 | 角色与应用 | VERIFIED时间 |
| --- | --- | --- |
| COM12 / c2:98:e4 | long / 19edea4b… | 2026-10-15 00:12:23.085 −04:00 |
| COM13 / c2:98:f8 | long / 19edea4b… | 2026-10-15 00:12:43.697 −04:00 |
| COM14 / c2:98:ac | long / 19edea4b… | 2026-10-15 00:13:04.324 −04:00 |
| COM15 / c1:e1:88 | corner / 761bd36f… | 2026-10-15 00:13:25.019 −04:00 |

首次四口并行45秒STATUS观察全部只有采集器OPEN/CLOSED，服务器modules仍为空，不能把工具复位返回等同App启动。之后严格使用no-reset/no-stub连接，四板均直接响应ROM，MAC相符，forceDownload=false；锁存strap的GPIO0位均低。按已冻结S3 SDK读取GPIO_IN（STRAP地址＋4）各三次，GPIO0当前均高。锁存低不等于当前按键仍按住；用户随后也明确BOOT已松开且无跳线。

本机5.3.1的ESP32S3ROM.hard_reset在uses_usb_otg分支才对此锁存条件调用watchdog；本批为USB-Serial/JTAG，原流程走RTS。没有因此修改生产固件或重刷。先对COM12在no-reset连接、MAC和live GPIO0核对后执行一次watchdog_reset，20秒观察证实App启动、V0.33/long/8MHz、GOT_IP和HTTP200。按主任务明确授权，随后COM13/14/15各同样执行一次watchdog_reset（2026-10-15 04:17:10–04:17:12 UTC），均成功。每板仅一次该恢复复位；它是实际扰动，不能省略为首次hard-reset就启动成功。

## 四板运行结果与RFID未通过项

第二个四口并行窗口为2026-10-15 04:17:23–04:18:08 UTC，各45秒、STATUS每5秒，四个采集进程exit0且均有MONITOR CLOSED，串口全部释放。全部HTTP200、实际LCD8MHz，HTTP独立确认V0.33/3long＋1corner/online；没有分配格号、设置锚点或操作房间。观察内boot不变、ORDER timerOK、rejected0，timing_drops均保持启动时1。

真实ORDER链为COM12/e4 → COM13/f8 → COM14/ac → COM15/88；每条串口upstream boot严格等于前一板的boot，HTTP的orderIndex为0/1/2/3，非注册先后排序。COM12无上游，后继接受计数分别推进至8/7/7，全部仍unanchored/assigned=false/closedRing=false。这只证明本次四节点开链邻接，不是16闭环或地图投影实测。

RFID仍未验收。第二窗口COM12与COM13的STATUS均为Fault/freshNO（各11次）；COM14为10次Fault及1次NoTag/freshYES；角落为2次Fault及8次NoTag/freshYES，并有状态转换日志。所有状态记录均count0、TXDIS=1/OFF，没有执行实体Tag拿放。串口没有panic、json_error、heartbeat_failed或TXDIS失败，不代表没有RFID故障。

| 模块 | STATUS观测5V轨范围 | 解释边界 |
| --- | --- | --- |
| COM12 | 4.624–4.642V | 观测值均低于4.65V扫描门槛 |
| COM13 | 4.611–4.660V | 跨越门槛，Fault期间无新鲜扫描 |
| COM14 | 4.636–4.677V | 跨越门槛，偶有有效空扫描 |
| COM15 | 4.644–4.689V | 跨越门槛，多次空扫描但仍有Fault |

V0.33在扫描前和开启场期间均检查供电，低于4.65V会拒绝扫描并报Fault。当前低压是已有证据的保护条件；STATUS采样与每次scan并非同步，不能声称每个Fault已定位到同一分支，也不能把INA读值当作校准示波器测量。已交主任务确认单点24V与USB/hub供电、同步PCB任务，未降低门槛或用固件绕过保护。首板显示目视、多板读Tag/干扰及电气稳定性仍待验。

烧录前后/api/state独立diff仅identity.serverEpochMs变化，room993580110/version10及其余业务一致。最终4模块注册不改变该事实；没有伪造心跳、Tag、拓扑或对局动作。

证据均在trial目录：batch01-release、batch01-inventory.csv、batch01-before-pnp.json、batch01-dry-run.log、batch01-flash.log、batch01-flash-events.jsonl；首个无输出观察和各板post-flash-rom/gpio-input/watchdog-reset日志全部保留；正常窗口为batch01-COMxx-started-serial.jsonl、batch01-started-observation-results.json，最终HTTP和分析为batch01-after-state.json、batch01-after-assignments.json、batch01-result.json。原先ROM识别与候选验证文件不覆盖。

本批状态：四板已部署普通V0.33并联网、角色/四节点ORDER通过本窗口核对；RFID、视觉、16环及完整对局未通过。主任务负责供电确认、剩余12板身份/分批写验和整盘联调；格子任务继续按具体现场问题定位。后续批量工具的显式watchdog复位选项由主任务实现，本批没有回写或重跑新版工具制造历史证据。

用户随后明确本窗口没有接24V总线，仅这四块通过电脑USB供电。该现场条件与低压记录一并保留；主任务协调接入已选24V电源或先继续其他批次，不在未重新测量时宣布RFID恢复。本任务没有新增采集或降低阈值。

已对主任务新增Tools/release的`--after`路径完成只读评审：默认hard-reset不变；显式watchdog限定非secure S3 USB-Serial/JTAG，同连接开始和最终复位前检查live GPIO0 HIGH，清RTC_FORCE_DOWNLOAD后读回确认，再调用固定5.3.1公共reset_chip。mode从CLI到worker/result/日志闭合，失败不报VERIFIED。本机S3 SDK确认GPIO_STRAP为0x60004038、GPIO_IN为0x6000403C；固定esptool的API和目标类属性与实现吻合，未发现阻塞问题。主任务报告41项工具回归全过；本任务未重复执行其测试、未改工具或重刷四板。本批独立WDT恢复时forceDownload已为false，不能充当新版清位分支的实机验收。

## 第二批4板写验及实际启动（累计8/16）

用户更换3块新长边后，首次COM16只读连接在ROM同步期间遇到Windows USB I/O错误31，尚未取得MAC，也未打开COM17/18；随后日志输出遇到GBK编码错误。原始失败保留于`batch02-COM16-rom-identify.log`及`batch02-first-identity-attempt.json`，未复位、加载stub或写Flash。主任务重新确认三口齐全后，授权两次稳定PnP取样和各一次有界重试；明确设置UTF-8输出，三口no-reset/no-stub识别均成功。失败不能省略为首次即成功。

用户再拔下首批角落COM15、换入新角落并进入下载模式，主任务据实物操作确定COM19为corner。COM19有界no-reset/no-stub识别通过。四板完整MAC均与PnP父ID相符，彼此唯一且不重复首批；均为ESP32-S3 rev0.2、16MB Flash、封装8MB PSRAM、非secure USB-Serial/JTAG，实时GPIO0为HIGH。

本批仅使用最终冻结`C:/Users/kicof/GridopolyReleases/trial16-2026-10-15-v1/`，release.json SHA256为`96be3b531563cee87c2ccd53ed8c41d064d1c822d2a128e1a3850348be7b890c`。实际重新核对全部profile源码清单/烧录段哈希及分区布局，四行`batch02-inventory.csv`的dry-run通过；没有使用旧staging，也没有烧录player。

固定Python312/esptool5.3.1执行`--partial --execute --after watchdog-reset`。四板全部先通过硬件身份和已有分区预检，再逐板同连接完成写入、Flash校验、MAC复核和watchdog复位。最终工具已明确`--after`只影响写验后复位：本批4条preflight实际为hard-reset，4条verified实际为watchdog-reset，日志分别记录requestedAfter/resetMode。主进程exit0，无写入重试、erase-all、NVS写入或分区迁移，也没有追加独立恢复复位。

| 标签 / 串口 | 完整MAC | 普通固件角色 / 应用SHA前缀 | VERIFIED时间（EDT） |
| --- | --- | --- | --- |
| tile-long-04 / COM16 | `a4:cb:8f:c2:99:08` | long / 19edea4b… | 2026-10-15 00:44:02.129 −04:00 |
| tile-long-05 / COM17 | `a4:cb:8f:c2:98:b8` | long / 19edea4b… | 2026-10-15 00:44:23.402 −04:00 |
| tile-long-06 / COM18 | `a4:cb:8f:c2:98:fc` | long / 19edea4b… | 2026-10-15 00:44:44.399 −04:00 |
| tile-corner-02 / COM19 | `a4:cb:8f:c6:26:24` | corner / 761bd36f… | 2026-10-15 00:45:05.568 −04:00 |

四口同时进行一次45秒观察，STATUS每5秒、DTR/RTS均false，主机记录窗口2026-10-15 04:45:24.632–04:46:10.199 UTC。全部exit0、MONITOR CLOSED，随后已向主任务报告串口释放和可换下一批；没有再打开这四口。各口92条记录，9次HTTP200，固件V0.33、实际LCD8MHz、boot不变。观察内无panic/reset/json_error/heartbeat_failed，ORDER rejected均0、timing_drops均固定1。

实际ORDER开链为COM16/9908 → COM17/98b8 → COM18/98fc → COM19/2624，最终accepted分别0/16/12/9；各后继upstream boot与前一板boot相同。HTTP确认3long＋1corner online、orderIndex0/1/2/3、无conflict，全部unanchored/assigned=false/closedRing=false。主任务另存`batch02-root-http.json`独立核对。旧COM6也在线，但不计入本批，且本任务没有操作它。

本批RFID仍未验收：每板9次Fault/freshNO，均count0、TXDIS=1/OFF，无实体Tag拿放。USB供电下的INA遥测如下；不是校准电气测量，也不能据此把所有Fault归因于同一分支。

| 模块 | 电压范围 | 电流范围 |
| --- | --- | --- |
| COM16 / long | 4.275–4.294V | 123.8–140.8mA |
| COM17 / long | 4.240–4.261V | 120.8–134.5mA |
| COM18 / long | 4.235–4.255V | 110.8–126.0mA |
| COM19 / corner | 4.105–4.131V | 591.3–609.0mA |

全部观测电压低于4.65V扫描门槛，角落遥测电流也高于350mA保护门槛。角落与长边的电流差异已同时交主任务及PCB任务，原因与量程/外围实物仍待核查；不据此宣布角落损坏或仅缺24V。遵守用户无24V仍继续刷写的最新安排，没有降低保护阈值或要求重复供电确认。

本批before/after `/api/state`仅`identity.serverEpochMs`变化，room993580110/version11与其余业务完全一致；当前32格房间保留，未创建16格新局、设置锚点或修改Tag/分配。四节点开链验证不等于16闭环或完整试玩通过。

新增证据均在`C:/Users/kicof/AppData/Local/GridopolyTileTools/trial-16-20261015/`：`batch02-long-pnp-retry1/2.json`、各long重试ROM日志和`batch02-long-identities-retry.json`、`batch02-corner-identity.json`及COM19日志、`batch02-before-flash-pnp.json`、inventory/dry-run/flash/events、四份`batch02-COMxx-serial.jsonl`、observation-results、before/after HTTP和`batch02-result.json`。本批未改生产固件、发布工具或Git索引。

累计唯一新板已完成6long＋2corner的普通V0.33写验和实际启动，共8/16。余下6long＋2corner由主任务协调用户换线并给出下一批身份；格子任务在明确串口窗口继续。RFID、显示目视、完整16环、供电及实体对局仍分别保留待验，角落电流核查归PCB任务。

### 第二块角落纯白高亮反馈，第三批暂停

随后用户明确报告当前角落整圈LED特别亮且纯白。主任务优先排查COM19/`a4:cb:8f:c6:26:24`并暂停第三批，未假设设备发热或损坏。PnP仍确认相同身份/OK。五份涉及灯光的当前源码哈希与冻结corner源码清单一致，首批与本批角落应用SHA同为761bd36f…；没有不同版本角落固件的证据。

V0.33的long/corner仅角色宏不同，均GPIO21、10颗、GRB24bit。当前未分配且不在配对流程，`renderLedScene`应按RGB(82,220,183)、level12..36输出青绿低亮呼吸，即通道R3..11/G10..31/B8..25，每50ms更新。初始化先送全黑；RFID Fault只影响LCD错误显示，不映射灯环纯白。未发现当前状态的正常全白高亮路径。`BRIGHTNESS`仅控制GPIO16的LCD背光，没有灯环OFF/低亮命令，因此未用该命令代替灯环诊断。

主任务授权COM19一次不超过30秒的只读窗口；实际只采25秒、每5秒STATUS，无复位/写入/灯控，2026-10-15 04:49:46.282–04:50:11.702 UTC，exit0且MONITOR CLOSED，已释放端口。6条状态均HTTP200、assignedNO、pairing inactive、cue none，仍Fault/TXDIS1/OFF；INA电压4.054–4.080V，电流596.8–612.3mA。HTTP仍确认online/corner/V0.33、unanchored/index3。原始证据为`batch02-COM19-white-led-status.jsonl`、`batch02-COM19-white-led-capture.json`及`batch02-white-led-assignments.json`。

现有STATUS不报告RMT初始化结果、像素或数据线电平；`showPixels`未记录RMT发送返回值，框架封装也没有透传底层`rmt_write_items`错误。源码期望tick125ns、0高375ns/低875ns、1高750ns/低500ns、空闲低、帧后80us；这些不是实测波形。只能确认实物显示与软件预期不符，不能仅凭USB低压、电流或源码判定具体故障层。主任务继续协调现场操作，PCB任务核查实际灯型号/电平/首灯DIN及串接；未经新授权不重刷或进行高亮测试。此反馈重新打开该角落灯光验收，不撤销已完成的身份/Flash/启动证据，也不把8板启动等同8板完整功能通过。

## V0.34精确V6时序修正与普通候选

用户按主任务安排断开COM19的USB及两侧连接10秒，再单独USB直连观察15秒，报告仍持续纯白，随后已拔USB；故障角落保持隔离。PCB设计归档LED为WS2812B-B-V6/C52941391，归档电路的数据经5V供电74HCT125D通道3及330Ω送首灯，并非GPIO3.3V直接驱动。设计型号与电路不等于已核实COM19实装BOM；实装器件、电平与异常电流仍归PCB实物核查。

已下载并目视核对[Worldsemi原始型号数据表第4页](https://atta.szlcsc.com/upload/public/pdf/source/20251224/2F6ED8FFFF9D3044AED3CCAA3E4BF0D9.pdf)，本机`WS2812B-B-V6-lcsc-20251224.pdf` SHA256为`a3fd8855415d1f94e099c18d8b781ead6c1e3bf5a3b146895f91a77eb63ec14e`，原页渲染另存`WS2812B-B-V6-page4.png`。T0H为220–380ns，T1H/T0L/T1L为580–1000ns，复位低电平大于280µs，周期至少1250ns。V0.33的T1L=500ns确定不符合要求，T0H=375ns仅余5ns上限余量；这是已确认的软件问题，尚不是本次全白实物根因的直接测量证据。常规50ms idleLOW本已足够复位，不声称每帧都因原显式80µs而失败。

经主任务审核与授权，修正为V0.34正常固件：62.5ns计数tick，0码5H/15L=312.5/937.5ns，1码10H/10L=625/625ns，周期1250ns，最小脉宽余量45ns；初始化和发送完成后显式保持低电平350µs。`ws2812_led.h`隔离实际编码、维护状态及发送控制，`ws2812_esp32.cpp`使用SDK原生RMT接口和相同4块内存容量，核对实际16MHz计数时钟，保留初始化/写入/20ms完成等待/停止的返回错误，任何发送失败后停止自动续发。没有修改SDK依赖或游戏/ORDER/显示协议。

`STATUS`新增WS2812-STATUS：意图RGB、维护模式/剩余时间、实际计数时钟、attempted/completed/failed及错误阶段；`optical_verified=NO`明确软件完成不证明实物颜色。新增显式`LED STATUS`、`LED AUTO`、`LED OFF|RED|GREEN|BLUE seconds`（1–30秒）；仅0或固定单通道8/255，无白色/高亮模式，不持久化、不改业务。到期永久清除维护窗口，时钟再回绕也不复活；AUTO立即恢复当前游戏灯效。超长串口命令整行丢弃至CR/LF，未知/溢出/尾随垃圾参数不操作。LCD BRIGHTNESS语义不变。

HostRegression 10套78例全部通过：原有66例保留，新增11例覆盖全部字节V6时序、10像素GRB/MSB、容量边界、单色限制、非法输入、到期及回绕、超长行、实际计数时钟、驱动写入/完成/停止错误及不自动续发；另1例使用真实render例程验证维护到期恢复当前Destination且不改网络快照。MSVC仍有既有ArduinoJson C4180警告，无测试失败。原生ESP32首次构建发现SDK配置宏int到gpio_num_t转换失败，改为结构零初始化及显式枚举转换后long/corner均构建成功；首次失败日志保留。

候选位于`C:/Users/kicof/AppData/Local/GridopolyTileTools/build-v034/`，两profile均normalFirmware/V0.34，RAM70244、Flash2713809、app bin2714176字节。共同sourceDigest为`68309d74d19bba3660d0ede90f1a082146b9008d8b3febd082d1111bf8c3abf1`，74源逐一复算无漂移，全部4个烧录段尺寸/SHA及分区布局通过；构建时Git HEAD为a0435d1，修改由主任务统一审查提交，实际候选来源以源码清单为准。

| Profile | candidate.json | 应用SHA256 |
| --- | --- | --- |
| long | `build-v034/tile_esp32s3_long/candidate.json` | `c08a4508086e8a574a5de066f46abdfbc6a60380de8d5775ed23f1a176584d8b` |
| corner | `build-v034/tile_esp32s3_corner/candidate.json` | `7153f74ff89f9be9207b9a6896e8ce0c6fcf0ecc9efe49dde0253598a0eeabd5` |

构建/测试/验证证据在trial目录：`v034-host-build.log`、`v034-host-tests.log`、`v034-build.log`（首次失败）、`v034-build-final.log`及`v034-candidate-validation.json`。没有覆盖冻结trial16-v1，没有修改Git索引。此时仅源码、Host回归和普通候选完成，尚未部署V0.34或验证实物灯色。

用户另换一块备用角落并授权重写，主任务明确它不属于原计划余下两块。COM10完整MAC为`a4:cb:8f:c1:e1:34`；一次no-reset/no-stub ROM识别确认S3 rev0.2、16MB/8MB、GPIO0 HIGH、非secure USB-JTAG，2026-10-15 04:55:06.230–04:55:06.760 UTC结束并释放，未复位/写入。证据为`replacement-COM10-rom-identify.log`和`replacement-COM10-identity.json`。COM19故障板不计可用；只有COM10后续受审V0.34写验和实际运行成功后，目标盘进度才恢复6long＋2corner=8/16，历史唯一烧录板数届时为9。主任务负责v2组合、候选哈希复核及COM10单板窗口；其余串口不动。

### COM10备用角落已部署V0.34，随后获目视确认

主任务独立复核候选/74源无漂移/78例日志，源码提交59efe7a，保留候选a0435d1构建基线及实际源码清单。新冻结包`C:/Users/kicof/GridopolyReleases/trial16-2026-10-15-v2/`的release.json SHA为`81a1708b21df573143a93676ca8dbfd86a3df67b3dc821ecb0fca816101fab06`。在用户重写授权和主任务单板窗口下，仅再次核对COM10 PnP/ROM完整MAC、16MB、liveGPIO0 HIGH，验证新包及dry-run后执行固定5.3.1写验；四段/MAC/分区通过，实际watchdog-reset后VERIFIED于2026-10-15 01:06:49.300 −04:00，进程exit0。没有erase-all、NVS/游戏修改，未操作其他端口或隔离COM19。

一次45秒STATUS窗口为2026-10-15 05:07:11.131–05:07:56.469 UTC，101条记录，exit0/MONITOR CLOSED并已释放。9条LED状态全部readyYES/scene、clock_hz16000000、reset_us350，attempted与completed同步从423增加到1243，failed/driver_error/stop_error均0；requested_rgb随呼吸变化，例如4,12,10至11,31,25。9次HTTP200，LCD实际8MHz、V0.34、boot保持D5E3A2BE3F247B35；ORDER timerOK、accepted3→9、rejected0、timing_drops固定1。HTTP确认corner online，前驱为COM18/98fc、index3，无conflict，仍未分配且非闭环。

INA遥测4.616–4.639V、180.5–193.0mA（峰值224.0mA），9次RFID Fault/freshNO、count0、TXDIS1/OFF；当前仍USB供电，读卡未验收。无panic/reset/json_error/heartbeat_failed或WS2812 ERROR。before/after游戏状态仅serverEpochMs变化。此次只发STATUS，没有LED OFF或色彩命令。采集开始后已提前通知主任务实际驱动及联网正常，由其向用户询问灯色；日志不能替代目视结果。

证据使用`replacement-v034-`前缀：pnp、COM10 ROM、inventory、dry-run、flash/events、COM10-serial、observation、before/after HTTP及`replacement-v034-result.json`。v1及其历史日志保留。当前计划盘8/16已写验并实际启动（6long＋2corner），仅备用COM10为V0.34，另外7块仍为V0.33；余下6long＋2corner尚未写入。历史唯一烧录新板为9，包含已隔离故障COM19。替换板和固件同时变化，不能把新板电流降低或驱动成功视为原板故障已关闭。

用户随后明确目视确认COM10为低亮青绿色呼吸，没有持续纯白；此项替换板灯效验收通过。原始result JSON保留采集结束时pending状态，目视证据来源为后续用户回复。原COM19仍隔离、根因未闭合；此次目视不替代RFID、供电、16环或整盘游戏验收。主任务接续授权仅当前COM16/17/18三块长边升级同一冻结v2的V0.34；COM10不再操作。


### 第二批三块长边升级V0.34（计划盘8/16，当前4块V0.34）

主任务在COM10灯色获用户确认后，授权仅COM16/17/18三块已部署长边升级。重新核对当前PnP完整父身份、冻结v2 release.json及各profile段哈希/布局，三行inventory的dry-run通过。沿用上述v2普通long候选，应用SHA256为`c08a4508086e8a574a5de066f46abdfbc6a60380de8d5775ed23f1a176584d8b`，没有重编或重打包。

固定esptool5.3.1通过正常USB自动下载路径完成全部三板预检后，再逐板同连接写验；没有要求用户手动进入下载模式。各板完整MAC、16MB Flash、既有分区和liveGPIO0 HIGH均通过；preflight实际hard-reset，写验后实际watchdog-reset，主进程exit0。未erase-all、未写NVS或迁移布局，未追加恢复复位。本轮没有打开COM10、旧COM6或隔离COM19。

| 串口 | 完整MAC | VERIFIED时间（EDT） | LED completed首末值 | ORDER accepted首末值 |
| --- | --- | --- | --- | --- |
| COM16 | `a4:cb:8f:c2:99:08` | 2026-10-15 01:12:24.685 −04:00 | 1556→2377 | 0→0（链头） |
| COM17 | `a4:cb:8f:c2:98:b8` | 2026-10-15 01:12:48.492 −04:00 | 1086→1906 | 8→15 |
| COM18 | `a4:cb:8f:c2:98:fc` | 2026-10-15 01:13:11.696 −04:00 | 620→1440 | 4→11 |

三口随后各一次45秒并行STATUS观察，DTR/RTS均false、STATUS每5秒，2026-10-15 05:13:43.150–05:14:28.612 UTC。各101条记录、9次HTTP200、9条LED状态，全部V0.34、LCD实际8MHz；采集进程均exit0/MONITOR CLOSED，已释放串口并通知主任务可换线。LED全部readyYES/scene、16MHz、reset_us350，attempted与completed相等且持续推进，failed/driver_error/stop_error均0，意图RGB随低亮青绿色呼吸变化。三块长边的实际灯色尚未得到用户目视确认，日志中的optical_verified仍为NO；仅COM10的目视确认已通过。本窗口只发STATUS，没有发送LED颜色或游戏命令。

三板boot分别保持`051052E7D06686EA`、`1C69E300D5F214AF`、`6DBF3AB8C5FDFE7A`；ORDER timerOK、rejected0、timing_drops固定1，后继串口的upstream boot与实际前驱一致。HTTP独立确认COM16/9908 → COM17/98b8 → COM18/98fc → COM10/e134，orderIndex0/1/2/3，四块均V0.34/online、3long＋1corner、无conflict，仍unanchored/assigned=false/closedRing=false。COM10本轮仅通过HTTP观察，未控制其串口。此证据仍限于四节点开链，不能扩写为16闭环或完整游戏验收。

| 长边模块 | USB供电下INA电压范围 | 电流范围 |
| --- | --- | --- |
| COM16 | 4.604–4.625V | 118.5–134.5mA |
| COM17 | 4.571–4.582V | 117.3–132.3mA |
| COM18 | 4.540–4.576V | 117.3–133.0mA |

每板均9次RFID Fault/freshNO、count0、TXDIS1/OFF，RFID仍未验收；保持原保护阈值。采集中无panic/reset/json_error/heartbeat_failed或WS2812 ERROR。before/after `/api/state`唯一差异为`identity.serverEpochMs`（1792041096342→1792041268733），其余游戏业务完全一致。

本次证据前缀为`upgrade-batch02-v034-`，均位于trial目录：`pnp.json`、`inventory.csv`、`dry-run.log`、`flash.log`、`flash-events.jsonl`、三份`COMxx-serial.jsonl`、`observations.json`、before/after state及assignments；汇总为`upgrade-batch02-v034-result.json`。主任务已独立核对verified事件及result，并另存计划板台账。

当前计划盘仍为8/16已写验并实际启动（6long＋2corner）：本次COM16/17/18和替换COM10共4块V0.34，首批COM12/13/14/15共4块仍V0.33，需后续授权窗口升级。历史唯一烧录新板仍为9，包含已隔离、根因未闭合的COM19；余下6long＋2corner尚未写入，备用COM10不占用这两块未刷角落。下一步由主任务协调用户更换下一批未刷3long并下发明确身份/串口窗口，格子任务据此继续。三块长边目视、RFID、16环及实体整局分别保留待验。本轮仅更新本报告，不新增测试、串口操作、构建或冻结包修改；共享Git索引与提交继续由主任务持有。


## 第三批4块新板V0.34写验及实际启动（累计12/16）

用户先接入3块未刷长边并进入下载模式/松开BOOT，后将COM10替换为下一块原计划未刷角落。角色由用户实际换板操作确定。当前PnP父身份均OK；固定esptool5.3.1对COM20/21/22及随后COM23各执行一次有界no-reset/no-stub ROM识别，确认均ESP32-S3 rev0.2、16MB Flash、封装8MB PSRAM、liveGPIO0 HIGH、非secure USB-Serial/JTAG。四个完整MAC彼此唯一且与此前已写9个MAC不重复；身份读取结束均释放，未复位或写入。长边身份窗口2026-10-15 05:20:26.990–05:20:28.687 UTC，角落05:22:47.566–05:22:48.103 UTC。

主任务明确授权身份一致后直接完成本批四板写验。仅使用冻结v2（manifest SHA `81a1708b21df573143a93676ca8dbfd86a3df67b3dc821ecb0fca816101fab06`），long应用c08a4508…、corner应用7153f74f…，均normal/V0.34；重新核对所有profile源码清单/烧录段哈希及布局，四行inventory dry-run通过。当前Git HEAD后续文档提交不改变候选构建基线，没有重编或修改冻结包。

固定5.3.1完成全批预检后逐板同连接写入四段、Flash校验、MAC复核、liveGPIO0检查及显式watchdog-reset。4条preflight实际hard-reset，4条verified实际watchdog-reset，主进程exit0。没有erase-all、写NVS、分区迁移、盲目重刷或追加恢复复位；未操作已拔COM10、旧COM6、隔离COM19、COM5或玩家屏。

| 标签 / 串口 | 完整MAC | 普通profile | VERIFIED时间（EDT） |
| --- | --- | --- | --- |
| tile-long-07 / COM20 | `a4:cb:8f:c2:99:60` | tile-long | 2026-10-15T01:23:52.532-04:00 |
| tile-long-08 / COM21 | `a4:cb:8f:c2:98:bc` | tile-long | 2026-10-15T01:24:14.009-04:00 |
| tile-long-09 / COM22 | `a4:cb:8f:c2:99:48` | tile-long | 2026-10-15T01:24:35.343-04:00 |
| tile-corner-03 / COM23 | `a4:cb:8f:c1:eb:3c` | tile-corner | 2026-10-15T01:24:56.606-04:00 |

四口各一次45秒STATUS观察，每5秒请求、DTR/RTS均false，窗口2026-10-15 05:25:24.416–05:26:09.895 UTC。各101条记录、9次HTTP200、9条LED状态，固件实际V0.34、LCD8MHz；四个采集进程均exit0/MONITOR CLOSED，串口已释放并告知主任务。LED全部readyYES/scene、16MHz、reset_us350、attempted=completed持续推进，failed/driver_error/stop_error均0，意图RGB随低亮青绿色呼吸变化。本批实际灯色仍待用户目视确认，角落启动及驱动结果已提前交主任务询问；没有发送LED颜色命令，软件状态不替代光学验收。

| 串口 | LED completed首末值 | ORDER accepted首末值 | INA电压 | INA电流 |
| --- | --- | --- | --- | --- |
| COM20 | 1529→2350 | 0→0 | 4.489–4.520V | 118.3–133.3mA |
| COM21 | 1331→2151 | 11→17 | 4.497–4.531V | 118.3–136.5mA |
| COM22 | 945→1766 | 7→14 | 4.549–4.575V | 115.5–136.5mA |
| COM23 | 544→1364 | 4→10 | 4.546–4.584V | 188.8–205.0mA |

四板boot依次为`C34B75899A668692`、`4EB62807E7039662`、`9B6B4D7184CFDEC7`、`A29A8080B6BBAF1C`，窗口内不变；ORDER timerOK、rejected0、timing_drops固定1。实际串口前驱boot相符，HTTP独立确认COM20/9960 → COM21/98bc → COM22/9948 → COM23/eb3c，index0/1/2/3、3long＋1corner、均V0.34/online，无conflict，仍unanchored/assigned=false/closedRing=false。只验证本次四节点开链，不等于16闭环。

每板9次RFID Fault/freshNO、count0、TXDIS1/OFF，USB供电下读卡仍未验收，保护阈值未调整；角落峰值遥测245.0mA，不能据此完成供电验收或关闭旧COM19根因。观察内无panic/reset/json_error/heartbeat_failed或WS2812 ERROR。before/after游戏状态唯一变化为serverEpochMs（1792041797207→1792041970016），其余业务完全一致；未注入分配、Tag、锚点或游戏动作。

证据在trial目录，使用独立`batch03-`前缀：long-pnp/long-identities、四份ROM日志及corner-identity、before-flash-pnp、release-validation、inventory、dry-run、flash/events/process、四份COMxx-serial、observations、before/after HTTP和最终`batch03-result.json`。离线首版汇总曾将字段名driver_error=0/stop_error=0误匹配为ERROR，已按独立ERROR词修正；初版另存`batch03-result-initial-parser-matched-error-field.json`，原始串口不变，最终错误列表为空，没有为此重开设备或重复采集。

当前计划盘12/16已写验并实际启动（9long＋3corner）：8块V0.34，首批COM12/13/14/15四块仍V0.33待后续升级；历史唯一烧录新板13，包含仍隔离COM19。余下3long＋1corner尚未刷写。下一步由主任务取得本批目视反馈、协调最后一批实物及串口窗口，再安排首批升级；格子任务据明确窗口继续。RFID、供电、全部显示/灯色、16闭环和完整实体对局仍分项待验；共享Git索引与统一提交归主任务。本轮未改生产源码、测试、发布工具或冻结v2。


用户随后明确确认本批COM20/21/22/23四块均为正常低亮呼吸，故本批四板目视灯效验收通过。该证据来自后续用户回复；原始串口的optical_verified=NO及result采集时的pending描述保留，不把软件遥测改成光学传感结论。RFID仍未验收，COM19隔离及16环/实体对局待验状态不变。主任务正在协调最后3块未刷长边，未给出新窗口前不再操作硬件。


## 第四批4块新板V0.34写验及实际启动（16/16完成首次写入）

用户先接入最后3块未刷long并进入下载模式/松BOOT，后将COM23替换为最后一块原计划未刷corner COM11；角色依据用户实际换板操作。当前四口PnP完整父身份均OK。固定esptool5.3.1分别一次有界no-reset/no-stub ROM识别，三long窗口2026-10-15 05:30:44.096–05:30:45.653 UTC，corner窗口05:39:20.255–05:39:20.782 UTC，均exit0并释放。均ESP32-S3 rev0.2、16MB Flash、封装8MB PSRAM、liveGPIO0 HIGH、非secure USB-Serial/JTAG；四个MAC彼此唯一，且与此前13个历史写入身份无重复。识别阶段没有复位或写入。

按主任务授权，身份通过后直接使用冻结v2正常候选：manifest SHA `81a1708b21df573143a93676ca8dbfd86a3df67b3dc821ecb0fca816101fab06`，long应用c08a4508…、corner应用7153f74f…。所有profile源码清单/烧录段哈希及布局重新核对通过，四行inventory dry-run通过。固定5.3.1先全批硬件预检、后逐板同连接四段写入/校验/MAC复核/liveGPIO0检查及watchdog-reset；4条preflight实际hard-reset，4条verified实际watchdog-reset，最终exit0。没有erase-all、NVS写入、布局迁移、盲目重试或追加恢复复位。未碰旧COM19、已拔COM23、首批四板、COM6、玩家屏或其他串口；未因Git HEAD变化重构建或修改候选基线。

| 标签 / 串口 | 完整MAC | 普通profile | VERIFIED时间（EDT） |
| --- | --- | --- | --- |
| tile-long-10 / COM24 | `a4:cb:8f:c2:99:6c` | tile-long | 2026-10-15T01:40:18.017-04:00 |
| tile-long-11 / COM25 | `a4:cb:8f:c2:98:a0` | tile-long | 2026-10-15T01:40:39.451-04:00 |
| tile-long-12 / COM26 | `a4:cb:8f:c2:99:50` | tile-long | 2026-10-15T01:41:00.807-04:00 |
| tile-corner-04 / COM11 | `a4:cb:8f:c6:25:e0` | tile-corner | 2026-10-15T01:41:22.221-04:00 |

四口各一次45秒STATUS观察，每5秒请求、DTR/RTS均false，窗口2026-10-15 05:41:47.303–05:42:32.815 UTC。各101条记录、9次HTTP200、9条LED状态，实际V0.34、LCD8MHz，全部exit0/MONITOR CLOSED；已告知主任务四口释放。LED均readyYES/scene、16MHz/reset_us350，attempted=completed持续推进，failed/driver_error/stop_error均0，意图RGB为低亮青绿色呼吸变化。已在观察期间提前报告COM11实际启动及正常驱动，请主任务取得用户目视反馈；采集结束时本批灯色仍待目视，optical_verified=NO。全过程仅STATUS，没有颜色或游戏命令。

| 串口 | LED completed首末值 | ORDER accepted首末值 | INA电压 | INA电流 |
| --- | --- | --- | --- | --- |
| COM24 | 1250→2068 | 0→0 | 4.506–4.544V | 118.0–144.3mA |
| COM25 | 903→1720 | 10→17 | 4.561–4.586V | 118.0–138.8mA |
| COM26 | 919→1736 | 7→13 | 4.506–4.526V | 117.8–135.8mA |
| COM11 | 490→1306 | 3→10 | 4.564–4.592V | 189.8–206.5mA |

四板boot依次保持`916B7EE27E20CDDA`、`D0E99ABD2B8F845A`、`0F6AD4DF0FBF5261`、`E101AF3771A0905F`；ORDER timerOK、rejected0、timing_drops固定1。实际后继串口upstream boot均等于前驱boot，HTTP确认COM24/996c → COM25/98a0 → COM26/9950 → COM11/25e0，index0/1/2/3、3long＋1corner、全部V0.34/online，无conflict，仍unanchored/assigned=false/closedRing=false。此次仍是四节点开链，未进行16闭环/锚点/对局操作。

每板9次RFID Fault/freshNO、count0、TXDIS1/OFF，USB供电下RFID仍未验收，保护阈值不变。COM11峰值INA遥测240.8mA；这些不是校准电气验收。观察内无panic/reset/json_error/heartbeat_failed或WS2812 ERROR。before/after `/api/state`只有serverEpochMs变化（1792042791267→1792042952970），其余游戏业务一致；未修改分配、Tag、房间或棋子状态。

独立证据均为trial目录的`batch04-`前缀：long-pnp/long-identities及3份ROM日志，corner-identity/COM11-ROM、before-flash-pnp、release-validation、inventory、dry-run、flash/events/process、4份COMxx-serial、observations、before/after HTTP及`batch04-result.json`。没有覆盖此前批次、重开串口或新增测试/构建；本轮仓库只更新本报告，Git统一提交归主任务。

计划盘16/16已完成首次普通固件写验并实际启动，12long＋4corner身份齐全；其中12块V0.34，首批COM12/13/14/15四块仍V0.33，需下一接线窗口升级。历史唯一烧录新板共17，含隔离COM19；替换COM10属于计划盘有效角落，不额外占格。16首次启动不等于16板同时联网、同版本、闭环或完整游戏通过。主任务接续负责本批目视确认、首批四板接线/升级窗口及整盘联调；格子任务在新窗口内继续，不自行打开首批端口。RFID、供电、全部显示/灯色、16闭环、配对拿放及完整实体一局仍分别保留待验，COM19根因仍未关闭。


用户随后明确确认第四批COM24/25/26/11四块均正常低亮呼吸，本批四板目视灯效验收通过。证据来自采集结束后的用户回复；原始STATUS中的optical_verified=NO及batch04-result.json采集时的pending描述保留，后续确认另存batch04-optical-user-confirmation.json。RFID仍未验收，COM19继续隔离；此目视结果不替代供电、显示、16闭环或实体对局验收。主任务正协调首批四块接回USB，后续按新鲜PnP完整MAC确认身份并授权升级窗口，不依据旧COM号自行操作。本次仅补记录，未打开串口、发送命令或修改冻结包。


## 首批四板升级V0.34：COM12应用写入中断，恢复待验

用户将首批四块正常接回USB，主任务新鲜PnP确认COM12/e4、COM13/f8、COM14/ac为long，COM15/e188为corner，并授权正常USB自动下载升级冻结v2。格子任务再次核对完整PnP MAC/状态、manifest SHA81a1708b…、全部profile源码清单与烧录段哈希/布局及四行inventory；dry-run通过，before HTTP已保存，没有重构建或改候选。

2026-10-15 05:47:22.962–05:47:30.965 UTC（EDT01:47:22.962–01:47:30.965），固定esptool5.3.1先完成全部四板硬件预检，MAC/16MB/分区/liveGPIO0均通过，4条preflight_ok的实际复位为hard-reset。随后仅COM12进入正式写入：0x0000 bootloader、0x8000 partitions、0xE000 boot_app0三段均写入并Hash verified；进入0x10000应用段时出现Windows PermissionError(13)/设备没有发挥作用/I/O31，工具exit1并按首个失败停止。应用段没有完成校验，不能保证旧应用仍完整，也不能标作V0.34部署成功；写验后的watchdog-reset尚未完成。

COM13/14/15没有进入正式写入，保留V0.33镜像，但前述preflight已对它们执行hard-reset，不能说本轮完全未操作。没有自动重试、追加复位、erase-all或NVS写入，没有启动45秒观察。所有串口进程已退出，故障日志完整保留。失败后的首次只读PnP查询未见四个目标串口（空管道输出文件保留）；随后05:48:45.032 UTC结构化快照已出现COM12/e4/OK，COM13/14/15仍未出现。设备枚举发生变化，不能仅凭I/O31判定单板、固件或USB供电根因。未据此再次打开串口。

证据使用独立`upgrade-batch01-v034-`前缀：pnp、release-validation、inventory、dry-run、before HTTP、flash/events/process、failure-pnp及failure-pnp-snapshot、`failure-result.json`。首次发给主任务的第二次PnP描述误沿用空列表，随后立即更正为仅COM12恢复出现；以带时间的原始结构化快照为准。

当前计划板固件状态应写为12块已验证V0.34、3块未改V0.33、COM12一块应用恢复待验，不能继续写成四块完整V0.33或16块当前都可运行。历史16块首次写验/启动证据仍保留，唯一新板写入历史仍17（含隔离COM19）；完整16环/RFID/实体对局仍未完成。已将具体失败、部分写入及端口变化交主任务协调连接/COM12下载模式和明确恢复窗口；没有盲重试或自行操作其余端口。本节完成失败阶段交接，不是升级目标完成。


## 首批recovery1完整恢复与升级成功（计划16块均V0.34）

用户随后明确说明“我重新插上了，刚刚我拔掉了”。主任务两次间隔1秒的新鲜PnP均恢复四板完整身份，随后用户进一步确认“四块都进入DFU了”，主任务提示松BOOT并保持USB连接。此前I/O31和端口消失应结合这次主动拔线事实解释，不能再仅据该错误推断hub、固件或供电故障；原单独请求COM12手动DFU的安排由四板已恢复接入及新的明确授权替代。首次失败、部分写入和当时未知应用状态的证据全部保留，不改成一次成功。

按新授权执行一次有依据的恢复，独立前缀`upgrade-batch01-v034-recovery1`。重新确认四口当前PnP/MAC，固定esptool5.3.1，冻结v2 manifest SHA81a1708b…、long应用c08a4508…、corner应用7153f74f…及全部profile清单/段哈希和布局均通过，dry-run成功。全批硬件预检之后，优先对COM12从bootloader、partition、boot_app0到app全部四段完整重写/校验，不假定旧app完整；COM12成功后依次续COM13/14/15。每板同连接MAC/16MB/分区/liveGPIO0通过，4条preflight实际hard-reset、4条verified实际watchdog-reset，进程exit0。没有erase-all/NVS写入或追加盲目重试，没有触碰玩家端、隔离COM19或其他串口；候选与构建基线保持冻结。

| 标签 / 串口 | 完整MAC | 普通profile | VERIFIED时间（EDT） |
| --- | --- | --- | --- |
| tile-long-01 / COM12 | `a4:cb:8f:c2:98:e4` | tile-long | 2026-10-15T01:52:06.112-04:00 |
| tile-long-02 / COM13 | `a4:cb:8f:c2:98:f8` | tile-long | 2026-10-15T01:52:29.581-04:00 |
| tile-long-03 / COM14 | `a4:cb:8f:c2:98:ac` | tile-long | 2026-10-15T01:52:53.120-04:00 |
| tile-corner-01 / COM15 | `a4:cb:8f:c1:e1:88` | tile-corner | 2026-10-15T01:53:16.881-04:00 |

一次四口并行45秒STATUS观察于2026-10-15 05:53:38.194–05:54:23.657 UTC，每5秒请求、DTR/RTS均false。全部exit0/MONITOR CLOSED，已释放并告知主任务；COM12应用恢复后实际运行V0.34，四板均LCD8MHz/V0.34，最后HTTP独立确认为online。COM12/13/15各101条记录、9条LED状态及9次HTTP200；COM14因网络状态变化额外输出，共128条记录、11条LED状态，HTTP200记录10条、http=-5记录2条（一次失败事件和一次状态记录，不是两次独立失败）。

LED全部readyYES/scene、16MHz/reset_us350，attempted=completed持续推进，failed/driver_error/stop_error均0。COM14于05:54:14.525 UTC出现一次heartbeat_failed count1/http=-5，保留Wi-Fi并重试，05:54:15.567恢复HTTP200，间隔约1.042秒；期间意图RGB为19,9,8网络错误色，恢复后为6,17,14青绿。该异常没有重启或RMT错误，不能写成整个窗口无网络失败或始终青绿色。其他三板没有网络失败记录，四板均未见panic/reset/json_error或WS2812 ERROR。

| 串口 | LED completed首末值 | ORDER accepted首末值 | INA电压 | INA电流 |
| --- | --- | --- | --- | --- |
| COM12 | 1648→2466 | 0→0 | 4.576–4.605V | 118.8–134.5mA |
| COM13 | 942→1733 | 11→18 | 4.637–4.655V | 118.0–135.5mA |
| COM14 | 637→1436 | 7→14 | 4.635–4.662V | 117.0–139.0mA |
| COM15 | 413→1230 | 3→10 | 4.610–4.628V | 185.0–197.3mA |

四板boot依次保持`37C42E0DF06BF183`、`71DC95F7F117117F`、`9F8344360C8DC7DB`、`61D47111E4F64238`；ORDER timerOK、rejected0、timing_drops固定1。后继串口upstream boot严格匹配前驱，HTTP确认COM12/e4 → COM13/f8 → COM14/ac → COM15/e188，index0/1/2/3，3long＋1corner、无conflict，仍unanchored/assigned=false/closedRing=false。USB供电下COM12/13/15各9条、COM14共11条RFID Fault/freshNO，全部count0、TXDIS1/OFF；角落峰值遥测238.5mA。RFID仍未验收，保护阈值未改。

本次before/after `/api/state`有两个差异：identity.serverEpochMs（1792043475945→1792043663822）和tagPairing.readerTagRevision（1→27）。前后配对均room993580111/session3202819504/revision13/state timed_out，选中的reader仍COM14，boundMask0/tagUid0；after模块记录tagRevision也为27。其他状态字段完全一致。不能写成只有时间戳变化；本任务只做GET与串口STATUS，没有发起配对、Tag绑定、分配、锚点或对局操作。

用户随后明确确认本批COM12/13/14/15四块均正常低亮呼吸，四板目视灯效验收通过。原始串口optical_verified=NO及result采集时pending描述保留，后续用户确认另存`upgrade-batch01-v034-recovery1-optical-user-confirmation.json`；目视不替代RFID、供电或整盘验收。首次失败与本次恢复成功分开保留。

本次独立证据包括user-context、user-dfu-followup、pnp、release-validation、inventory、dry-run、flash/events/process、四口serial、observations、before/after HTTP、`upgrade-batch01-v034-recovery1-result.json`及analysis-notes/optical-user-confirmation，全部在trial目录。只更新报告和本机证据，没有新构建/测试/源码或冻结包修改，Git统一提交归主任务。

至此计划内16块（12long＋4corner）均完成普通V0.34写验并实际运行过，COM12恢复待验状态已解除；历史唯一新板仍17，包含持续隔离的COM19。该结论来自分批部署，不等于16块同时在线、闭环、RFID或完整实体对局通过。主任务接续更新完整MAC/profile/app台账并统筹后续整盘窗口；当时24V未到，RFID/供电/16闭环/实体整局仍待验，玩家屏依用户要求稍后更新；该句是分批烧录结束时的历史状态。晚间24V供电、16板基本读卡与真实闭环已通过，最新剩余以页首链接为准。本任务串口已全部释放，不自行开启新采集或操作玩家端。
