# 玩家屏四人配对、终局与整组清算进度

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
