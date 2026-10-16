# 格子建房素材 RAM 按需缓存：V0.37 候选

日期：2026-10-16（America/New_York）。本报告记录格子任务的源码、主机回归与普通候选交付；设备验证由主会话接续记录。本任务没有打开串口、烧录、修改线上建筑/资产/分配或提交共享 Git 索引。

## 候选交付阶段（随后实机进展见文末）

已部署基线是全部16块普通V0.36，方向/当前图文/低亮已获得用户确认，不能退回早期COM24单板待验的状态。V0.37为本轮新候选，长条与转角构建成功，4套43项主机测试通过；完整源输入、四段烧录文件与分区布局已独立核验。主会话接管COM20首板，再按其设备计划处理COM21/22/23；本报告的候选交付不替代后续实际写验、缓存操作和目视结果。

## 实现

服务端补充`assignment.buildingLevel`并纳入版本变化；格子真实HTTP解析器接受整数0～5，缺失兼容为0，拒绝负数、超过5、小数、字符串和bool。`TileAssignmentDto`投影到已有`TileState.building_level`，无主及非地产为0；现有页面差异比较会触发升/拆房刷新，默认图旁的等级徽标仍按权威状态绘制。

下载只选择当前地产的稳定`artworkKey`和等级1～5，不使用临时地图序号，不预抓其他等级。0级及非地产始终使用已有嵌入图。路径为`/assets/development/v1/tile/{artworkKey}-l{level}.rgb565`；两个共享头提供22地产白名单、160px CRC、重试/错峰策略。`tile_http_endpoint.h`统一资源与心跳的主机、端口及本地配置来源，没有新增独立硬编码服务地址。

单独FreeRTOS worker在core0运行，只有一个在途请求；主循环用零等待队列投递和消费，HTTP不在显示、读卡、ORDER、灯效或心跳调用栈中执行。连接/HTTP读取超时各1200ms；正文无进展1500ms或总计8000ms退出。每1024字节以内一批读入并让出执行，计算原始LE字节的增量CRC。只有HTTP200、精确Content-Length=51200、完整正文和目录CRC都通过才允许发布；未知/不符长度、压缩/分块、截断、坏CRC均回退嵌入图。

初始250～2000ms错峰，设备身份混合新鲜随机熵；失败从1s指数退避至30s，再加0～1000ms jitter，Retry-After作为下限（共享策略上限120s）。离线不发请求，重连重新错峰。重复心跳不会推迟已有下载计划；重分配、L0或断线不能清除已生效退避，因此503/429不会因不断换页面而立即重试。

缓存最多5张ready＋1张staging，**307200字节只指图片PSRAM payload**。8192字节worker栈、队列、HTTP/client对象及已有页面/提交缓冲另计。图片按需分配后保留复用，故图像`ram`同时也是已分配峰值；没有SPIFFS/NVS持久化，重启清空。内存不足时退避并继续嵌入图，不改用大块内部RAM。

缓存元数据、发布、淘汰及绘图lease归主显示线程；worker只写它独占的staging。完成结果先核对请求与当前选择代号，A→B、升级/降级、撤销及A→B→A期间的旧结果不能发布。绘图lease固定正在读的槽，LRU只淘汰未被引用的ready槽；若都在使用中，丢弃新结果并退避。复制完最后一行才释放lease，不把PSRAM直接交给长期SPI传输。

V0.36硬件rotation2、240×320、默认8MHz、背光150和V0.35低亮灯效保持。普通候选不包含COM8 stage2 RFID诊断；RFID/ORDER行为不在本轮重构范围内。

## 有界真实链路预览

`ART PROBE a1-rivet-row 1`可替换目录中任一地产key及1～5级，使用同一个生产worker、缓存和CRC验证，显示显著`TEST ARTWORK - 30 SEC`。`ART STATUS`给出请求、发布、命中、失败、ready/staging及图片内存；`ART STOP`立即恢复最新权威页面，未停止的预览在30秒期限到达后由主循环恢复。未分配模块的旧联网进度条不会覆盖测试图。

