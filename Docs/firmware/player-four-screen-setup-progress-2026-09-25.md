# 玩家屏四人配对、终局与整组清算进度

> 2026-09-27阅读提示：本文件按时间保留阶段证据，早期“当前/未部署/通过”仅指当时。最新实现与尚待验收见[当前交接](../CURRENT-DEVELOPMENT-HANDOFF.md)，实际设备版本见[部署台账](current-device-connections.md)，32环/配对见[发布记录](batch32-game-release-2026-09-26.md)。不要将旧输入确认、旧core0性能或双板结果扩大到最新固件/完整32实体。

2026-09-25 / 09-26 EDT，玩家任务。主任务统一审核和提交共享 Git；本端未暂存、提交或烧录。实际仓库为 `Z:/Files/我的项目/Gridopoly`。

## 本轮源码范围

- 全六字节 MAC 生成稳定 FNV-1a 设备 ID，发现名 `GP-<12hexMAC>`，声明 TagPair capability。原先截取 MAC 低32位会忽略同批设备后两字节；这次 ID 会改变，不能假定旧席位已自动迁移。新局的在线真人冻结及补机器人由服务端决定。
- 接入冻结的 TagPair Request20B / Snapshot88B，独立 revision/session/requestId，不用游戏 stateVersion 比较配对版本。20B/88B 编解码与字段映射有真实协议测试。
- 等候顺序、指定 reader/格子、放置、清空、成功拿回、UID已绑定、多Tag、读卡器离线、超时、取消、重试和保存失败提示。只有当前活跃玩家可 Retry/Cancel；请求携带 room/session/revision/self。
- SelfBound+CanEditIdentity 后展示约1.2秒成功拿回提示，再继续该玩家头像/名字设置；下一玩家的配对更新保留前一人的本地草稿。不把“上一Tag必须拿走”作为所有其他reader和身份编辑的全局门槛。
- 未配对不能通过旧 Identity stage、头像预加载回调、手写或触摸进入身份提交。TagPairing stage7不会被旧stage1..6的大小比较误判ready；一旦房间要求配对，缺失Enabled标志不会解除门禁。
- 同房服务器重启只有新认证transport session的本地epoch标记允许降低配对revision。wire Resync不能降版本；事件合并和满队保留未交付epoch标记，同时transport缓存仍拒绝同epoch旧revision。
- 补充持续 GameOver 胜负/赢家页、破产旁观页，禁止终局掷骰；普通权威推送、重连resync都能恢复，重开清理旧赢家/破产状态。
- 显式 `SELL 1` 保留 SellBuilding argument0；`SELL GROUP` 使用argument1，展示整组总收益、全部同色组建筑出售及土地归属保留，允许取消，需长按1.2秒且只能提交一次。报价改变时需重新查看再确认。债务资产进入同一详情页，heartbeat不强行踢回，返回仍回强制筹款列表。
- 全盘权威资产计算库存：level1..4占普通建筑，level5占地标；上限取BoardCatalog，32格为普通24、地标10。普通库存0不能建普通建筑、地标库存0不能升级，地标降级需要银行至少4枚普通建筑。全组清算无需中间库存，收益严格为各地产 `level * (buildingCost / 2)`。

## 自动验证证据

证据根目录 `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/`。

- `host-setup-group-client-20260926.log`：65项主机检查。实际链接 app_state、Protocol、BoardCatalog、GameEngine，涵盖65536个同MAC前缀设备无ID碰撞、四屏独立配对和草稿、旧room/seat/revision、认证重启、终局/旁观，以及库存/整组清算与真实Core资格和执行对照。
- `host-tag-transport-20260926.log`：7项。实际生产TagPair parser/codec以及从生产源码提取的push/poll；只stub无线I/O/调度。验证队列满后的epoch恢复、事件合并、低版本拒绝和错误room/seat/request过滤。不是实体认证/无线压力测试。
- 原旋钮主机15项通过（含4000次转换），`host-rotary-20260926.log`；移动提示28项通过，`host-cue-final-20260926.log`。
- 字体101个非ASCII覆盖、已有布局检查通过。新清算提示依据实际字体advance测量：确认说明197.4/240px，三个禁用按钮92.6–103.8/132px，土地保留168.8/232px。原较长库存按钮文字超宽已缩短。此为静态字宽核对，不替代实体目视。
- 初次债务测试曾因为样例欠款超过全部可清算价值而进入权威破产页；改为整组出售可筹足的样例后债务入口与命令通过。没有修改规则来掩盖破产结果。

## 构建与来源

首轮正常fresh构建 `runs/production-20260926-001450-7fc9bf838f8b42498742e4d654595e57` 已通过：program1,899,282B，RAM125,632B；链接HWCDC检查通过。包含四屏/终局，不含随后整组清算，保留作阶段证据，不作为最终交付。

第二轮 `runs/production-20260926-003026-6bc4a99a5950449eaa70987689a943ba` 冻结2126个文件，包含整组清算，fresh构建通过。随后保留完整基线源与产物到同路径后缀 `-before-final-20260926-004124`，对7个已审核文件做同路径受控增量：epoch三文件、ui_renderer字宽、README、Core GameEngine.cpp/.h；其余2119文件SHA不变。

00:42最终增量构建通过。普通app1903040B，program1,902,886B，RAM125,632B，HWCDC链接检查通过；app SHA为 `07e830d98278268ea87d7c0f402f8e18aa462294e5921d88bf0d0997944b1f99`。最终交付是上述run目录中的 `candidate.json`，sourceDigest为 `0bb7c156cbf082d2402e2f278949eeaf3a99826dd7ee3a42c05c727ae2ece3de`，Git基准 `5e18b5c6061ee1c32a9cf3a228747c2d690931de`。`incremental-changes.json`记录前后SHA及基线位置。

独立Python重新计算2126快照源和4段产物SHA，全部匹配；与本次核对时当前源的差异为0。确认完整基线2126源和4段产物仍可核验，差异集合严格等于7项审核清单；直接调用发布工具 `flash_layout.validate_images` 通过。最终源码含服务器确认的Core SHA `65e9bf786af69a9d17c724c462f27a1fb15e994bbdd91e15fb1551cfc7a1e706`；65项客户端测试已在该Core上重跑通过。

