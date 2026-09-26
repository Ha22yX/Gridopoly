# 格子量产候选、32环与RFID配对（2026-09-25/26）

## 范围与当前状态

本轮只负责 Firmware/TileModule、TileModuleTests 和格子协议/报告；共享服务器Protocol/API由服务器任务实现，发布包与真实设备MAC清单由主任务整合。没有改Git索引，没有打开串口、复位或刷机，没有修改在线房间/玩家/资产/Tag。当前只读PnP看见旧COM6与玩家COM7，COM8未连接；PnP不等同已读取当前ROM MAC或固件。最后有证据的旧双板部署为2026-09-09的V0.32，本轮V0.33仅构建候选。

32块实体尚未到，不能宣称整盘电气、配对拿放或画面通过。PCB任务提供的2026-09-21最后可核网表证明两种板U3网络相同、GPIO13 NC、HTRC/INA/ORDER/LED/LCD接口相同；没有与本批订单绑定的制造包哈希，hardwareRevision明确unknown。未把板名或MAC/OUI当作硬件role，也没有修改PCB。

## 实现

新增 module_profile.h、tile_pairing.h、tile_pairing_json.h、tools/export_candidate.py；修改 platformio.ini、main.cpp、tile_network.cpp/.h；对应新增pairing JSON测试，增强生产函数fixture和ORDER测试。

long/corner两个明确构建环境固定上报moduleRole，默认环境unknown；物理板型不等于地图格号。N16R8/240×320/rotation0/10LED保持最后软件基线，不凭corner机械形状改成240×240或自动旋转。默认LCD8MHz，40MHz仍仅原COM6 MAC本地白名单；RS485 DIR保持低，不启用共享A/B环。

原ORDER V1波形、CRC及6秒周期/15秒TTL不变，每节点独立OUT只发自身boot，直接上游采样天然支持有向闭环。主机新增32节点环同时收发、逐个核对直邻身份和单边断线只失效该边的测试。服务端负责32格/四角间距验证、稳定ID选择角落START和手动锚点规则，不把固件模拟当作服务端拓扑或整盘实测。

RFID请求增加tagScanSeq/tagAgeMs/tagFresh与pairingRoomId/pairingSessionId。tagRevision仅内容变化递增；相同完整扫描也更新scanSeq。扫描开始锁定当前配对会话，完成时记时间；旧缓存、网络重试、后来到达的新session不能刷新原扫描证据。fresh要求stable、完整、无overflow且age严格小于5000ms。碰撞枚举只拿到部分UID、Unstable或缺失消抖保留的旧库存不算fresh。保留原UID集合和视觉防抖，但这不再能成为新绑定证据。

pairing JSON严格类型/范围校验，非法响应不发布半个signal；缺pairing兼容旧服务器，active=false清提示。active配对优先于movement；断网与本地提示lease到期清除。界面用玩家编号、名字可显示部分及动作提示：等待放Tag、成功拿回、已绑定换Tag、只放一个Tag、移走所有Tag、读卡器不可用、超时/取消/完成。蓝色呼吸等待、绿色成功、橙色需处理；不会在模块端自行绑定或执行正常到达。旧模块成功拿回提示与另一个模块下一人的配对可并行，模块只消费各自响应会话。

ASCII字体没有中文支持，中文名字显示问号，始终保留可识别的PLAYER编号。不改变RFID只读UID命令范围，不写Tag。

## 回归与构建

主机生产函数与纯状态机/JSON回归8套62项通过（model12、decoder11、RFID protocol5、presence4、firmware boundaries11、display8、ORDER9、pairing JSON2）。包括真实生产库存函数的锁失败重试、同库存新扫描、缓存过期、消抖不续fresh、旧scan session不可被新会话覆盖、灯效优先级及配对lease的uint32时间回绕。JSON拒绝负数、超范围、浮点/字符串/布尔冒充整数、未知状态且失败时输出原子保留。原移动提示、显示差分和40MHz单机边界回归仍通过。

本机旧Temp工具缓存已不可用；在本地GridopolyTileTools恢复PlatformIO6.1.19/espressif32 7.0.1/Arduino2.0.17及原依赖。首下载镜像DNS失败由PlatformIO回退镜像成功，不改变框架版本。两个profile均为正常固件，无SelfTest。候选清单记录实际DIO/80MHz Flash/16MB，LCD时钟独立。

本地证据根目录：C:/Users/kicof/AppData/Local/GridopolyTileTools/（工具可解析为Windows包LocalCache重定向真实路径）。host-build-v033-final.log、host-tests-v033-final.log、build-v033-final.log；build-v033下各profile有candidate.json和candidate-sources.json。候选schemaVersion1、profile tile-long/tile-corner、sourceDigest、gitRevision和完整segment哈希；gitRevision只记录构建时HEAD，未提交源由source清单定位，不冒充已提交。

烧录segments为0x0000 bootloader、0x8000 partitions、0xE000 boot_app0、0x10000 firmware；不烧NVS镜像、不拷贝整Flash、不修改eFuse。配套导出命令在Firmware/TileModule/README.md，主任务负责离线包整合、实体MAC/role确认、验前哈希、烧后读回/运行验证。候选正常构建通过不授权在本阶段直接刷现场板。

## 未完成与下一步