预览只覆盖本地素材选择，不写`TileNetworkSnapshot`、owner/buildingLevel、Tag、分配或服务器业务。重复命中显示`cached=YES`，请求计数不增；`result=0 http=200 bytes=51200 published=YES`表示完整校验后发布。当前权威L0条件下重启后ready/ram/requests为0；若重启后权威已为非零等级，则会正常错峰下载，不能把其后非零计数误判为持久缓存。

## 回归证据

最终`host-tests-final.log`：4套43项、0失败/0跳过。19项firmware boundaries、8项display diff保留原灯效和方向边界；新增11项development artwork直接编译生产缓存/流式下载例程，使用真实RGB565文件；network contract共5项，使用真实服务端JSON序列化和格子解析/投影。新增范围包括：

- L0、非地产、未知key绝不分配/请求；只下载当前级，单在途，完整校验后原子可见，逐字节LE内容与真实文件一致。
- 未知/短/长Content-Length、截断、CRC错误、编码错误、空闲及总超时、404/503/429；失败关闭连接且不发布半图。
- 重分配、降级/撤销、A→B→A、离线/重连及Retry-After跨选择保留；分配失败、millis回绕及模拟新对象启动为空。
- 五张ready加一张staging的上限、LRU与全槽pin保护；真实主机线程写staging时，显示线程保留lease并改变选择，旧结果取消且可见像素不变。
- probe白名单/等级/额外参数/停止/30秒期限；服务端等级0→1→2→3→4→5→0真实序列化，缺字段兼容和非法类型拒绝。

第一轮host构建因MSVC对标准`sscanf`的弃用警告在`/WX`下失败；按已有host目标惯例配置`_CRT_SECURE_NO_WARNINGS`后通过，首次日志保留。既有ArduinoJson C4180编译警告不影响测试，未伪称零警告。第一次双角色固件也构建成功，但未包含最后补充的未分配TEST页保护；已重新编译两个角色，以`firmware-build-final.log`和以下哈希为最终产物。

这些回归不证明ESP实际调度、运行堆峰值、Wi-Fi整盘吞吐、真实LCD视觉或建筑业务端到端验收；后续由主会话分别记录。

## 普通候选与复现

交付根目录：`C:/Users/kicof/AppData/Local/GridopolyTileTools/development-v037-20261016/`。

| profile | candidate.json相对路径 | 应用SHA256 | 大小 |
| --- | --- | --- | --- |
| tile-long | `build/tile_esp32s3_long/candidate.json` | `aef76554ce0bd71f945f41f5298cae62c463560aff70058e929dafe5b77d5a17` | 2724672 |
| tile-corner | `build/tile_esp32s3_corner/candidate.json` | `963f0b66960adf6095fa09698503f9967b9babcf28443d5531131e592a7ece69` | 2724672 |

两个candidate文件SHA分别为`570c2763ffeb6a2e3041a6a0a8f3d39d20265897c282f1eb111002e999eb2727`、`af03733170d0a657b70fe6ad576b8125571f1f5e7d465bd6ef0562885ca37728`。共同sourceDigest为`75a822d2c7fd9452e24f3500a5a9d657238a44d14581f7935b2058c431915218`，82个实际输入含两个共享头。四段哈希、长度、分区及保护区边界通过；normalFirmware=true、rfidDiagnostic=none，二进制无RFID-DIAG。旧V0.36两candidate及段哈希未变。

源基线明确为已验证的私有普通V0.36快照（sourceDigest `837dfecde4385ba5025e18145508c8dfa1c06ad97fa219e174d327e2305b9515`；59efe7a基础叠加已提交低亮与旋转），不是共享树的stage2 main。`v036-to-v037-overlay.patch`为14个变化输入的可读差异；`build-input-overlay.zip`保存逐字节变化输入，SHA `9f1b0b0b47677119bdb43cb4df1ae8dc2579d83bb71b08e8a820e8f800919daf`。ZIP不含本地配置/凭据；V036/V037输入映射只记录配置哈希。

`prepare-normal-source.py --base <V036-normal-source> --output <新目录>`先校验74个基线输入，再应用冻结ZIP，最后核验全部82个最终输入。已实际执行到`reproduction-check`，全部一致。`build-normal.ps1 -Source <新目录> -Output <新输出目录>`运行两个显式profile的`candidate`目标，使用独立英文路径。完整可直接复制命令见交付目录`REPRODUCE.md`；`build-environment.json`记录工具包元数据及固定库版本。重建源输入一致不等于跨时间/路径编译出的应用逐字节相同，新候选仍需重新校验。