原始构建日志分别为 `four-screen-production-20260926.log`、`four-screen-group-production-20260926.log`、`four-screen-final-incremental-20260926.log`；最后一次有界增量脚本在证据根目录 `final-epoch-core-incremental-20260926.ps1`。所有构建进程已结束。

构建工具现在冻结5个外部板卡/IO库，Arduino ESP32 core3.3.11，USB HWCDC、Flash16M、app3M/fat9M、OPI PSRAM。最终 `candidate.json` 标记profile/player、normalFirmware:true、ESP32-S3，参数从生成flash_args读取，记录bootloader/partitions/boot_app0/app四段及sourceManifest/sourceDigest。清单Git revision仅指构建基准；未提交差异以快照文件SHA为准。本地配置只存哈希，不在报告或发布包复制明文。

冻结界限为sketch、Core/Protocol/LVGL项目库和5个外部板卡/IO库。ESP32 core3.3.11及其安装工具链使用固定版本，本轮没有整体复制进2126文件的清单。烧录参数为DIO/80MHz/16MB，offset依次0、32768、57344、65536；这是实际生成参数，不是LCD时钟。

## 设备实际版本与未完成验收

本轮没有烧录、没有改游戏状态或Tag绑定、没有创建自动化。COM7当前仍依据先前部署链为正常core1 `b53e31f2bb8f1ae6befbc132a734904575862020ff2c3ed794922e5c3d8d7250`，不含本轮新功能。9/25被动15秒观察为新鲜WiFi status3/IP10.42.0.37，无reset；未重读app flash hash，不冒称此次验证了设备哈希。串口已释放；另外3屏由主任务后续确认MAC与连接。

主任务负责候选复核、批量包、设备角色/MAC清单与部署协调。真实四屏同时发现、逐人实体Tag放取、前一人自定义/后一人配对、故障和重启恢复、整局胜负、重开，以及屏幕可读性均待实机联调。本端继续承担玩家UI/transport修复。

原旋钮操作用户已确认；core1 Retarget历史max57ms仍未达42ms，频闪/错位回退后的原场景目视尚未获得用户确认。本轮未改变RGB绑核/PCLK/bounce/frame ticket，也没有新增性能benchmark；弃用core0的帧率PASS不能用作当前显示验收。

## 后续：首次入局等待页补丁（取代07e830阶段候选）

只读审核证实：旧room拒绝新完整MAC身份时，07e830的客户端只重置transport，冷启动会留在WAITING游戏首页并显示初始化的$1860/TILE17，且没有新建局提示。主任务明确要求补齐这个流程，保留既有提交0188bfd，不改旧room或自动映射旧设备ID。

正常网络固件现在由启动点明确调用 `appInit(..., waitForAuthority=true)`，第一帧就是独立WaitingForGame页面，无示例金额、位置或游戏操作。默认参数false及SelfTest使用的Demo transport保持原诊断场景；没有把性能测试悄悄改成等待页，也没有新跑benchmark。

初次Connecting显示寻找游戏服务器；经过真实UDP envelope认证、room/ack/server/session校验后的PairAccept拒绝，在reset/clearProjection之后可靠投递JoinStatus，持续显示 `WAITING FOR NEW GAME / Start a new game on the web panel.`。等待页不画“Wi-Fi断线”红图标；普通发现重试保留新建局提示，真实链路丢失可以切回Connecting。真正接纳先显示Joining Game，直到合法设置期Identity或完整/简版游戏权威状态替换。进行中游戏的Active Identity只有姓名/头像，不能单独打开含尚未知现金的Home；早到Tag也不绕过这个门槛，已完成配对不会在恢复进行中游戏时误闪取回成功页。

已经取得真实状态的屏幕保留原业务页和值，临时失联、拒绝重试和恢复仍使用断线标记及原权威resync语义。等待期间原Home触摸、Demo入口、长按和操作队列均不会进入游戏。

最新验证：`host-join-client-20260926.log` 共83项PASS（取代前65项结果），包含新增18项首次初始化、重复拒绝、重试不丢提示、真实State/Authority不同到达顺序、合法Identity/Tag、既有业务断线恢复、Active Identity不提前显示现金及原诊断默认值不变。`host-join-transport-20260926.log` 10项PASS，编译实际生产UDP认证/PairAccept/reset/push/poll函数和真实SHA256-HMAC/协议编解码，只stub无线I/O调度、无关消息与外设类型，验证满事件队列先清后可靠投递、重复拒绝、真正接受、篡改包/旧ack/错room/错端点/畸形server或seat不能冒充拒绝原因。Tag生产队列7项、移动提示28项回归仍PASS，glyph/layout和新增文字实际字体宽度检查PASS。这些是主机验证，不是4台实体屏幕的现场结果。

两次受控增量记录：`waiting-admission-incremental-20260926.log` 合入8文件；`waiting-gameplay-state-incremental-20260926.log` 再仅更新app_state.cpp以处理Active Identity先到。旧07e830源/4段产物完整保存在原run路径后缀 `-before-admission-20260926-005900`；第一轮等待页候选另保存在后缀 `-before-gameplay-state-20260926-010129`。每层都有重定向后可独立核验的candidate/sourceManifest以及上一层incremental-changes记录。

**最新交付**仍是证据run `production-20260926-003026-6bc4a99a5950449eaa70987689a943ba/candidate.json`，app SHA `109df53f3941db281cc710a282fd5cbea700a70c73d6ef3316ab8350d55537b0`，文件1,904,176B，program1,904,030B，RAM125,632B，HWCDC检查PASS。sourceDigest `9e4ef62c9eeef4e70bbf4e6e10f04a20be3fbed09d8ff018d9c9afcff1434177`，Git基准 `0188bfd9e9bbfc955fdcc1c2df514ff0aef24c82`。再次独立重算2126快照源、全部4段SHA，均匹配且当时与当前源差异0；相对07e830仅8源文件变化、其余2118完全不变，发布flash_layout校验PASS。完整核验结果保存在 `admission-verification.json`。

本补丁仍未烧录，COM7实际版本及显示问题边界保持前节说明。主任务需用109df候选替换阶段发布包，审核和统一提交后协调部署与新局/实体Tag联调；本端不擅自新建房间或迁移旧席位。


## 2026-09-26 COM7 普通候选部署与有界启动观察

