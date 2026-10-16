# 分级建筑图片：Pi资源服务与格子等级投影

更新：2026-10-16（America/New_York）。本轮按用户要求保留设备内0级默认图，1～4房与旅馆按需从Pi下载到客户端RAM。本文记录服务器范围，客户端RAM缓存、画面及实机验收由玩家屏/格子会话分别交付；网页本轮没有改选图逻辑，仅提供PNG端点。主会话统一审核、Git和现场部署。

## 已实现的服务与兼容边界

| 请求 | 内容 | 正常响应 |
| --- | --- | --- |
| `/assets/development/v1/tile/{key}-l{1..5}.rgb565` | 160×160 RGB565 LE，51200字节 | HTTP200、固定Content-Length、ETag、immutable |
| `/assets/development/v1/player/{key}-l{1..5}.rgb565` | 128×128 RGB565 LE，32768字节 | 同上 |
| `/assets/development/v1/web/{key}-l{1..5}.png` | 160×160 PNG，目录记录的精确长度 | 同上，Content-Type=image/png |

只接受共享`DevelopmentArtworkCatalog.h`的22地产、1～5级与三种路径组合；0级不发布到新目录，旧`/assets/tiles/`和固件默认图保持。各路径组件通过`openat`/`O_NOFOLLOW`读取真实目录和普通文件；未知key、错误级别/扩展名、穿越、符号链接、缺失/多余字节或CRC不匹配均不能返回200。服务端先按预期长度限界读取，再核对共享目录CRC，PNG也核对整文件CRC。正常下载不压缩、不分块。

成功响应使用`public, max-age=31536000, immutable`与内容ETag；有效If-None-Match可返回304，无正文且省略Content-Length。实际文件验证先于304判断，损坏文件不能借旧ETag伪装成功。非GET返回405，坏路径/坏文件返回404；错误响应no-store。

`assignment`新增`buildingLevel`，由权威资产投影：PROPERTY且有效owner时保留合法0～5，5表示旅馆；非地产、未拥有或异常等级回0。抵押标记不单独抹去合法等级。等级变化与owner变化一样推进全局和该assignment的revision，不修改游戏状态或存档；ORDER重投影也比较等级。老客户端可忽略新增字段，新格子端按`artworkKey`与`buildingLevel`构造固定v1路径。

## 分发限流

新development资源共用最多4个活动发送、每秒8张、burst4的token bucket。permit覆盖文件读取/校验及实际socket发送，发送结束或失败后释放；不会因进入send前释放而失去并发边界。发送缓冲设为8192字节，使慢接收端保留活动名额。资源连接完成后关闭，避免下载后空闲keep-alive占用worker。

没有活动名额或速率token时立即返回HTTP503及`Retry-After: 1`，不sleep、不排队等名额。正常32worker配置为4；小worker配置为`min(4,max(1,workerCount/2))`，例如4worker仅2个新图发送，给心跳、控制和头像留处理余量。旧tiles与头像接口不纳入该限流。`/health`新增`developmentAssets`的active/peak/limit/admitted/rejected计数。

此限流针对图片突发，不宣称抵抗任意慢HTTP请求或互联网流量；管理API仍属原可信LAN范围。客户端必须按共享策略错峰并处理503退避，服务端不会为客户端抢占心跳线程或持续预取整个目录。

## 资源暂存与v1不可变

新增`Server/RaspberryPi/tools/stage-development-assets.py`，从`Assets/GridCity/DevelopmentV1/manifests/development-v1.json`选择110个新增状态，每状态tile/player/web三文件，共330文件、12927172字节。清单SHA、RGB长度与PNG完整块/CRC/尺寸均校验；发布文件名/来源/长度/SHA的集合指纹固定为`5a546ea2b2b9ad6256d243d076bf6efd94b3d969bff99ac9543c753f250dd458`。报告状态字段可更新，v1发布字节不能静默替换，改图需新版本URL和客户端目录。

工具先写同文件系统临时目录，核验完成后rename发布；目标已存在时只接受330文件完全相同的幂等核验，不覆盖、不修补不完整或不同内容。`--verify-existing`只验证已有目录。示例目标是完整v1目录：

```sh
python3 Server/RaspberryPi/tools/stage-development-assets.py /private/stage/development/v1
python3 Server/RaspberryPi/tools/stage-development-assets.py --verify-existing /private/stage/development/v1
```

旧`stage-web-tile-assets.py`仍生成72个原素材，并在`development/v1/`加入330文件，总402个。`install.sh`在替换程序前核验已有v1字节，递归设置资源权限；其全量网络安装逻辑未用于现场。现役USB部署不得通过重跑全套installer来更新这批图。

## 构建与回归证据

最终源快照、逐文件SHA、构建日志及资源在：

- 本机：`C:/Users/kicof/GridopolyReleases/server-development-assets-20261016-200352/`。
- Pi隔离目录：`/home/kicofy/gridopoly-development-20261016.Bv2WB1`。
- `development/v1/`与`asset-manifest.json`：330资源原件和逐文件SHA。
- `development-v1.tar.gz`：6402115字节，SHA256 `59c0bd67a4092d2184a512cd01883a5e3b78ef321fb607307ddf79280f02cfd7`。

