# 房屋与旅馆图片按需下载

日期：2026-10-16。对应用户要求：默认图片留在格子固件，建房/旅馆图片从树莓派按需下载到内存，重启清空，并控制整盘同时请求的压力。

## 数据与显示约定

格子模块继续使用固件中的零级默认图片。未分配、非地产、空地均不下载开发素材；收到当前地产的1～5级权威状态后，只请求当前等级，不在分配时预加载五级。下载期间显示默认图和真实建筑等级标记；下载失败不阻塞游戏。

服务器在格子分配中提供`buildingLevel`：0为空地，1～4为房屋数量，5为一座旅馆。等级变化和所有者变化都触发分配修订；降级到0立即回默认图。选图依据稳定地产标识，而非16/24/32/40格棋盘的位置编号。

玩家屏保留原有默认图片下载方式，新增等级选择；未知等级回默认图，不使用演示数据猜测。格子固件的默认图片本地化要求不意味着把玩家屏原有全部图像也塞入固件。

## URL与校验

| 用途 | URL | 内容 |
| --- | --- | --- |
| 格子屏 | `/assets/development/v1/tile/{key}-l{1..5}.rgb565` | 160×160 RGB565小端，51,200字节 |
| 玩家屏 | `/assets/development/v1/player/{key}-l{1..5}.rgb565` | 128×128 RGB565小端，32,768字节 |
| 网页资源 | `/assets/development/v1/web/{key}-l{1..5}.png` | 160×160 PNG，长度按目录 |

22块地产、每块5种开发状态，三种输出共330个文件。`v1`内容不可变；修改像素需新版本URL及校验表。旧`/assets/tiles/`保持兼容。

[公共目录](../../Firmware/libraries/GridopolyCore/src/gridopoly/assets/DevelopmentArtworkCatalog.h)仅携带稳定标识、长度和CRC，不包含像素，由[生成器](../../Assets/GridCity/DevelopmentV1/tools/generate_runtime_catalog.py)从已校验素材生成。CRC针对原始传输字节，必须在RGB字节交换前检查；下载完整且长度、CRC正确后才可发布到显示缓存。

## 缓存与拥塞控制

- 格子端：最多5张就绪图片＋1张暂存，图片缓冲上限307,200字节PSRAM；单个后台下载任务。8KiB任务栈、队列及HTTP对象另计。缓存不写Flash、NVS或文件系统，重启后为空。
- 玩家端：640KiB总预算，包含下载暂存。图片对象持有缓存引用，在显示期间不可回收；使用地产＋等级＋版本键。
- 首次请求按设备身份和本次随机种子错峰250～2000ms；失败从1秒指数退避到30秒，并附加随机延迟。尊重服务器`Retry-After`，避免一批模块在同一秒重复冲击。
- 服务器对新开发素材默认限制同时4份传输、每秒8张、突发4张；繁忙立即返回503和`Retry-After: 1`，不让等待占满业务线程。旧头像/默认图片不套用新限流。
- 分配、等级或请求代次改变后，旧请求不得覆盖当前图。下载和显示共享像素时必须有明确生命周期保护，不能释放仍在绘制的缓冲。

零级新局的开发素材请求数为0。恢复已建造对局时，每个模块仅加载自己当前需要的一张；无需同时传输整套110张。

## 验证与现场记录

主会话公共测试在MSVC C++17、`/W4 /WX`下通过：330个文件长度/CRC正确，各翻转一位后均被拒绝；覆盖CRC分块、时钟回绕、退避上限、无效Retry-After及40个设备错峰分布。生成目录`--check`通过。测试文件：[development_artwork_tests.cpp](../../tests/host/development_artwork_tests.cpp)。这些结果不替代真实网络并发或实板下载验证。

用户接入四块计划板，USB身份及10秒有界STATUS观察均核对成功：

| 本次端口 | 完整MAC | 角色 | 更新前版本/分配 |
| --- | --- | --- | --- |
| COM20 | A4:CB:8F:C2:99:60 | long | V0.36 / A1 |
| COM21 | A4:CB:8F:C2:98:BC | long | V0.36 / CARD-CF-1 |
| COM22 | A4:CB:8F:C2:99:48 | long | V0.36 / A2 |
| COM23 | A4:CB:8F:C1:EB:3C | corner | V0.36 / CORNER-HOLD |

四口HTTP200、旋转2/180°、LCD8MHz，采集结束均已释放。更新前房间993580116，所有观察到的格子建筑等级0。没有为测试修改房屋、资金、房间、Tag或模块分配。

### 服务器已部署