主任务随后明确授权仅部署现有COM7并检查正常启动/真实发现，本节取代前文“109df未烧录”的设备状态。2026-09-26 01:08:00–01:09:05 EDT，使用本地 `batch32-2026-09-26-v2` 发布包、仅COM7一行清单、`--partial --execute` 和固定esptool5.3.1完成操作。工厂MAC `dc:b4:d9:02:d1:dc`、16MB Flash及已有分区布局均通过检查；同一串口上下文内写入、校验四段并复核MAC，工具记录VERIFIED。当前实际普通app为 `109df53f3941db281cc710a282fd5cbea700a70c73d6ef3316ab8350d55537b0`。没有erase-all、NVS/文件系统写入或配置迁移；otadata按发布包显式更新。

证据目录 `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/admission-com7-20260926-010725/`：`flash.jsonl`、`flash-console.log`、`player.log`/`player.jsonl`、`capture-events.jsonl`、前后及最终HTTP快照和 `verification.json`。预检和正式刷写各进入下载模式并hard-reset；随后采集DTR/RTS均false，没有额外主动reset。采集实际45.027秒，记录串口closed，命令exit0。初次准备脚本曾将发布包相对路径当工作目录路径而失败，当时未打开串口；修正为release解析后的resolvedPath校验后才执行设备步骤。

刷写前将历史已部署b53e的bootloader/partitions/boot_app0/app四段复制到证据目录 `rollback-b53e/`，逐段SHA重新校验，app严格等于 `b53e31f2bb8f1ae6befbc132a734904575862020ff2c3ed794922e5c3d8d7250`，与新候选分区布局一致；完整来源/偏移/哈希见 `rollback.json`。这是保留并复核历史回退产物，没有冒称本轮从设备读回旧app，也未实际回滚。

串口捕捉一次完整板卡/LVGL初始化及正常ready，构建标记Sep26 01:02:11、device `7aa041c7`。WiFi进度8条从ms5000到40097均status3/IP10.42.0.37，内部heap32924–32984、largest22516；窗口没有panic/abort/SelfTest或重复启动标记。服务器真实GET `/api/player-devices` 返回唯一在线eligible设备 `GP-DCB4D902D1DC` / deviceId2057322951 / capabilities3 / seatId0；旧GRID-P1离线。串口持续发现旧room993580107、没有paired记录，符合新ID尚未进入旧席位的边界。

部署前后及采集结束后的完整 `/api/state` 对照只变化旧屏连接、peer数、onlineMask、version12→13、identity revision7→8和服务器时钟。房间993580107、24格、现金/位置/资产/Tag/身份内容和待结算业务保持不变。本端没有新建游戏、改registry/分配/Tag、注入旋钮或游戏命令，也没有触碰COM6/COM8。

现场日志没有JoinStatus/page埋点，本轮也没有屏幕照片或目视反馈，所以 `WAITING FOR NEW GAME` 是依据已刷代码和实际发现状态的预期，不能标作真实页面目视通过。此前频闪/错位及Retarget max57ms限制仍保留；未跑新benchmark。主任务继续协调真实等待页文字/视觉确认、新局及多屏实体Tag流程；本端交回报告和源码所有权，所有构建/烧录/采集进程均已结束，COM7已释放。


## 2026-09-26 16:44–16:47 EDT 新局反馈只读审核与真实入席

用户反馈网页新游戏后仍显示WAITING FOR NEW GAME。主任务分工由服务器/Web定位新局未成功，本端审核已刷109df在真正新room建立后的认证入席与Tag接收，不刷机、不发游戏/Tag/新局动作。审阅9个生产文件与109df冻结清单逐SHA一致，记录于本地 `newgame-readonly-20260926-0218/source-audit.json`；目录名称的0218只是证据标签，实际时间以UTC时间戳和采集事件为准。

未发现本症状对应的玩家生产缺陷：拒绝后回Scanning持续接收广播；Pairing/Online时认证同server的新room立即清session/投影再PairRequest。新PairAccept按room/ack/session认证并派seat，合法设置期Identity（包括TagPairing stage7）解除等待页。服务器sendIdentity先发Tag；首个Tag可能在App尚无合法room/seat时被忽略，但Identity使transport Online/identitySetupActive，此后正常2秒自动Query，Tag parser允许同revision重新投递，因此不会因首次Tag先到永久卡在等待页。本次没有修改生产逻辑或重复泛测。

仅进行一次授权20秒COM7采集，实际UTC20:44:00.865–20:44:20.887（20.022秒），DTR/RTS均false且无复位命令。**这次采集存在实际扰动**：开头旧uptime55832273/status3/IP10.42.0.37及旧room发现后，约opened+21ms的读取批次出现ROM `rst:0x15 (USB_UART_CHIP_RESET)`，接着正常重新启动。不能称其为“无复位采集”；与串口打开相邻的USB-UART复位是明确日志证据，未通过再开端口重现实验来猜测具体驱动控制线过程。

随后20秒内多次event113/reason2，status6/IP0；本机固定ESP32S3/3.3.11头文件将reason2定义为AUTH_EXPIRE。HTTP一度显示新屏offline/eligible=false。串口按时closed后不再打开，未刷机或修改凭据/AP；UTC20:45:27Z仅HTTP复查已online=true/eligible=true/seat0。已同步主任务及服务器暂离线、USB复位和自行恢复，不能将短窗口离线留作当前状态。源码有30秒节流WiFi.reconnect路径，但本轮没有取得该分支的串口记录，不冒称实测走到特定分支。

**UTC20:46:42Z再次GET已经取得真实新room接纳证据**：`roomId=993580108/version=11`，`GP-DCB4D902D1DC/deviceId2057322951` 在线、capabilities3、seat1并列入frozenDevices。TagPairing已enabled，state=complete、revision7、session547832328、requiredHumanMask/boundMask/identityEditableMask均1；Identity revision12/phase1/avatarFinalMask15/nameFinalMask14/readyMask14/onlineMask15。来源为 `newroom-state.json` 和 `newroom-devices.json`。这证明109df在不重刷、不额外复位的情况下被真实新room认证入席；Tag已绑定完成是服务器状态，不冒称本端亲眼观察了实体拿放或屏幕具体页面。收尾脚本原先断言旧seat0，因实际已变seat1而中止，之后按新快照记录，没有写下旧状态结论。