Windows暂存回归19例中18通过、1个POSIX符号链接用例跳过；Pi Linux随后19/19通过，无跳过。旧72素材与新330联合暂存、头像目录验证和头像暂存回归通过。公共`development_artwork_tests.cpp`已加入native脚本，验证330实际文件长度/CRC、逐文件单bit损坏拒绝及40设备错峰；该共享测试由主会话维护。

最终隔离HTTP集成已通过：330文件逐字节一致，三种真实HTTP路由、ETag/304、未知/穿越URL、缺失/截断/增大/同长度损坏、符号链接拒绝、限流恢复；从独立存档恢复的5级地产经真实assignment与heartbeat返回buildingLevel5，忽略伪造query等级。级别升1→2→3→4→5→拆4→0、无主/非地产清0及revision回归通过。

| Linux回环负载 | 已观察结果 |
| --- | --- |
| 32并发下载 | 4个200、28个503，active peak4；8轮心跳/health/state批次最大耗时小于1ms |
| 40并发下载 | 4个200、36个503，active peak4；同上最大耗时小于1ms |
| 32worker＋4个慢接收端 | 下载保持4个活动名额；忙响应、health、heartbeat及头像合计2ms；RST后名额全部释放 |
| 4worker＋2个慢接收端 | 活动上限2，其余控制/头像合计2ms；RST后名额全部释放 |

上述时间为该隔离Pi回环窗口的毫秒粒度读数，不是20/36台无线实测保证。第一轮旧快照`200052`也通过常规并发与生产构建，但没有最终慢发送/304/5级wire验证，不作为交付版本。

最终`200352`快照的公共development、tile assignment、完整HTTP asset、onboarding HTTP/UDP四个native测试目标全部通过，正常生产程序构建通过，`build-result.json`退出0。编译器g++14.2.0，C++17、`-O2 -Wall -Wextra -Werror -pthread`；测试没有NDEBUG，assert开启。本轮选择这些目标，不声称完整仓库CTest通过。

最终候选`gridopoly_server`为641624字节，SHA256 **`19c8cbad5b68268da159e7d94944d88b10ebc0d70d999afb0af8a1b1c2241a9f`**。541项源快照与当前工作区核对无漂移；共享目录/策略和原有WebUi/WebUiGzip在清单中，网页本轮没有新增选图行为。原AP脚本/env/watchdog哈希与本轮前一致。

## 现场交接与部署顺序

部署前只读核对：当时游戏为PID721、SHA256 `ea6e1f517c597cc69f6f0724af0d501206cf3d57e457945c9cc138fc3f2d118d`。`GRIDOPOLY_DATA_DIR=/var/lib/gridopoly`；未设置`GRIDOPOLY_WEB_ASSET_DIR`，按main默认使用`/usr/local/share/gridopoly/tiles`。该目录当时有72个旧素材，v1目录尚不存在；路径与旧素材SHA保存在私有`live-deployment-inputs.json`。这是本轮构建交接时的带时间读数，部署后结果见下节。

主会话实施受控更新时：

1. 核对候选程序、330资源清单、源快照和当前生产版本，保留旧binary与`/var/lib/gridopoly`备份；现有AP/USB配置及72默认素材不替换。
2. 在`/usr/local/share/gridopoly/tiles/development/`同一文件系统暂存完整v1目录，逐文件核对数量/长度/SHA并设置目录0755、文件0644。目标v1若已存在，只允许与清单完全一致，不覆盖不同字节。
3. 原子rename新目录为`v1`后再替换程序。旧程序不引用新路径，因此可先准备资源；新程序不会先遇到半套资源。
4. 候选binary在`/usr/local/bin/`同文件系统暂存并核SHA、权限，原子替换后只重启`gridopoly.service`；不运行全套installer，不重启AP/DHCP/watchdog，不恢复旧房间或注入建房动作。
5. 核对原房间/业务恢复、默认素材保持、限流后控制接口及真实设备下载/画面。若新程序失败，恢复旧binary并只重启游戏服务；已核验的新增v1可保留供诊断，旧程序不会使用，不必动存档或网络。

本端没有生产部署、刷机、新局或建房操作，也没有更改既有未提交AP脚本/env、PCB或Git索引。主会话负责部署及真实4板/整盘端到端验收；两端负责RAM缓存、重启清空、下载失败回退和对应画面。物理Wi-Fi负载、RAM峰值与实际建拆房后的显示仍需设备证据；此次回环负载不能替代这些项目。

## 主会话受控部署已完成

2026-10-16主会话已将上述精确候选部署到Pi；本端独立读取其私有部署与HTTP证据文件，服务端实现继续冻结。运行程序SHA256为`19c8cbad5b68268da159e7d94944d88b10ebc0d70d999afb0af8a1b1c2241a9f`、641624字节，部署后PID747219。330份资源先原子上线，再替换程序并仅重启游戏服务；旧72份素材SHA及网络配置保持。旧程序/数据备份位于Pi的`/var/lib/gridopoly-artwork-deploy/gridopoly-artwork-deploy-20261016-200917`。

