# 16格试玩：格子端兼容、候选与首批身份记录

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