完整本地证据目录 `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/newgame-readonly-20260926-0218/`，含player.log/jsonl、capture-events、分阶段HTTP快照、source-audit和review-result。当前屏幕仍109df、已加入新房间，COM7保持关闭，无构建/采集后台进程；本端没有触发新局或任何游戏、Tag、身份操作，未修改生产文件/索引。报告交主任务统一提交；后续具体页面反馈、姓名与开局以及单板现场流程由主任务继续协调，若有新玩家端缺陷再针对性处理。

## 2026-09-26 Avatar 焦点与手写输入修复（候选完成，待部署）

用户在109df报告头像旋钮偶发跳项、方向观感异常及延迟，手写H尚未写完就提交、断笔以及不出字。本轮先以真实生产代码做主机复现，没有重新打开COM7：生产reducer身份心跳例原2项失败；真实LVGL加生产renderer/handwriting例原3项失败，证明Tag心跳、草稿字符追加和断线提示会销毁同页画布。根因是setFocus只更新nav而不更新identity.focus，下一次身份快照恢复旧焦点；NameHandwriting同焦点状态revision走rebuild，画布删除回调清除墨迹并取消识别generation。这两条与服务器HTTP识字无关，识字实际完全本地执行。

修复同步identity焦点，并在同room/seat的手写页保留画布，仅更新姓名、状态、焦点及提示层。头像仍使用现有保留对象路径，配方/提交状态/新预览改变时才重新获取预览，焦点和心跳不再重复刷新头像。没有反转旋钮Gray解码或更改RGB核心、时钟、bounce buffer、帧票据。实际旋钮方向及显示延迟仍须候选上机后验证，主机结果不是现场延迟测量。

触摸改为LVGL控制器每次回调直接记录物理样本和抬笔，10ms读取周期，笔迹按16ms节流绘制；应用循环不再二次读取缓存状态。1.2秒空闲从真实最后接触/抬笔计时，锁内用新millis，带符号deadline防旧时间戳误触发。连续pressed跨140ms调度间隙保持连线；明确release、移出画布或控制器读失败才断开contact。控制器-1与合法0分开：错误最多保留UI按压100ms后释放，错误期间不伪造笔迹/不启动DONE识别，恢复有效样本后再继续。没有实机I2C错误证据，本项是错误边界修正。

DELETE优先清当前墨迹并取消旧worker结果，画布空时才删草稿末字。触摸和实体按钮的DONE共用pending请求，末字识别并追加后才回姓名审核；NUL保留墨迹，结束本次DONE并提示补写/清除，补写仍得到完整idle窗口。名字满、点数溢出均保留提示而不悄悄丢字。旧worker忙时不把新generation误标已尝试，仅真正入队才更新attemptedGeneration。画布删除/重建、新笔画及DELETE都使旧结果失效；worker输入是固定拷贝，LVGL对象仍只由其锁内所有者操作。

识别器原本拒绝少于4点的长笔画，现按几何拒绝点触、允许2/3点可见线段。完整三笔H另复现神经网络以margin0.715判B而模板判H；未改神经网络0.45门槛或模型权重。新增通用几何匹配门控：归一化模板双向均方距离总和不超过4像素平方，且次优至少差2倍，采用清晰模板；其余仍走神经网络及原低置信模板回退。独立A–Z笔迹312组宽度/横杠变体原284正确，现301正确，原正确样本0退步；12组H的6点稀疏与密集插值笔迹一致，R/K、Y/T保留。仍有11/312合成路径误判，这些计数不能作为真人手写准确率，也未重训或重新宣称EMNIST准确率。

本地证据根目录 `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/`：`input-session-before-20260926.log`（旧焦点2 FAIL），`native-input-runtime-20260926/before-ascii-v3.log`（旧画布3 FAIL，原源码镜像保留）；修复后 `input-session-after-20260926.log` 4/4、`input-runtime-final-v3-20260926.log` 40/40、`input-fix-setup.log` 83/83、`input-fix-rotary.log` 15/15。UI layout/glyph检查通过。新runtime使用真实LVGL、生产reducer/renderer/识别器与模型，只替代硬件任务/分配和网络图片缓存；覆盖140ms主循环及控制器调度间隙、抬笔、旧时间戳、millis溢出、H完整性、拒识恢复、DONE/DELETE、满名字、失联及保留头像更新。

独立只读审查另在Temp冻结生产源码，真实worker受控暂停后检查添笔、DELETE、页面重建和旧结果发布，13/13通过；集成复核发现的DONE失败后补写过早提交已修复，并加入最终40项。审查证据分别为 `C:/Users/kicof/AppData/Local/Temp/gridopoly-handwriting-generation-audit-20260926/` 和 `.../gridopoly-handwriting-integration-audit-20260926/`；后者原日志保留了修复前失败，不能误称旧日志全通过。

正常固件候选run为 `runs/production-20260926-171245-ebcd9d9f5f3046d4a44323b06a1cf129/`。首次全量构建捕获LVGL8输入定时器字段错误，已改为 `input->driver->read_timer`；失败日志、原port源及manifest保留在 `before-input-timer-fix/`，仅这一源受控更新，其余2126不变，详见 `timer-fix-change.json`。修正底本编译成功后发现同一poll的两次DELETE被bool请求合并，最小复现失败记录 `input-action-order-before-20260926.log`；最终ino在触摸队列前处理实体pending，并在每个触摸动作后service，保持原顺序。新增两次DELETE、DELETE→DONE、实体pending→触摸三例，最终 `input-runtime-final-v4-20260926.log` 为43/43。独立DONE恢复复验 `C:/Users/kicof/AppData/Local/Temp/gridopoly-handwriting-done-recheck-20260926/done-recheck.log` 为7/7，通过异步NUL、workerBusy、1199/1200ms及先追加后离页边界。

成功底本app `b04ff784e46e0e43485924d37207b566ca768b31d9c036daad26f64ca531eb3a` 的完整源码/库和四段固件保存在run的 `before-action-order/`，candidate及manifest重定向后可独立核验。最后仅ino受控增量，其余2126不变，记录 `action-order-change.json`。最终app SHA **`efbe3d08ca59cf73d395202105dc91a4e874cea53e05c8eac90a69e3162deea7`**，文件1,905,536B、program1,905,390B、RAM125,672B，HWCDC验证PASS。sourceDigest **`7ea4c62f984d30dbf983769845ec5320b2f11ea8de48221d0e2997b27a2c9c1e`**，构建基准Git仍 `f6cfc39b616294e5ce5b8647cf99ab030e708498`；最终逐SHA核验2127冻结源与当前源、四段二进制全部一致，核验时当前提交为 `18a75e2d343fcbaeae061d58a7369f57ce95fc55`，证据 `final-verification.json`。bootloader/分区表/boot_app0与109df相同，四段布局及旧分区兼容检查通过。