主会话抽核tile、player、web三种路由各L1/L5，共6份实际HTTP资源，长度和SHA均匹配冻结清单。随后15组只读观测全部为16模块在线、16已分配、ORDER ready；room993580116及phase0保持，资产与业务状态未变。记录中的变化字段为version、identity和tagPairing；配对仍complete/boundMask7，不能把这些连接/会话字段变化写成整份状态逐字节不变。

证据：`C:/Users/kicof/AppData/Local/GridopolyArtworkRuntime/server-deploy-result.json`及`post-deploy-http-check.json`。最终源码复核文件`final-workspace-drift.json`明确为`drift: []`，541文件零漂移；此前会话消息中关于共享测试变量重命名被检测为漂移的措辞不准确，冻结快照已与当前测试一致，无须改写产物或清单。

至此服务端源码、选定回归、生产构建、资源发布和服务部署均有证据。Git审核与提交由主会话统一处理；两端候选部署、真实下载/缓存重启与建拆房画面仍由对应会话和主会话继续验收。网页本轮仍未接入新等级选图；服务端提供PNG并不表示网页已显示分级建筑。

## COM12瞬时HTTP断连：只读分析，根因待复现

第三批V0.37升级后，主会话提供COM12（`a4:cb:8f:c2:98:e4`）的STATUS与独立复查记录。本端只读核对日志、冻结源码和实际构建所用Arduino 2.0.17 HTTPClient实现。以下时间均为2026-10-16 America/New_York（EDT，UTC−04:00）的主机串口接收时间，不是设备事件的精确发生时间。

| 接收时间 | 记录及解释 |
| --- | --- |
| 16:29:50.700 | 首段窗口打开，DTR/RTS均false；窗口持续12.035秒。 |
| 16:29:50.748、16:29:51.541 | STATUS显示进入窗口时已有的`NET ERR/http=-5`；第二条是同一错误状态的重复观测。没有捕获这次失败的起点，不能当作两次新失败。 |
| 16:29:51.703、16:29:51.945 | 先出现assignment发布，再观察到`AUTO/http=200`。 |
| 16:29:56.789 | 窗口内唯一新捕获的`heartbeat_failed count=1 http=-5 retry_http=YES keep_wifi=YES`。 |
| 16:29:57.842、16:29:57.947 | assignment再次发布，随后`AUTO/http=200`；距离上述失败日志分别1.053秒、1.158秒。 |
| 16:30:02.735 | 首段窗口关闭并释放串口。 |
| 16:30:31.330～16:30:43.363 | 独立复查12.033秒；16:30:31.944的STATUS为`AUTO/http=200`，窗口无新增失败日志。 |

两段保持同一boot `B499D607A48FEE2E`、B3/map9、assignment revision170及assigned状态，未见Wi-Fi断联事件。实际SDK中`HTTPC_ERROR_CONNECTION_LOST=-5`，读取超时为`-11`；本路径的`sendRequest`返回-5表示等待响应头期间连接丢失，错误码本身不能确定哪端或何种原因关闭连接。现有`tile_network.cpp`在非200后清理HTTP transport，保留Wi-Fi，下一轮心跳重新连接；这不是同一次请求内的无感重试。独立复查只能证明该短窗口未再现，不能写成长期网络稳定性全部通过。

没有证据将这次症状归因于图片更新：COM12所有ART状态均`requests=0`、`ram=0`；`final-four-live.json`、`second-live.json`和`third-live.json`的图片累计admitted均为13、rejected0、active0。图片任务使用独立的WiFiClient/HTTPClient并禁用复用，心跳使用另一组连接对象。服务器新增的强制关闭与发送缓冲设置仅用于development资源路径。三份health的HTTP rejected、sendErrors及keepAlivePreemptions均为0，采样时active17、queued0；这些快照不替代逐连接追踪。

既有连接时序保留为后续待复现项：服务器每连接最多100请求，第100次响应明确`Connection: close`，客户端读取正文后按此关闭；当前短间隔及缺少请求计数不支持直接以100次上限解释。`HttpServer::readRequest`的同一个3秒deadline同时覆盖keep-alive空闲、请求头和POST正文；正常2秒心跳等待会消耗其中一部分，留给后续收包的时间更短。这一逻辑在旧`0fed0ce`已存在，本轮没有改变。三份health的全局timeouts为9→37→60；后续keep-alive请求头的普通空闲超时不会增加该计数，计数来源是首请求头或正文读取超时，尚不能定位到COM12。共享deadline、TCP关闭时序等均未由此次日志证明为根因，不将它写成已定位或已修复。

原始证据位于`C:/Users/kicof/AppData/Local/GridopolyArtworkRuntime/`下的`third-COM12-status.jsonl`、`third-COM12-recheck.jsonl`及上述三份live快照。下一步由主会话先完成已冻结V0.37全盘升级及本轮收尾；后续若续派传输问题，先隔离复现空闲等待、正文deadline与请求次数上限的区别，再决定修复。本次仅追加报告，未改源码、重启、部署或修改线上状态；Git由主会话统一审核提交。
