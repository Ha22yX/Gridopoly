# 玩家屏建筑等级远程图片缓存（2026-10-16）

状态：源码、主机验证及两份普通固件构建/独立产物核验已完成，候选已冻结但未部署。本轮不烧录、不打开设备串口、不请求线上素材或修改现场房间。

## 范围与来源

用户要求图片存于RAM、重启清空、按需下载并错峰。本轮只修改 `Firmware/PlayerConsole` 和本文；共享目录、资源及服务器路由由对应会话维护，主会话统一Git。新增素材来源为提交 `10d2bb08` 下的 DevelopmentV1，22地产各有6级；玩家屏只新增L1～L5的远程获取，不嵌入这些像素。共享接口是 [DevelopmentArtworkCatalog.h](../../Firmware/libraries/GridopolyCore/src/gridopoly/assets/DevelopmentArtworkCatalog.h) 和 [RemoteArtworkPolicy.h](../../Firmware/libraries/GridopolyCore/src/gridopoly/assets/RemoteArtworkPolicy.h)。

- L1～L5：`http://<配置的Pi主机>/assets/development/v1/player/{key}-l{level}.rgb565`，128×128、RGB565 LE、精确32768字节。
- L0保持原 `/assets/tiles/{key}.rgb565` 和StreetV3图源；非地产不请求development。未知等级使用显式 `0xFF`，保留原默认图来源，不借用Demo建筑数推断等级。
- `GRIDOPOLY_ARTWORK_HOST` 可在本地配置或编译选项中设置，默认 `10.42.0.1`，允许主机名/IP及端口，不包含scheme；示例配置没有加入凭据。
- 新图使用共享v1 CRC；36张旧图按原有RGB565文件生成独立CRC表。固件只增加key/CRC元数据，不增加整套位图。旧图内容若更新，须同时重生成校验表。

## 请求、缓存与界面生命周期

缓存身份包含地产key、Artwork、等级和catalog版本。首页、地产详情、购买、移动、拍卖及卡面从当前权威资产/棋盘位置传递等级；完整快照缺失时，仅显式命名该资产的紧凑快照可提供等级。地图不兼容、无有效来源或非法等级保持未知。资产列表本身仍使用原布局；进入详情后才请求对应图片。

只保留当前可见图片和已确定的掷骰目标两个下载需求，不在分配阶段预取全部五级。等级变化、旅馆降级、卖空、新局、地图变化都会重新选择请求；新房间/地图使旧下载结果失效。同目标等级变化也更新预取。无关目标预取完成不触发当前页面整屏重建。

RAM像素预算仍为640KiB：20个32768字节槽，正在下载的暂存图片也占一个槽。一个低优先级core0图片worker处理网络和内存分配；UI线程只选择需求和取得/释放引用，不等待HTTP。LRU只能回收未引用且非当前需求的槽；全部槽被持有时保留现有加载表现，等待引用释放，不突破预算。图片不会写入NVS、SD或文件系统；重启后重新按需下载，已验证的不可变图片可在同次运行的新局间复用。

每个真实LVGL图片对象各持有一个引用。删除对象或创建对象失败时释放；释放前在LVGL线程使其decoder缓存失效，worker不能在引用仍存在时修改descriptor或pixels。下载内容完整且CRC通过后一次性发布；未就绪仍使用原加载环/默认图逻辑，所请求等级保留在请求和权威状态中。旧等级下载完成不能覆盖新等级。

## 网络与失败处理

设备MAC混合新鲜 `esp_random()` 作为随机种子，首次下载延迟250～2000ms。失败按共享策略从1秒指数退避到30秒，再加0～1000ms抖动；429/503遵守秒格式Retry-After，按共享契约最多120秒再加抖动。切换页面或等级不能绕过已有退避期限。HTTP-date不是本次服务器契约，非法值按普通退避处理。

请求要求200、明确32768字节Content-Length及无压缩/分块编码。响应体读取有独立3500ms总期限、分块累加CRC，校验LE传输原字节后才发布。损坏、短包、错误长度、慢传输、分配失败和限流均释放暂存区并保留加载表现，不阻塞UI/UDP/旋钮。

## 验证与构建

- 真实缓存代码的主机检查：34/34，覆盖110张新图和36张旧图CRC/路径、原子发布、重复图片引用、失效下载、20图引用压力、包含暂存区的640KiB峰值、坏包、超时、分配失败和429/503退避。
- 真实LVGL renderer/input：71/71，包含既有输入/手写/等待页回归及本次等级传递、降级/卖空、未知/紧凑快照来源、同帧焦点与等级变化、对象删除引用释放、新房间和离开图片页面。
- 电池UI既有回归：18/18；显示构建配置：18/18。没有启用电池遥测或改变其逻辑。
- 旧素材检查与36项CRC表核验通过；共享目录生成校验通过，覆盖22地产、110状态和330个文件CRC。
- 首次Arduino构建因嵌套共享头无法自动发现GridopolyCore库失败，后续已加入公共 `GridopolyCore.h` 入口；失败快照/日志保留，不记为通过。
- 其后vendor/pclk12两份构建暴露缓存变量 `randomSeed` 与Arduino内置同名函数冲突，均以编译失败结束；改为 `artworkRandomSeed`，并在主机Arduino替身中声明真实API。增强夹具先复现原代码失败，修正后34/34通过。原失败日志保留为 `development-artwork-build-vendor-final-20261016.log` / `development-artwork-build-pclk12-complete-20261016.log`，不能将文件名中的final误读为成功。