本轮首次run `production-20260926-171145-95e735ca77444bfaa20d7ac1c29ace77` 在新DONE问题被发现后于快照阶段取消，没有候选，不发布。当前设备仍109df，COM7关闭；本节不表示已经刷写或完成现场验收。主任务统一Git提交及batch32-v3发布包，随后协调仅COM7正常固件更新，保留room108、已绑Tag和确认身份。部署证据预备目录 `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/input-fix-com7-20260926/` 已保存并逐SHA核验109df回退四段；该备份来自既有发布产物，并非本轮设备读回。

## 2026-09-26 17:31–17:32 EDT 输入修复普通固件已部署

本节取代上一节“待部署”的设备状态。主任务独立核验后生成 `C:/Users/kicof/GridopolyReleases/batch32-2026-09-26-v3/release.json`，明确通知用户屏幕会重启、未确认的RAM草稿可能需要重写，并授权仅COM7正常更新。实际以Python3.12及进程级 `PYTHONPATH=C:/Users/kicof/AppData/Local/GridopolyReleaseTools/python-packages` 使用esptool5.3.1；dry-run无串口打开，随后MAC/Flash/真实旧分区预检及同一写入上下文内四段写验全部VERIFIED。当前实际app为 **efbe3d08ca59cf73d395202105dc91a4e874cea53e05c8eac90a69e3162deea7**。只写0x0/0x8000/0xe000/0x10000四段，未erase-all或写NVS/文件系统/设备配置，未触碰COM6/COM8。

仅一次45秒启动采集：UTC21:31:55.308895开串口至21:32:40.317408观察完成，工具记录实际45.000秒，21:32:40.321408关闭且isOpen=false/exit0。已知预检和正式写验各执行一次hard-reset；采集DTR/RTS初值false且未请求reset。窗口从板卡初始化开始，只有一次build标记（Sep26 17:29:22）及普通ready，未出现ROM rst、第二套启动、panic/Guru Meditation/abort。不能把“未捕获ROM rst”写成证明USB打开绝无复位效果，也没有为验证这一点重开串口。

固件记录WiFi GOT_IP事件ms1870、IP10.42.0.37，随后正常UDP认证 `paired seat=1 room=993580108`、Identity phase1/stage3及Tag快照。5–40秒的8次状态均status3/IP10.42.0.37，内部heap31504–31568、largest22516。采集后的真实设备列表为唯一 `GP-DCB4D902D1DC` / device2057322951 / capabilities3 / online=true / eligible=true / seat1。完整前后 `/api/state` 只差version16→18、identity.revision16→18和serverEpochMs；room、32格棋盘、全部玩家/资产/事件/Tag、冻结设备及已确认身份内容没有业务差异。avatarFinalMask15/nameFinalMask14保持，TagPairing complete/session1361533898/revision2及boundMask1保持。

本地部署证据目录 `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/input-fix-com7-20260926/` 包含仅COM7清单、四段109df回退产物、flash.jsonl、player.log/jsonl、capture-events.jsonl、HTTP前后全量快照、state-differences.json及verification.json。两个本地分区辅助调用最初误用函数名/将3072B产物传给要求4096B设备读回的接口，仅本地报错且未开串口；随后使用validate_images和parse_partitions正确核验了产物布局，实际设备分区由批量工具完整预检验证，未把填充数据当设备读取。

构建、写验及采集进程均已结束，COM7已释放。本端未发新局、Tag、身份确认或游戏动作。源码/host回归、真实普通启动联网及状态保持已验证；此窗口没有人工旋钮/笔迹视觉证据，实体顺时针手感、画面无断笔、H识别和DONE/DELETE操作仍交主任务与用户现场验收，不把此次启动观察宣称为已完成输入手感或显示性能验收。

## 2026-09-26 Avatar 行选择残余回跳：实机记录与旋钮恢复修正

用户随后确认手写已正常，但头像各行之间的选择框仍在慢转和快转时卡住或回跳，明确不是编辑字段数值。本轮保持手写、触摸、RGB设置及界面代码不变。主任务复核状态投影，独立真实LVGL审查共1046次采样未发现边框或实际flush像素回旧高亮；该结果只排除所测界面路径，不等于实机旋钮验收。

首个有限采集窗口因用户已从room108切换至109，而准备脚本仍以旧room作为ready门槛，18.047秒后退出并关闭，没有开启INPUT TRACE。保留 `avatar-focus-trace-20260926/` 失败证据；不把它归为设备故障，也没有恢复旧room。经主任务协调后只重试一次：`avatar-focus-trace-live-20260926/` 于UTC21:37:50.282647打开COM7，21:37:53.335498取得TRACE ON回执，90秒后OFF，21:39:23.316379关闭，isOpen=false/exit0。该窗口未出现重启或fault，没有发送游戏、Tag或身份操作。设备仍为普通固件efbe3d08；没有部署诊断固件。

捕获2049条raw、47个decoder步进及对应47次Avatar旋转消费，另有4次按钮事件。所有旋转时editing=0，配方不变；焦点始终按clamp(previous+delta,0..5)变化，没有事件间自行复位。诊断ring dropped=0、输入队列最大2条，输入至消费延迟中位3ms/P95 65ms/最大119ms，不能把全部体验解释成队列堆积。invalid从198升至446，共248次双相跳变；这些可能包含接点变化及短Gray相位漏采，日志不能证明机械故障。raw记录并非每次1ms轮询；两等相位00/11都有多次长停留，保留现有half-step机械尺度。

旧生产C++解码器按前一raw补齐缺失1ms采样，可复现47个实际输出中的46个，23处stable记录不一致，模拟invalid251与实采增量248不同；未知调度间隙不能被补点还原，所以不宣称完整波形逐点复现。实际431493–431539的12条局部记录则与旧解码器完全匹配：0→1→3在431498输出+1，随后3→0非法跳变，0→2→3在431539输出−1，回到同一物理停留位却把行选择撤回。

