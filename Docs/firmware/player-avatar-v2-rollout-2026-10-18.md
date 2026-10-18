# 2026-10-18 四台玩家屏头像与姓名页面升级

用户在 Avatar V2、RANDOM 和姓名页面修复完成后，明确要求烧录已连接电脑的四台玩家屏。主会话协调玩家任务独占串口，服务器任务准备配套 V2 组件与游戏服务；不继续用户已取消的 COM7 旋钮排查。

## 固定升级内容

- 新头像兼容：共用脖子、统一头部轮廓、正面服装、显式肤色遮罩及最终 H06/H08/H10 贴合修订。网页审美确认和数字像素验证见[素材与代码交付](avatar-v2-redesign-2026-10-18.md)。
- Avatar Setup 的 CONFIRM 下方增加 RANDOM，随机五项外貌，不自动确认。
- NameReview 保留已有按钮，只更新变化的内容，避免后台状态更新重复播放选中按钮淡入。未修改手写识别算法。
- 四台统一使用正常 Wi-Fi UDP、12 MHz、viewe-md80et-v10 电量配置。已有 V1 配方仍兼容；V2 由房间版本选择。

应用 SHA-256：`99f6a8405ca9da1f2ad7169da60fa7b32d02a36269f8d1be9ab377b927fc7788`，1,952,768 bytes。源码已提交 `51b6141d7689d55791b62f25ca827f5264d90cc8`。冻结 candidate 的 gitRevision 为构建时的前置提交 c09f0e25，实际输入由 2,147 项源码清单绑定，不以该前置提交代替来源清单。

冻结候选在本机 `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/avatar-random-name-final-20261018/frozen-candidate/`。candidate.json SHA-256 为 `3404b4c3ef110d06460420ce33fc4917a03388a42c818b16f46819973c89f2bf`；candidate-sources.json 为 `6e719cc8337cf6b38f6e1957c56ef615de2aac9e40f09ebd5686b9c591cff0f4`。

| offset | 文件 | SHA-256 |
| --- | --- | --- |
| 0x0 | PlayerConsole.ino.bootloader.bin | `31b3c1be45dc5a76aa85c82540d6787b675e711eaf11021a5eea7e36f469c6de` |
| 0x8000 | PlayerConsole.ino.partitions.bin | `ace02503447d0f470692e65fa76002f2d77a92dc81cd3813d8aa66718d716da9` |
| 0xe000 | boot_app0.bin | `f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0` |
| 0x10000 | PlayerConsole.ino.bin | `99f6a8405ca9da1f2ad7169da60fa7b32d02a36269f8d1be9ab377b927fc7788` |

## 现场前置核对

重新枚举四个 ESP32 USB 身份，并用固定 esptool 5.3.1 的保护工具逐台实读芯片 MAC、16 MB 容量和分区。COM7、COM27、COM28、COM29 均通过。采用同布局四段更新，保留 NVS 和素材分区；没有整片擦除。

升级前只读 HTTP：room 993580119、16 格、version 31、四真人、Tag 均已绑定；COM28/29/7/27 为席位 1/2/3/4，均 online/eligible。头像确认 mask 7，姓名确认 mask 0。16 个格子已组成唯一闭环，ORDER ready。该状态是升级前快照，不推定用户之后没有继续操作。

逐台烧录与启动证据在本机 `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/avatar-four-flash-20261018/`。每台分别保留 preflight、flash 及有限串口观察。启动/联网/电量结果和用户目视结果分开记录；日志正常不能代替实屏无闪烁、无错位。

## 部署结果

四台实际应用均为上述 `99f6a840…`，16 个分段全部写入并校验通过。每台随后进行一次约45秒的无命令被动采集，未追加复位；正常 BUILD/ready、Wi-Fi、原席位重连均有日志，无窗口内 panic、重复启动或串口错误，结束后全部关闭串口。12 MHz 依据经写验的冻结镜像及构建参数确认；普通启动日志不提供 PCLK 实测。