两份修复后的独立构建均以exit 0完成，HWCDC和构建参数检查通过；两者均为 `BatteryBoard=none`、`DisplayDiagnostic=none`、正常Wi-Fi production，SDK为v5.5.5。构建时Git基线为 `10d2bb084d14cfd9611ab3a0636ee22861c3e84a`；随后主会话的提交不追溯改写该基线，实际内容由逐文件快照标识。

| 配置 | 应用文件字节数 | 应用文件SHA-256 |
| --- | ---: | --- |
| vendor，默认16MHz | 1912320 | `81430a30d910f089b78e1e53da9438cda2f9427950df5778bf9ac726339b78f1` |
| pclk12，仅显式12MHz／COM29候选 | 1912464 | `79a53b01047533b9de3caef1078b03ffd82d686d76133dd380b855c3d37ee578` |

编译器报告的代码占用分别为1,912,170／1,912,314字节（60%），静态RAM均为125,208字节（38%）；静态RAM统计不等于运行期可用内存或性能验收。两份实际应用文件都装得进3MiB app0分区。

候选位置（均在本机ToolRoot的 `runs/` 内）：

- **vendor**：`production-20261016-162745-e4a83712820847479d91569d4250d752/candidate.json`；清单SHA-256 `c981ccee07dec5439691ee558590e74b6b31e789bdb109de3584d936da32e42b`；源码清单摘要 `a14a7302e46e87f6ea12c681f9963d6f092e3daf31c6a44f7ab2d98135f92dcc`；ELF及应用内嵌ELF SHA均为 `83cc065f123e288b88243518d79d9fb0b8fc8f7604531e1f00044ce045367068`。
- **pclk12**：`production-20261016-162755-c70e12064f4c431482f0d90d2cdd650d/candidate.json`；清单SHA-256 `4019e8f0421727fb14f5f1fd830be7ff95cfe5efddc23cbe149012f68a4dfea7`；源码清单摘要 `1ef8b4847189671cd7f1dfecc4847e28137f5c5e8acffd612a4bdc345558cbdc`；ELF及应用内嵌ELF SHA均为 `c7aceb3a99a0c52fd17927cfc159722bb8aaf21e51e972da45e0ef61089b845e`。

每份2141项冻结输入均重新逐文件核验，全部与当前来源文件一致；两份内容集合完全相同，时钟差异只来自构建选项。四个写入段在0x0000、0x8000、0xE000和0x10000，逐段SHA匹配候选清单；前三段与前次普通候选一致：

| 段 | SHA-256（两份一致） |
| --- | --- |
| 0x0000 bootloader | `31b3c1be45dc5a76aa85c82540d6787b675e711eaf11021a5eea7e36f469c6de` |
| 0x8000 partitions | `ace02503447d0f470692e65fa76002f2d77a92dc81cd3813d8aa66718d716da9` |
| 0xE000 boot_app0 | `f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0` |

独立解析分区表确认无重叠并位于16MiB Flash内：NVS 0x9000/0x5000，otadata 0xE000/0x2000，app0 0x10000/0x300000，app1 0x310000/0x300000，FFat 0x610000/0x9E0000，coredump 0xFF0000/0x10000。

候选无显示诊断标记，未编入电池UART对象；SDK Arduino运行核为core1，mode3配置保留。本次没有修改LVGL绑核、显示时钟默认、porch、mode3双缓冲、40行bounce或旋钮解码/分发逻辑。成功日志为 `development-artwork-build-vendor-fixed-20261016.log` 和 `development-artwork-build-pclk12-fixed-20261016.log`；独立核验明细在 `development-artwork-20261016/candidate-verification.json`。

源码/测试及本文共27个文件的精确交付清单与SHA保存于 `development-artwork-20261016/handoff-files.json`：11个已跟踪修改、15个新增实现/测试文件和本文。主会话按清单统一审核、暂存及提交；本会话未写入Git索引，未改共享库、服务器或PCB。

## 当前设备与待完成验收

本轮没有部署。最后已知设备仍为COM7 `22ca08e6…`、COM27/28 `dfc4b7e5…`（16MHz），COM29 `716ff623…`（12MHz正常游戏诊断，临时日志已结束）；旧普通12MHz `3da4c493…` 也没有因本次构建自动部署。最新连接/席位以[设备台账](current-device-connections.md)为准。

玩家候选仍需由主会话协调后续部署，再配合服务器验证真实建房、旅馆降级、卖空、新局及多设备下载。主机HTTP故障夹具不等于真实Pi网络或32格＋4屏压力验收。COM29历史12MHz仅静止等待页目视通过，不能继承为新图片/动态页面、精确旋钮或性能通过；持续≥24FPS、最大42ms及core1历史Retarget max57ms仍未关闭。电池遥测和实体完整对局保持独立待验。

本机证据位于 `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/development-artwork-20261016/` 及同ToolRoot下 `development-artwork-*-20261016.log`；独立构建快照位于 `runs/`。这些本机构建/测试产物不纳入Git。