生产修正只涉及 `rotary_input_filter.h` 和 `rotary_input_tests.h`：保留最后由合法路径取得的detent；非法跳变不替换锚点，只有完整合法半周期到不同可信端点才输出。合法立即反转、正反方向、00/11两停留位尺度、1ms采样/去抖均不变。启动在01/10且尚无锚点时仍在首个合法detent同步；已有锚点在非法中间相位恢复时保留。零净回摆不会将非法终点升级为可信锚点。

初版锚点候选被主审发现上述“零净回摆”和“非法中间相位后半程恢复”两条反例，针对回归实际6 FAIL，修正后最终30/30 PASS，包含实采片段、相位/方向镜像、10倍慢时序、恢复后合法立即反转、4000合法转换及真实appHandleInput行焦点一致性。失败日志 `anchor-review-before.log` 保留，没有把初版通过25项写成最终充分验证。未重复无变化的手写43项测试。

最终同一整段raw回放为37步（+19/−18），相对旧模拟46步删除9步，没有新增步或改变剩余步的符号/时间；≤150ms反向仍有430061→430116与445838→445846两组。初版42步结果已被最终结果取代。另一个“非法后无条件丢弃下一半程”方案仅29步且会额外漏计，未采用。不能把实采47步当作12个物理刻度的错误计数，也不能把所有反向认定为错误；用户真实操作总次数尚无完整对齐。当前策略会保守丢弃方向不明的半程，仍可能漏计，需普通候选上机后验证卡顿和回跳是否改善。

完整证据目录为 `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/avatar-focus-trace-live-20260926/`：原始player.log/jsonl、events、前后HTTP快照、raw.csv、analysis.json、实际C++ replay/compare、compare-final.log及production-tests.log。正常构建使用原成功run受控增量，efbe完整2127源、四段和可独立核验的重定向candidate/sourceManifest已保存于run下 `before-rotary-anchor/`；初版两header及manifest另保存在 `rotary-anchor-review-v1-unbuilt/`，明确未构建、未部署。最终仅两header变化、其余2125源不变，见 `rotary-anchor-change.json`。本节编写时新构建进行中、COM7关闭；不能据此声明新版本已部署或用户已验收。

后续普通构建已完成：app SHA **`9c4e182530b4f1e5043bd1180c3658664c9bba089f2f526972a8009a9d1d4437`**，program1,905,434B、文件1,905,584B、RAM125,672B，HWCDC PASS。candidate仍位于run `production-20260926-171245-ebcd9d9f5f3046d4a44323b06a1cf129/candidate.json`，sourceDigest **`f8a69b41e0916c5b2fe658832453a0451c7064040e99fc745f0c68efe0224a45`**。再次逐SHA校验2127冻结源及当前源一致、四段一致，相对efbe恰好上述两header变化，证据 `candidate-verification.json`。bootloader、分区表及boot_app0均沿用efbe原SHA。当前只完成源码、回归和构建；候选交主任务统一Git/发布包与COM7部署协调，尚未写入设备，实机仍efbe。

## 2026-09-26 17:54–17:55 EDT 旋钮恢复修正已部署，现场手感待验收

本节取代上一节“尚未写入设备”的状态。主任务最终独立复核生产header 21/21 PASS，证据 `C:/Users/kicof/AppData/Local/Temp/gridopoly-rotary-production-final-audit-20260926/audit.log`，filter SHA b83cf35b与候选一致；两个header由主任务提交 **fd051e2**。主任务生成v4并完成单COM7 dry-run，发布manifest `C:/Users/kicof/GridopolyReleases/batch32-2026-09-26-v4/release.json` 的SHA为 `b852c45167777731475e17703f1100342c49f3cb33877d2df47909fc5ab574ef`，玩家端执行前再次验证该SHA和应用SHA。

沿用已有授权，仅对COM7/MAC dc:b4:d9:02:d1:dc更新普通应用 **9c4e182530b4f1e5043bd1180c3658664c9bba089f2f526972a8009a9d1d4437**。固定Python3.12/esptool5.3.1，批量工具真实MAC/16MB/旧分区预检通过，在正式写入同一上下文内四段写验VERIFIED。只写0x0/0x8000/0xe000/0x10000，未erase-all或写NVS/文件系统，COM6/COM8未触碰。前后快照均实时GET，不以109作为恢复目标，没有发送新局、Tag、身份或游戏动作。

单次启动观察于UTC21:54:20.705186打开，21:55:05.710480结束，实际45.000秒；21:55:05.715476关闭，isOpen=false/exit0。预检和写验各自有工具要求的hard-reset；采集DTR/RTS初始false、没有另发reset。窗口唯一build为Sep26 17:50:22，唯一普通ready，没有ROM rst、第二套启动或panic/abort。启动前约29秒出现反复event113/reason2（当前固定core定义AUTH_EXPIRE），30.459秒取得IP，随后正常认证 `paired seat=1 room=993580109`。35.034及40.037秒status3/IP10.42.0.37，内部heap分别21460/17044、largest7668；启动期间仍在装载头像素材。这是短窗口先连接失败后自行恢复，不是45秒始终联网，不据此修改网络或重复开串口。

实际HTTP前后room均993580109，version9→11、identity.revision10→12和serverEpochMs改变；除这三处之外完整业务diff为空。TagPairing complete/session2754481782/revision7保持，boundMask1保持，avatarFinalMask14/nameFinalMask14/readyMask14/onlineMask15保持。唯一设备GP-DCB4D902D1DC/device2057322951仍online=true/eligible=true/capabilities3/seat1。当前房间玩家1外貌尚未确认，不能把其它房间曾确认的外貌或RAM草稿冒称已在本房间保存。

完整证据目录 `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/rotary-anchor-com7-20260926/`，包含player-inventory.csv、flash.jsonl、串口原文/jsonl、capture-events、HTTP前后快照、state-differences.json和verification.json。`rollback-efbe/`的四段已逐SHA确认，来自前次部署产物而非本轮设备读回。准备阶段PowerShell曾拒绝保留设备名COM7.csv的目标拷贝，随后采用player-inventory.csv；该错误仅涉及本地清单准备，没有打开串口或写设备。

源码、30项生产旋钮/真实reducer回归、独立21项复核、普通构建、写验及正常入局均已完成，所有进程结束、COM7已释放。**旋钮慢转/快转卡顿与行选择回跳仍待用户现场复测**，候选保守漏计及日志不能区分的合法快速反转边界仍在；不把部署检查通过宣称为输入手感最终修复。主任务负责协调用户最小复测动作，玩家屏任务按新反馈继续定位，不额外后台录制或自动重刷。