`normal-source`和`reproduction-check`继承本机凭据，不能放进公共交付包。可交付ZIP、哈希映射、制备/构建脚本、差异、校验记录和日志。导出器已识别共享main中的stage2诊断并标normalFirmware=false，防止今后直接将诊断源码误标普通候选；本轮不迁移RFID实现。

主会话负责接续打包、COM20下载/复用/重启清空验证、三板后续更新、视觉验收和Git索引。格子任务的构建与测试交付已完成，设备状态以主会话随后记录为准。

## 主会话首板验证续记：COM20（16:13～16:16 EDT）

主会话独立核对82输入与全部四段后，将COM20（完整MAC `a4:cb:8f:c2:99:60`、long）更新为上述普通V0.37应用`aef76554…`。串口、烧录和用户交互均由主会话执行；格子任务随后只读复核已关闭的JSONL，并用素材文件重新计算六张下载图的长度与CRC，全部一致。

证据根目录：`C:/Users/kicof/AppData/Local/GridopolyArtworkRuntime/`。`com20-v037-before-probe.jsonl`显示联网及恢复A1/owner0/buildings0后仍`requests=0 ready=0 ram=0`，证明此真实L0场景未请求development图。LCD日志为V0.37、8MHz、rotation2、240×320。

`com20-v037-levels.jsonl`中A1各级走生产worker，五次均`result=0 http=200 bytes=51200 published=YES`：

| 素材 | 请求序号 | 实测CRC | 发布后ready | 图像RAM字节 |
| --- | --- | --- | --- | --- |
| A1 / 1房 | 1 | `B8AB30CF` | 1 | 51200 |
| A1 / 2房 | 2 | `36FC0B9B` | 2 | 102400 |
| A1 / 3房 | 3 | `288FDE5B` | 3 | 153600 |
| A1 / 4房 | 4 | `B4098CD9` | 4 | 204800 |
| A1 / 旅馆 | 5 | `9F22DAC8` | 5 | 256000 |
| A2 / 旅馆 | 6 | `B47B6ACE` | 5 | 307200 |

第5次后切回A1一级，日志为`cached=YES hits=1`，请求/发布计数保持5，没有重新下载；随后`ART STOP`记录恢复权威页面。**主会话明确转述用户已确认本轮图片完整、方向和颜色正常**，该目视证据来自用户反馈，不是CRC或串口推断。

第6张见`com20-v037-sixth-image.jsonl`：在已有5张ready基础上下载A2旅馆，完成后仍`ready=5`、图像RAM/峰值307200，失败/分配失败均0。此为5-ready＋1-staging图片payload上限的实测，不代表总堆、任务栈或HTTP内存峰值。该窗口还记录16:15:04.904开始预览、16:15:34.919自动`expired restore=AUTHORITATIVE`，主机接收时间相隔约30.015秒；不将它作为所有调度场景的时限保证。

补记期间主会话的`com20-v037-after-reboot.jsonl`也已完成：16:16:03～16:16:15可见新的V0.37启动、ORDER boot由`87B81223F385BA9D`变为`5A59DA590E4262DB`，`requests/published/hits/ready/ram/peak_ram`均清零，随后重新联网HTTP200。这提供真实重启清空缓存的证据。该12秒窗口结束时仍为`NO TILE`、ORDER刚接收一条有效上游，因此不能把清零验证写成整盘分配/恢复也已经验收。

四份窗口均以`closed/released`结束。此时COM21/22/23由主会话继续写验，格子任务没有读取其未完成结果或打开端口；其他三板实际部署、后续整盘恢复及建筑业务端到端结果，由主会话后续记录。本次仅追加报告，没有改源码、重构建或覆盖候选/复现包。

## 四板写验完成与整盘恢复续记（16:15～16:17 EDT）

主会话随后完成COM21/22/23四段写入及校验、watchdog启动和各12秒状态采集；格子任务只读核对`remaining-flash.jsonl`与三份`com21/22/23-v037-final-status.jsonl`。每板均为普通V0.37、8MHz、rotation2、HTTP200，当前0级或非地产场景`requests=0 ready=0 ram=0`，并恢复原ORDER分配：