主任务继续跨端审查、Git统一提交及release工具整合；服务器与玩家任务完成各自候选后需联调。实体到货后先核对制造版/外接屏规格，再逐块MAC/role烧录验证，再整盘供电与32环稳定性、随机上电/断线重接/错误role/角落冲突、多人新游戏Tag拿放、拿回与下一人并行、已绑定Tag提示、RFID fault恢复及正常自动到达隔离。COM8历史读卡fault未因本轮软件测试消失。屏幕/LED实际可读性及64位boot唯一性长期运行亦未作硬件保证。

## 最终候选冻结清单

两种候选均 RAM 70204 字节、Flash 2709857 字节；对应bin 2710224字节。最终8套62测试PASS，全部source与segments哈希独立重算一致。

- tile-long firmware SHA256 `19edea4ba388588f1ba6548e85043ae91bea70e0527c708d3c9c758212d4ebe4`；sourceDigest `c064153da8105a4c04d0b93b7128bf89a987739ad9e4b195999d7c00ed150ba5`。
- tile-corner firmware SHA256 `761bd36ffb4b918d06ebf386a747623fb516cbb8ce68ee2a2d1f46b8e20023ff`；sourceDigest `c064153da8105a4c04d0b93b7128bf89a987739ad9e4b195999d7c00ed150ba5`。

独立重算结果 candidate-validation-v033.json。源码冻结等待主任务审核，未提交Git索引、未部署、没有任何持续运行串口或后台任务。


## 2026-09-26 COM6 有界部署与新服务端联调

主任务明确授权后，仅对COM6长边板部署已冻结V0.33，未重建候选。前10秒无复位串口确认旧V0.32/实际LCD40MHz/HTTP200，且已有Tag `8EFA24DF`（count1）；此前主描述“无Tag”不能作为实际线圈无Tag依据。全程未拿放、绑定或写Tag，未新建局、指定锚点或伪造ORDER。

PnP匹配既有COM6，ROM重复读取factory MAC `28:84:85:ba:9f:e8`、ESP32-S3和16MB Flash。备份首次使用esptool5.3.1整段读取报 Packet content transfer stopped；改16KB块/115200后读到0x214000约2.2MB仍中断，失败无完整备份且未烧写。第二次finally复位。随后仅用此前已验证的esptool4.11只读备份，在33.9秒成功读取0至旧应用尾2771136字节并通过工具内置MD5校验，正常硬复位。根因未证实，不归咎于5.3.1 worker漏初始化：主委派独立只读审查确认connect/_post_connect、watchdog及flash参数初始化均已存在。

备份包括boot、分区表、NVS、otadata与原应用，**不是完整16MB备份**。整个读取文件SHA256 `c5c5e2f0122689c21a57334c43b0627296e185d0bf9b12a9ef45f7f41bf18263`；从0x10000提取的2705600字节应用SHA256严格匹配历史V0.32 `176db3cc3ea78c920f235ebcffafeef423dbe07caf6db7bb89a9e4681e502760`，已保存独立rollback文件。两次失败日志保留，不冒称首次成功。

真正烧写仍使用主任务v2批量工具和固定esptool5.3.1，单行COM6清单/--partial。2026-09-26 05:10:21Z preflight_ok，05:10:44Z verified；预检及同一连接内MAC、16MB、分区兼容、四段写入、四段Flash digest校验、再次MAC与复位全部成功。目标普通固件tile-long候选SHA `19edea4ba388588f1ba6548e85043ae91bea70e0527c708d3c9c758212d4ebe4`。只更新boot/table/otadata/app0，未整片擦除或写NVS，未修改布局；未操作COM7/COM8。VERIFY表示对工具指定flash参数处理后的字节校验，未额外复位做整应用原始SHA读回。

随后60.102秒无复位观察正常结束（MONITOR CLOSED COM6）：V0.33、实际LCD40MHz、HTTP200，12次配对状态全部active=NO/room0/session0、fresh=YES，scan_seq50→269；Tag仍count1/8EFA24DF，无overflow。ORDER boot `427BBC5B8B37B1BC`不变，tx_seq2→12、timerOK、timing_drops固定启动时1、accepted/rejected均0，上游为空。HTTP独立显示online=true/moduleRole=long/firmwareVersion=V0.33/reader stable/orderCapable=true、unanchored、assigned=false、closedRing=false，与当前单板未建环相符；没有把未分配页面当作故障或完整32环。未出现panic/reset日志、json_error或heartbeat_failed。tagAgeMs未逐条打印，序号和fresh来自实际串口，HTTP200及严格新字段解析证明正常报文已被新服务端接受；没有主动伪造心跳取样。

部署前本任务已读到room993580107/version12。主明确此前v9→12源于04:59:35Z服务端重启及旧玩家屏重连，不是游戏动作。本窗口与玩家任务固件部署并行，前后/api/state实际diff为：espnowPeers1→0、onlineMask15→14、identity revision7→8、P1 connected true→false、version12→13以及serverEpochMs。其余字段一致，包括room、游戏、资产及Tag绑定；不能写成所有字段完全无变化。玩家连接恢复由玩家/主任务继续，不由格子任务越权操作COM7。

原始证据目录 `C:/Users/kicof/AppData/Local/GridopolyTileTools/com6-v033-deploy-20260926/`：before/after-state.json、before/after-assignments.json、before/after-serial.jsonl、backup.log、backup-chunked.log、backup-v411.log、backup-manifest.json、com6-before-v033-boot-through-app.bin、com6-v032-rollback.bin、com6.csv、flash-events.jsonl、flash.log、deployment-result.json。所有本任务串口与工具进程已结束。当前COM6已实际升级V0.33；COM8没有接入/未升级，32实体与真实逐人配对验收仍未做，交主协调下一步。