## 2026-10-18 头像 V2 玩家端兼容候选

本节记录头像 V2 源码和主机验证。用户已停止此前旋钮排查，PCB/原理图由 PCB 设计会话负责；本轮没有继续旋钮、电量或硬件工作，也没有打开串口、构建 ESP32 固件、烧录、部署、修改共享协议或 Git 索引。此前记录是历史状态，不能视为本轮设备读回。

玩家端现支持 catalog 1/2：V1 保持原三层和染色规则；V2 使用 32 字节 GAVC 头、schema/encoding 2、五字节 RGBA+skinMask 的 RLE 与原样 CRC，按 neck→face→outfit→hair 合成。独立 neck/n1 只在 V2 存在，脸和颈部按基准 178 的中性灰 R 与显式 mask 进行整数染色，服装不染色，头发算法不变。所有预览维持 220×300，预加载分别为 30/31 个组件。

组件 HTTP、预览代次和最终头像缓存均区分版本；最终 URL 使用座位自身 recipe.catalogVersion，保留 V1 旧路径及 V2 的 avatars/v2 路径。切换版本时等待正在合成的旧快照结束，保留 LVGL 当前完整画面，拒绝迟到的旧组件/最终图，随后释放旧组件。释放后立即重入同样经过代次校验，避免释放工作线程仍引用的内存。缓存预算仍为组件 2,095,296 字节加两个 132,000 字节预览，单组件上限 128 KiB；四个 HTTP 下载临时缓冲及其它运行时堆开销不包含在这项已接收缓存预算中。

默认编译开关 `GRIDOPOLY_AVATAR_DEFAULT_CATALOG_VERSION` 保持 1，可显式设为 2；合法的 1/2 配方不受默认值改写。生产快照解析另修复了新玩家全零 recipe 的版本丢失：仅未设置配方继承 snapshot 的房间 catalog，已确认座位保留自己的版本。未修改共享 GridopolyProtocol，该部分归服务器任务。

主机证据目录为 `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/avatar-v2-20261018-candidate-check1/`。在两个默认版本构建下，实际 remote_avatar_cache.cpp 的解码、合成、内存计数、损坏/截断/跨版本文件、迟到下载、合成中切换和释放重入测试分别 **1,276 checks PASS**；生产快照解析→AppState→ConfirmAvatar→实际协议编码分别 **22 checks PASS**；既有 setup 回归 **137 checks / 0 failed**；双缓冲 ownership 回归 **22,768 checks PASS**。三个新执行日志保存于 cache/identity/setup 的 host-run.log，原所有权工具执行结果见本会话工具记录。完整固件仍需由主会话编译。

真实候选包复制前后逐 SHA 核对一致，快照位于上述目录 components-v2，清单为 components-v2-manifest.json；共 31 个文件、**1,362,537 字节**，加双预览共 1,626,537 字节。全部真实文件通过解码、CRC、预算及末尾 mask 字节破坏拒绝测试。H08/C01/F07/S08/O02（Ebony）、H01/C01/F10/S01/O07（Porcelain）均由生产客户端导出完整 RGB565；对应 CRC32 为 3f44c1c5、79b3a5f1。它们是候选快照的结果，不能套用于后续修改的素材包。

同时从 Git c09f0e25 冻结未修改的 V1 客户端合成器，读取真实 30 个旧组件重建同样两组配方；与新代码的 V1 输出各 132,000 字节完全相同，证据 legacy-v1-byte-comparison.json 和 legacy-v1/run.log，已将旧输出 CRC 固定为回归期望。

V2 跨端比较已完成。服务器任务使用相同冻结的 31 个文件，以服务端 C++ renderer 对照素材 Python 整数合成器，两组 RGBA/flatten RGB565 均零字节差异。本端独立重读其 real-art-output 原始 RGBA 与 RGB565，重新核对 31 项组件 SHA，仅将 alpha=0 位置转换为客户端 chroma 0x07E0，两组各 132,000 字节与玩家输出均完全一致；证据 independent-server-byte-comparison.json。两组客户端 SHA256 分别为 db280232c1afce91479d34078fa8f2c60a690a25ac2adafd2b1058c0c1ab49bb、9d5b7a6de532319c0ed2d4edc6cd110735ed609f2c4ddcae48c1984282bd673a。服务器原始证据位于 C:/Users/kicof/GridopolyReleases/server-avatar-v2-20261018-045441/real-art-comparison.json。本次仅是两组指定真实配方的精确跨端结果，不能扩大为所有组合已比对；全组合与美术审定由主会话负责。

保留失败准备记录：首轮严格主机编译发现窄化警告，已显式使用 byte 取整函数；首轮实际包测试采用窄字符中文路径参数而异常退出，改为 Windows 宽字符入口后通过，同时修复 runner 对负异常退出码的检测。identity 主机目标仍显示既有 app_state.cpp 的两处窄化、两处未使用参数警告，未将其写成零警告。源码完成、主机通过不代表实机画面或投产验收；主会话负责美术审定、冻结最终素材、完整固件构建、Git 和后续发布窗口。

## 2026-10-18 最终发型、RANDOM 与姓名页保留控件

最终 H06/H08/H10 素材已经重新冻结，31 个 GAVC 合计 **1,353,679 bytes**；相对上一节候选只变更三个发型文件。生产玩家缓存针对最终实际包的 172 项检查通过，两组指定配方与服务器/Python再次逐字节复核：H08/C01/F07/S08/O02 的 132,000 字节色键 RGB565 SHA256 为 `96cb7014b4708d1b9859dcb3a34e754456fff3506723c388248cf178958e953e`；H01/C01/F10/S01/O07 为 `9d5b7a6de532319c0ed2d4edc6cd110735ed609f2c4ddcae48c1984282bd673a`，差异均为零。完整 31 项 SHA 也一致。本端证据位于 `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/avatar-v2-final-hair-20261018/`；服务器最终三方记录为 `C:/Users/kicof/GridopolyReleases/server-avatar-v2-final-hair-20261018-check1/real-art-comparison.json`，没有用旧素材的比对结果代替最终包。