| 串口 | 完整MAC | profile / 应用 | VERIFIED时间（EDT） | 恢复分配 |
| --- | --- | --- | --- | --- |
| COM21 | `a4:cb:8f:c2:98:bc` | long / `aef76554…` | 16:15:31.397 | CARD-CF-1 / map2 |
| COM22 | `a4:cb:8f:c2:99:48` | long / `aef76554…` | 16:15:55.575 | A2 / map3 |
| COM23 | `a4:cb:8f:c1:eb:3c` | corner / `963f0b66…` | 16:16:19.128 | CORNER-HOLD / map4 |

`com20-v037-final-hotel.jsonl`补齐重启后再次下载：A1旅馆显示`cached=NO`、新会话`request=1`，HTTP200、51200字节、CRC `9F22DAC8`、publishedYES、ready1/ram51200，证明旧图未跨重启持久保留。30秒后自动退出，并在16:17:04.591记录`page=TILE id=A1 revision=81`，随后权威状态仍owner0/buildings0。前节12秒窗口末的暂未分配是当时事实，此后恢复A1已获新证据。

最终只读`final-four-live.json`（state时间16:17:54.988 EDT）为room `993580116`、`grid-city-16-v1`，16块全部online/assigned，ORDER status=ready、唯一16节点closedRing。正好上述四个MAC为V0.37，其余12块仍V0.36；没有将本批四板更新描述成全盘V0.37。该结果关闭本次重启/分批写验后的分配与环恢复观察，不替代完整建筑业务对局或其他故障场景验收。

全部串口窗口已释放。COM20图片完整、方向和颜色已有用户确认；**COM21/22/23目视仍待主会话已发出的用户反馈**，不会用写验或HTTP状态代替。主会话负责后续目视、下一批接线/更新和Git；本次格子任务仅追加报告，未进行其他操作。

## 第二批四板升级与恢复（16:22～16:24 EDT）

主会话随后明确转述用户已确认第一批COM20～23四块正常，第一批目视待反馈项关闭。用户接入下一批四块并要求升级；主会话独占COM24/25/26/11，格子任务只读核对其完成日志，未重复打开串口或烧录。

沿用前述冻结V0.37普通候选、固定esptool 5.3.1及watchdog-reset，`second-inventory.csv`逐板记录完整MAC与显式板型；`second-flash.jsonl`四条verified均包含全部四段写入校验，应用哈希与本报告候选一致：

| 串口 | 完整MAC | profile / 应用 | VERIFIED时间（EDT） | 恢复分配 |
| --- | --- | --- | --- | --- |
| COM24 | `a4:cb:8f:c2:99:6c` | long / `aef76554…` | 16:22:46.401 | B1 / map5 |
| COM25 | `a4:cb:8f:c2:98:a0` | long / `aef76554…` | 16:23:10.356 | T-WEST / map6 |
| COM26 | `a4:cb:8f:c2:99:50` | long / `aef76554…` | 16:23:34.201 | B2 / map7 |
| COM11 | `a4:cb:8f:c6:25:e0` | corner / `963f0b66…` | 16:23:59.054 | CORNER-REST / map8 |

同一证据目录内`second-COM24/25/26/11-status.jsonl`分别为约12秒有界窗口，均显示V0.37、8MHz、rotation2、HTTP200、assignedYES及上述原ORDER格子。当前0级或非地产页面均`requests=0 ready=0 ram=0`，workerREADY；四口16:24:32.070全部closed/released。

`second-live.json`在16:24:20.179～16:24:28.435的连续5次只读样本均为16在线、16分配、ORDER ready，版本分布为8块V0.37＋8块V0.36；最终拓扑为唯一16节点closedRing。房间仍为993580116/version17，developmentAssets.admitted保持13、active0、rejected0，未因本批正常0级启动增加开发图片请求。

第二批写验、启动联网及原分配恢复已通过，**第二批页面、方向和亮度目视仍待用户反馈**；主会话已发出问题并继续接管后续批次。格子任务本轮只补本报告，候选、源码及Git索引均未改动，不将8板升级写成全16板已升级。