服务器候选`19c8cbad5b68268da159e7d94944d88b10ebc0d70d999afb0af8a1b1c2241a9f`（641,624字节）已替换原`ea6e1f51…`，PID747219。先发布330个素材，再备份游戏数据并只重启游戏服务；72个旧素材和核查的网络配置保持。备份位于Pi的`/var/lib/gridopoly-artwork-deploy/gridopoly-artwork-deploy-20261016-200917`。

部署后实际HTTP抽验三种输出各L1/L5共6份，长度/SHA正确；15组只读样本均16在线、16分配、ORDER ready。room993580116/phase0及资产保持；version、身份时间戳、Tag配对session/revision变化单列，配对complete及boundMask7保持。完整回归见[服务器报告](server-development-artwork-progress-2026-10-16.md)：Linux打包19/19，32/40并发及慢接收/中断释放、控制/心跳/头像响应均通过。这些回环时间不是36台无线现场的性能保证。

### 四块普通V0.37与实板缓存

本次四块COM20～23均已完成预检、四段写验及watchdog启动：三块long使用`aef76554ce0bd71f945f41f5298cae62c463560aff70058e929dafe5b77d5a17`，corner使用`963f0b66960adf6095fa09698503f9967b9babcf28443d5531131e592a7ece69`，应用各2,724,672字节。源快照82项、共同sourceDigest`75a822d2c7fd9452e24f3500a5a9d657238a44d14581f7935b2058c431915218`已独立核验；从已验证普通V0.36制备，未将共享目录里的历史RFID stage2诊断混入实板。制备材料见[格子报告](tile-development-artwork-cache-2026-10-16.md)。

COM20使用与生产相同的worker、缓存、CRC与绘图入口进行有界TEST探针，没有修改权威建筑状态：

- 空地自动分配后：requests0、ready0、图片RAM0，无开发图片请求。
- A1等级1～5：五次HTTP200、各51,200字节、CRC一致并完整发布。用户明确确认“图片完整，方向和颜色正常”。
- 切回L1：cached=YES，requests仍5，直接复用内存。
- 第六张A2旅馆：requests6、ready仍5、图片RAM307,200，满足5张就绪＋1张缓冲上限。
- 看门狗重启（不重写Flash）：requests0、ready0、RAM0；重新查看A1旅馆为cached=NO，再发request1并成功校验发布。
- TEST每次30秒自动退出，显式STOP同样恢复最新权威页；下载期间HTTP心跳保持，批量重启引起的ORDER暂时撤销/恢复照实记录。

COM21/22/23的开机STATUS分别验证普通V0.37、8MHz、rotation2、HTTP200及默认页requests0/RAM0；它们恢复原CARD-CF-1/A2/CORNER-HOLD分配。四块写验不等于其余12块更新：**第一批完成时为4块V0.37＋12块V0.36；随后第二批完成，现为8块V0.37＋8块V0.36**。玩家屏源码/候选与实际部署分开见[玩家报告](player-development-artwork-2026-10-16.md)，本轮未刷玩家屏。网页新资源端点可用，但网页选图未接入。

本机原始证据在`C:/Users/kicof/AppData/Local/GridopolyArtworkRuntime/`，包括写验、各口有界串口、六级缓存与重启记录、部署和HTTP快照；不提交凭据、固件或游戏存档。真实玩家操作建房/拆房后的自动换图尚待对局验收，不能用TEST探针替代。32格＋4屏真实无线压力仍需对应实物。


### 第二批写验与整盘恢复（累计8/16）

用户确认第一批四块正常后换接第二批。2026-10-16 16:22～16:24 EDT完成下列四板的完整MAC/16MB/分区预检、四段写入校验和watchdog-reset；使用与第一批一致的冻结普通V0.37，未重置NVS。

| 端口 | 完整MAC | 板型 | 恢复的格子 / mapIndex |
| --- | --- | --- | --- |
| COM24 | A4:CB:8F:C2:99:6C | long | B1 / 5 |
| COM25 | A4:CB:8F:C2:98:A0 | long | T-WEST / 6 |
| COM26 | A4:CB:8F:C2:99:50 | long | B2 / 7 |
| COM11 | A4:CB:8F:C6:25:E0 | corner | CORNER-REST / 8 |

四口分别12秒有界STATUS：V0.37、8MHz、rotation2、HTTP200、assigned=YES，开发图片requests0/ready0/RAM0。采集正常结束，全部释放。随后连续5组HTTP快照均16在线、16已分配、ORDER ready；逐模块格子与mapIndex同第一批结束时完全一致。room993580116/version17保持，服务器新图片admitted仍13、active0，确认这一批默认页未产生新开发图片流量。

证据：本机`second-inventory.csv`、`second-flash.jsonl`、`second-COM*-status.jsonl`、`second-live.json`及`upgrade-ledger.json`。当前8块V0.37、8块V0.36；第二批页面目视已向用户询问，尚未用日志替代确认。第一批用户反馈为“四块正常”。