Avatar Setup 新增英文小按钮 RANDOM，位置 x132/y398、88×30，位于 CONFIRM 下方，避开头像裁切区域和底部电量显示。触摸和旋钮均可到达第七项；随机改变发型、发色、脸型、肤色、服装，保留 catalog、姓名、房间、席位和已确认配方，仅更新本地草稿与预览请求。每次按下保证五项组合不同于上一次，不自动发送 ConfirmAvatar；正在提交时禁用，离开头像页后拒绝迟到的随机触摸。两版 catalog 各 256 次随机检查、七项焦点边界、心跳保留焦点和显式确认一起纳入 setup 回归，**163 checks / 0 failed**，日志 `avatar-random-setup-20261018/host-run.log`。

姓名页问题在真实 LVGL 8.4 和生产 renderer 上复现。NameReview 三个选中焦点分别接受普通 revision、其他席位身份更新及全局素材失效，每种各 12 次：修复前均为 **12 次整页重建、12 次重绘失效、12 次选中按钮重新降至 40% 透明度**。不变状态本身没有触发重建；NameHandwriting 对应的后台更新本来就是零重建、零失效、零重复淡入。保留修复前 `42 UI checks, 12 failed` 的日志和源码快照，不将其误记为手写识别故障。

修复仅让同房间、同席位的 NameReview 沿用已有控件，按变化更新姓名文字及空值颜色、焦点、连接提示和 toast；切换页面、房间或席位仍重建。手写 canvas、采样和识别算法未改。修复后上述 15 组指标全部为零；加测空姓名、与占位文字相同的真实姓名、焦点样式、断线恢复、提示框出现/过期、换房间/席位、Edit Name 触摸进入手写，以及 RANDOM 布局/触摸/预览/禁用状态，最终 **74 UI checks / 0 failed**。

UI 证据目录 `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/avatar-random-name-ui-20261018/`。`before-fix-final.log` SHA256 为 `7387787113777a5b5f6679484d1a29ab328824ae4d46965ea995d0a0db933ab1`；`after-fix-final.log` 为 `b6948ae1f5d242f8a783b3277b4b8fdd503d3ed65139ad6f8e60c3a71985a58d`。三张截图来自实际 LVGL 帧缓冲，头像使用最终真实包的指定 RGB565；另有网络缓存桩，不能当作实机下载或显示测试。[RANDOM 布局（CONFIRM 选中）](../player-console/images/avatar-setup-random-2026-10-18.png)与[姓名页 BACK 选中](../player-console/images/name-review-retained-2026-10-18.png)由主会话复制留档，玩家端和主会话均已数字目视核对布局。

测试准备失败也保留：首次测试未启用电池 fixture，导致空 root 被几何检查访问而异常退出；修正只涉及测试初始化和非空断言。LVGL 性能统计标签自身的刷新失效在测量前排空，静止测试 fixture 清除启动 toast；这些测量修正没有改生产电池代码。追加样式断言时误用不存在的 `lv_color_eq`，改为颜色值比较后最终 74 项通过，编译错误日志保留。

完整 ESP32 首轮编译于 `processIdentitySnapshot` 的匿名聚合赋值发生 Xtensa GCC `gimple_add_tmp_var` 内部错误，未产生应用候选。已改成具名默认初始化对象后赋值，保留全部成员默认值。失败日志和 2,147 个原始输入完整保存于 `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/avatar-random-name-final-20261018/failed-baseline/`；最终增量构建沿用原绝对路径和配置，仅替换审核过的 app_state.cpp、app_types.h、ui_renderer.cpp、espnow_player_transport.cpp 四项，其余 2,143 项逐 SHA 不变。构建结果以下方最终交付为准。本轮不操作串口、不烧录，Git 索引与部署窗口由主会话负责。


最终增量的第一次尝试通过了原报错点，随后在 clearProjection 的同型匿名赋值再次触发相同 GCC ICE；exit1 与完整 sketch 快照保存在上述目录的 failed-incremental-1，其库输入引用 failed-baseline 并再次逐 SHA 验证。继续修正 begin/clearProjection 两处，使三处均使用具名默认初始化；仅 espnow_player_transport.cpp 的冻结 SHA 改为 4c38dcb7cbb80ecb961f12b17cd95cd3233e52ffbc562d34b732ed4b9d50ace8，另外 2,146 项输入保持。为解决真实编译失败而继续增量构建，没有重跑无关回归或改动 UI、电池与输入。

### 最终普通固件交付（未部署）

继续增量后完整 ESP32 构建 **exit0**，UTC 05:34:19～05:44:57；GCC 内部错误消除，HWCDC 链接和实际构建配置检查通过。程序存储占用 1,952,614/3,145,728 bytes（62%），静态动态内存 127,400/327,680 bytes（38%）；这些构建统计不等于实际运行堆余量或帧率。实际应用 binary 为 **1,952,768 bytes**，SHA256 **`99f6a8405ca9da1f2ad7169da60fa7b32d02a36269f8d1be9ab377b927fc7788`**。

本次候选是正常 Wi-Fi UDP、12 MHz、默认 catalog 1 且支持显式 V2，继续使用 viewe-md80et-v10 电池 profile；不是 SelfTest、ESP-NOW 或显示诊断固件。未修改时钟、输入驱动、电池实现或手写识别。2,147 个冻结输入的 SHA 全部符合清单，与构建后仓库来源也无漂移；已核验四个烧录段 offset 0/32768/57344/65536，并独立复制留存至 `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/avatar-random-name-final-20261018/frozen-candidate/`，入口为该目录的 `candidate.json`。

2,147 输入清单 `candidate-sources.json` SHA256 为 `6e719cc8337cf6b38f6e1957c56ef615de2aac9e40f09ebd5686b9c591cff0f4`。同级上层交付目录的 `player-source-manifest.json` 单列本轮 30 个源码/测试工具文件及快照，SHA256 为 `00f25f922323e5f7a98a6b4926e2e4f199312cc46def2b919565c4ce4927c5ed`；`firmware-verification.json`、`ui-verification.json` 和 `player-handoff.json` 记录四段 SHA、配置、日志、前后测量及未部署边界。前两次失败证据独立保留，最终成功没有覆盖它们。

至此本次开发范围的源码、163 项 setup、74 项真实 LVGL、最终实际包 172 项及两组零差异复核、完整普通固件候选均已交付。**本轮没有打开串口或烧录，设备版本未由本任务改变，也未重新读回。** Git 审核/提交及后续部署由主会话负责；部署后仍需在真实玩家屏验收 RANDOM、头像显示和姓名页选中按钮稳定性，主机测试不能替代这项实屏确认。