| 端口 / MAC | 四段写验结束 UTC | 本次启动电量采样 | 结果 |
| --- | --- | --- | --- |
| COM28 / dc:b4:d9:07:81:b0 | 06:14:37.974 | 9组完整，0组有效；regs=0066130095 | 正常启动/联网；随后用户断电恢复，见下文 |
| COM27 / dc:b4:d9:02:bf:88 | 06:16:09.193 | 9组完整，8组有效；14%，3668–3694mV | 启动后自动获得有效电量，首组未就绪 |
| COM29 / 20:6e:f1:ba:c9:04 | 06:17:23.226 | 9组完整，0组有效；regs=0055B50066 | 正常启动/联网；随后用户断电恢复，见下文 |
| COM7 / dc:b4:d9:02:d1:dc | 06:18:58.798 | 9组完整，0组有效；regs=1A55183766 | 正常启动/联网；随后用户断电恢复；不恢复旋钮排查 |

三台电量失败均为配置检查拒绝，Inspect 时间在当前启动中持续推进，不能把原始 0 当成真实 0% 电量，也不根据这些寄存器值推定变值原因。主会话在串口全部释放后请求用户将这三台 USB 和电池同时断开10秒再接回，用户回复“三台电量都恢复了”。随后重新核对USB身份，只进行一次30秒被动采集：COM7、COM28、COM29均6/6组完整有效，分别59%、87%、17%；没有发送命令或请求复位，窗口无重启/串口错误，全部端口关闭。证据为 `after-manual-power-cycle-*`。本次明确是**用户手动断电后恢复**，不沿用此前版本的自动恢复结论；本轮没有为此改固件、盲写通信参数或反复重刷。

主会话独立核对四份 write/verify、采集与电量记录，结果保存在 `main-audit.json`；2026-10-18 06:20:26 UTC只读 HTTP确认四屏 online/eligible、仍在原席位，16格完整闭环 ORDER ready。玩家任务的 `rollout-verification.json` 保留四台16段和全部证据文件SHA。

## 服务器配套与旧局边界

同次部署正常服务器 `00c978afdb9fa19923c2458754cb69fd6e13d7f9f9fb186555a331c826c6bf20`，实际运行PID2086790；V2共31组件、1,353,679 bytes，31个线上HTTP文件逐SHA相符。组件manifest为 `441d37f6a34db86a41c5d35e4b0cc78ac6bb7a9de75b39ee1ac8bf3b528ba96e`。实际进程新局目录配置为2，V1组件及已确认头像仍保留。

只重启游戏服务；AP/DHCP/watchdog的PID718/719/720及相关配置未改。部署前后原room993580119仍为catalog1，身份除在线标记和revision外一致，业务差异为空。没有创建新局、修改棋子绑定或迁移已确认头像。下一次**由用户创建的新游戏**才使用V2；仅烧录新玩家固件不会替换现有V1房间的素材。

服务器备份：`/var/lib/gridopoly-avatar-v2-deploy/gridopoly-avatar-v2-deploy-20261018-061933`。本机证据：`C:/Users/kicof/AppData/Local/GridopolyAvatarV2Deploy/deployment-20261018-061933`。配套发布详情与回退边界见[服务器报告](server-avatar-v2-progress-2026-10-18.md)；不把旧状态备份自动覆盖到玩家随后操作的数据上。

## 用户目视反馈

- 用户单独确认升级后的 **COM28画面正常**。
- 用户随后反馈 RANDOM 已出现，并明确 **“按钮闪动问题确实消失了”**；这次没有指定端口，不扩大成四台逐一验证。
- 用户同时看到外貌未变；当时服务器尚未完成V2部署且当前房间为V1，已解释房间版本及下一新局启用规则。
- 用户已确认COM7/28/29手动断电后电量恢复，并有上述新鲜采样支持。COM27首启动已自动取得有效电量。
- 四台逐一画面和新V2房间中的实体头像仍待后续现场反馈；不以数字素材检查或服务器发布通过代替。
