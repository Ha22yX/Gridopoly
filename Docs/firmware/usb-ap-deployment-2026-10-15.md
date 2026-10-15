# ALFA AWUS036ACM USB 专用热点部署

更新：2026-10-15。用户已将ALFA AWUS036ACM接入Pi，确认实际使用地美国、16格＋4玩家屏已供电，授权配置USB热点。现已部署并完成实际Pi软件重启自启动验证；此前20台同时关联/注册已有证据，重启后按用户实际开启的16格＋3屏完成连续两分钟检查。

## 最终状态与整机开机自启动

| 项目 | 最终结果 |
| --- | --- |
| 热点 | `gridopoly`，原密码不变，2.4GHz／US／信道6／HT20 |
| 网卡 | AWUS036ACM，`0e8d:7612`／`mt76x2u`，永久MAC `00:c0:ca:be:2a:df`；持久接口`gpusb0`，不依赖PHY枚举编号 |
| 访问 | 连棋盘热点后`http://10.42.0.1/`；家网管理地址本轮仍为`http://10.0.0.124/` |
| 开机服务 | AP、dnsmasq、watchdog、游戏服务器均active/enabled，网页由游戏进程提供；重启后NRestarts均0 |
| 地址与协议 | `.1/24`唯一属于gpusb0；HTTP80、UDP4242；DHCP `.20–.200`、网关/DNS `.1`，保留61条租约 |
| 联网容量 | 22:07:58及重启前22:18:49两个快照均16格＋4屏、20 station；最终持续观察16格＋3屏，COM7用户曾关机 |
| 自动排序 | 唯一16节点环ready；全部source=order、0～15唯一分配，四角0/4/8/12；16个reader stable |

22:19:18.912 UTC执行唯一一次已授权OS reboot。重启前bootId `ee8390e1…`、game PID61309；重启后`369caf40…`、game PID721。程序SHA仍`ea6e1f517c597cc69f6f0724af0d501206cf3d57e457945c9cc138fc3f2d118d`。22:20:12快照已恢复16计划格＋指定3屏及原房间993580115。四服务在该次启动的monotonic约4.55～4.58秒进入active，家庭网wait-online约11.73秒才结束；完整依赖图无network-online/wait-online依赖，游戏启动实际早于其结束。**这是整机软件重启验证，尚未做完全断电且家庭路由器不可用的冷上电试验。**

重启前后262个存档中260个逐字节相同，state.bin仅版本/CRC、identity.bin仅revision/CRC改变，CRC均有效。完整sync业务内容及格子分配一致，正常变化还包括serverEpoch与配对会话sessionId/revision。期间房间由早期993580113变为重启前993580115，本任务仅GET业务接口；以最新115为恢复基线，未恢复旧房间或手动重置对局。

主任务最终独立观察为22:21:14.260～22:23:15.442 UTC，13个样本跨121.18秒全部通过：16计划MAC、新鲜HTTP租约（最小9.119秒）、指定3屏、唯一环、四角映射、16个reader stable持续满足；room993580115/version16保持。无HTTP采集失败。该有界进程已退出。

最终9个部署文件哈希匹配候选，临时回退timer全部取消，无周期任务。`get_throttled=0x0`；本次启动日志仅在驱动初始化前有一次USB reset，至22:22:37未见之后重复reset或mt76固件错误，不把正常枚举复位写成电源故障。游戏和模块固件未更新。


## 首次迁移与实际20台证据

22:05 UTC迁移到USB热点后，22:06:27主任务独立HTTP快照确认计划16块全部在线、reader stable，形成唯一16节点闭环并自动分配地图0～15；全部source=order，四块corner对应0/4/8/12，未手动注入分配。

22:07:58.592 UTC服务器任务现场快照进一步记录16格＋4屏同时在线。主任务从私有原始归档独立复核：20个Wi-Fi station均associated/authenticated/authorized=yes，4个玩家device在线，16格剩余HTTP租约为11.410～14.955秒。此窗口证明20台同时接入和注册；不是36台验收或长期稳定性保证。用户随后明确COM7已关机、暂不便开启，最终持续观察按实际开启的16格＋3屏进行，不把关机计为故障。

随后仅对USB驱动接口`2-1:1.0`进行解绑/重绑恢复试验：gpusb0重新出现，但三服务未在30秒内自动启动。一次性维护程序于22:08:36自动恢复旧AP，四原件按字节和权限恢复、五新增文件移除，停止临时回退计时。game PID61309及wlan0管理连接保持；此失败单独保留，后续必须修复启动链并重新迁移验收，不能把首次20台样本当作最终部署成功。

本轮维护用了私有一次性执行脚本与600秒systemd回退计时，区别于仓库仍然只提供prepare/verify的工具；不是用户曾删除的周期任务。最终维护结束必须取消这次临时回退。

## 修复后的USB恢复与当前验证

v2只修改设备激活规则：按真实父驱动`DRIVERS=="mt76x2u"`和MAC匹配，`ACTION!="remove"`覆盖add后的move/change；其余八份候选字节不变。v1没有完整udev事件记录，因此不能断言某个driver属性缺失就是唯一根因。v2现场properties确认正确`.link`、gpusb0 alias及三服务的SYSTEMD_WANTS。

再次对同一USB驱动接口解绑/重绑：网卡恢复后约6.9秒，AP、DHCP、watchdog由设备事件自动启动，没有手动补`systemctl start`，持久命名与身份核验通过。随后跨60.38秒的四个样本均为16计划格＋指定3屏、19 station，game PID61309未变，`.1`仅在gpusb0、health正常、get_throttled=0x0。随后已完成上节记录的整机软件重启验收，二者分别留证。

v2准备及运行证据：本机`C:/Users/kicof/AppData/Local/GridopolyNetworkAudit/usb-arrival-20261015-215331/v2/`；初次失败证据留在父目录。主任务独立HTTP/计划MAC/闭环核对证据在`C:/Users/kicof/AppData/Local/GridopolyNetworkAudit/usb-main-20261015-215722/`。这些目录含配置原件或私有快照，不入Git。

## 范围与现状

目标是 USB 独立 PHY 提供 2.4 GHz `gridopoly`，板载 wlan0 保留管理/可选上网。游戏网继续 `10.42.0.0/24`，Pi 为唯一 `10.42.0.1`，HTTP 80、UDP 4242、广播 `10.42.0.255`，DHCP 网关和 DNS 均为 `10.42.0.1`。已消除等待家庭Wi-Fi的启动依赖，并完成联网条件下的整机软件重启；完全离线冷上电另验。

[生成器](../../Server/RaspberryPi/deploy/usb-ap/usb_ap.py)复用现场 AP 脚本的独立父接口分支、dnsmasq 和 watchdog。现有[以太网外置 AP 工具](external-ap-deployment-2026-09-26.md)仍只支持 eth0，不能用于 USB；其自动定时回退也不适用于本工具。**本工具没有 apply、install、自动回退定时器或在线 restore 命令**。

现场Pi内核为`6.18.34+rpt-rpi-2712`；USB ID `0e8d:7612`，永久MAC `00:c0:ca:be:2a:df`，驱动`mt76x2u`，独立`phy1`，初始名`wlan1`。实际驱动固件文件`mediatek/mt7662.bin`及`mediatek/mt7662_rom_patch.bin`存在，2.4GHz AP能力及1/6/11信道可用。驱动 128 项 WCID 是结构依据，不是 128 客户端保证或 36 台验收，见[选型记录](portable-wifi-selection-2026-10-15.md)。

当前 Pi 游戏程序仍以既有部署记录为准：SHA256 `ea6e1f517c597cc69f6f0724af0d501206cf3d57e457945c9cc138fc3f2d118d`；本轮没有更新它。板载 AP 的实际 7 台关联及 10/0/20/45/2 配置来源见[容量调查](ap-capacity-investigation-2026-10-15.md)。工作区原有 AP 参数化修改保持原状，生成器不会把现场 10/0 悄然换成缺参数时的 15/1。

## 迁移前只读准备契约（保留用于复查）

把整个 `usb-ap` 目录复制到 Pi 的私有工作目录。`guard.py.in` 必须与生成器相邻。默认执行只显示帮助，无网络操作。以下只读命令在 Pi 上运行；`ACTUAL_USB_MAC` 和 `ACTUAL_COUNTRY` 必须替换成真实永久 MAC 与使用地两位国家码，不从用户时区或旧 CN 配置猜测：

```sh
python3 usb_ap.py --inspect
sudo --preserve-env=SSH_CONNECTION python3 usb_ap.py --check \
  --mac ACTUAL_USB_MAC --country ACTUAL_COUNTRY --channel 6
sudo --preserve-env=SSH_CONNECTION python3 usb_ap.py --prepare /root/gridopoly-usb-review-NEW \
  --mac ACTUAL_USB_MAC --country ACTUAL_COUNTRY --channel 6
sudo python3 usb_ap.py --verify-bundle /root/gridopoly-usb-review-NEW
```

生成器不安装依赖、不加载模块、不改国家码、不扫描/连接 Wi-Fi。需要已有 Python 3、ip、iw、ethtool、nmcli、modinfo、systemctl。输出目录必须不存在，且父目录应在 Pi 本地私有磁盘，不放共享仓库。目录 0700、内部文件 0600；包含原 AP 密码及 server.env，不能提交 Git、上传普通发布包或粘贴完整配置到聊天。失败后可能有不完整目录；只有 `--verify-bundle` 通过的目录才可供审阅。

预检拒绝以下情况：

- 找不到唯一永久 MAC，实际 USB ID/驱动不是窄范围的 `0e8d:7612` / `mt76x2u`，当前 MAC 被随机化，接口已桥接/占用，或与 wlan0 共用 PHY。可另传 `--interface 当前名称` 交叉核对，绝不默认 wlan1。不同 USB revision 需另行审阅。
- 缺 AP 能力、所选 1/6/11 信道不可发射、国家码与已读取监管域不符、驱动固件文件缺失。此时拒绝而非自动修复系统。
- USB 已有全局地址/路由、`10.42.0.0/24` 与其它接口/路由冲突、旧 ap0 不再唯一持有 `.1/24`，或准备会话没有独立 wlan0 IPv4 SSH。
- 原服务不是 active/enabled、存在未经审阅的单元 drop-in、原脚本哈希不属已审阅版本、SSID/服务地址/运行 10/0 不符，或候选目标文件已存在。这里只支持当前已知起点，不通用合并未知部署。

监管域检查默认要求明确国家码一致。实际内核报告global 98（监管域交集）及phy0 99（驱动自定义、无明确国家），不是ISO国家码。已按[Linux reg.c](https://raw.githubusercontent.com/torvalds/linux/v6.18/net/wireless/reg.c)核实语义；用户确认美国后，使用显式`--review-derived-regdom --country US`生成本次候选，清单保留原始监管域和审阅标记。该选项仍拒绝不同明确国家、未知特殊码或目标信道no-IR/disabled；不执行`iw reg set`。频率解析已兼容现场`2437.0 MHz`而不接受非零小数或错误频率。国家码不会赋予硬件不支持的信道；出行换国家后要重新检查并生成对应候选，不能宣称一次配置适用于任意地点。

## 生成的候选与原件

私有包有 `original/`、`candidate/` 和 `manifest.json`。清单记录原件 SHA256、mode、uid/gid，以及新文件原本不存在；校验器复核字节并提供恢复映射，**不会写回系统**。哈希用于发现意外改动，不是对抗整个包被替换的签名。

| 候选目标 | 内容 |
| --- | --- |
| `/etc/gridopoly/ap.env` | 保留凭据/未知键；UPLINK=wlan0，AP_PARENT/AP_IFACE=gpusb0，明确 10/0 与 watchdog 20/45/2 |
| `/etc/gridopoly/dnsmasq.conf` | 接口统一改 gpusb0，保留 `.20–.200`、12h、非零网关/DNS；不清租约 |
| `/etc/systemd/network/10-gridopoly-usb.link` | 按永久 MAC 与 mt76x2u 命名 gpusb0；不是按插入顺序猜 wlan1 |
| `/etc/NetworkManager/conf.d/90-gridopoly-usb.conf` | 仅该 MAC 的设备不由 NM 管理；不覆盖旧 ap0 规则或 wlan0 配置 |
| `/usr/local/libexec/gridopoly-usb-ap` | 从现场已审阅 AP 脚本复制，仅替换固定国家码；旧脚本不覆盖 |
| `/usr/local/libexec/gridopoly-usb-ap-guard` | 启动前核对身份/地址；停止时仅撤销匹配 USB 上 `.1/24`，不删除物理网卡或操作 wlan0 |
| `/etc/udev/rules.d/90-gridopoly-usb.rules` | 只匹配实际MAC和mt76x2u，非remove生命周期保留激活属性，由systemd启动AP/DHCP/watchdog三服务 |
| 两份完整 AP/game service | 移除对 network-online/wait-online 的直接依赖；AP 等待并绑定 USB device，游戏原执行命令/运行用户保留 |

systemd 依赖不能用空 drop-in 重置，所以必须审阅完整单元候选，而非只添加 `After=`。原件均备份，已有未支持 drop-in 会使准备拒绝。[systemd 官方说明](https://raw.githubusercontent.com/systemd/systemd/main/man/systemd.unit.xml)；永久地址匹配与 NM 单设备规则分别依据 [systemd.link](https://raw.githubusercontent.com/systemd/systemd/main/man/systemd.link.xml) 和 [NetworkManager.conf](https://networkmanager.dev/docs/api/latest/NetworkManager.conf.html)。

## 迁移与回退的审阅顺序

1. 主任务取得到货身份与当前现场结果，审核候选差异、私有备份完整性及独立管理/本地恢复通路。现场配置有变则重新生成，不能直接套旧包。准备好可执行的维护窗口恢复步骤；本工具没有自动回退保障。
2. 维护时先安装经审阅的 `.link` 和 NM 单设备配置，仅重读相应配置，再对实际USB接口进行定向驱动重绑定，或由用户重新插入USB；核对 `gpusb0` 的永久 MAC、driver、独立 PHY，以及实际采用的 `.link`。不能全局触发 udev 重命名、重启 NetworkManager 或断开 wlan0 来代替核对。其它更早规则可能抢先匹配，生成成功不证明重命名已生效。
3. 停 watchdog、DHCP、旧 AP，确认旧 ap0 的 `10.42.0.1/24` 已消失，再安装其余候选并 daemon-reload、启动新 AP/DHCP/watchdog。任何时候都不得让两个接口同时拥有游戏地址。游戏服务仅更换单元依赖不要求立即重启进程；不得动存档或注入对局动作。
4. 检查实际 hostapd 国家码/信道/10/0、三处接口、唯一地址、非零 DHCP 网关、UDP 4242/HTTP 80 与真实客户端。生成文本候选阶段不能证明没有间接网络等待；本次实际部署后已检查完整依赖图及软件重启启动时间，结果见页首。以后更换现场配置仍需重新核对。
5. 若失败，保持 wlan0 管理，先停 USB watchdog/DHCP/AP。guard 会尝试撤销 USB `.1/24`；**再次读取地址确认其已消失后**才能恢复旧 ap0。身份不符时 guard 故意拒绝，不能盲删其它接口地址。
6. 校验私有包：将四个替换文件（ap.env、dnsmasq.conf、AP/game service）从 `original/` 按原字节、mode、uid/gid恢复；删除本次新建且清单标为原本不存在的五个文件（USB AP、guard、link、NM、udev）。其余备份仅作参照，未经修改不必写回。若文件已被其它维护改动，先核对，不能覆盖。
7. daemon-reload、重读 NM/udev 配置；仍连接且已确认身份的 USB 可显式交回 NM 管理，不连接任何 STA 配置。必要时仅重新插拔 USB 恢复普通命名。启动旧 AP、DHCP、watchdog，核对 ap0 唯一 `.1/24`、原 10/0 和健康接口；不重启/重建对局。恢复文件字节不等于在线恢复验收。

USB拔除会停止绑定的AP，DHCP/watchdog随依赖停止；本次新增按MAC＋driver匹配的udev规则，在设备再次出现时启动三服务。v2定向驱动重枚举与整机软件重启均已通过且分别留证；真实物理拔插及无家庭网冷上电仍待验。30秒AP启动作业超时不能代替恢复验收。

## 验收与交付

离线回归：`python3 tests/deploy/test_usb_ap_config.py`，覆盖身份、缺硬件拒绝、地址/路由冲突、国家码/AP 能力、固件缺失、文件未改、双服务依赖、DHCP 契约、备份字节/权限元数据、候选损坏及窄范围 USB 地址清理。测试不访问 Pi，不将模拟清理或文件恢复映射写成真实网络回退成功。

到货前准备阶段结果：USB **20 项通过**，原以太网方案 **20 项通过**；另以 Git HEAD 部署文件（包含旧 15/1 脚本）与本轮新文件组成私有临时副本，两组均再次通过。测试使用[无凭据的精确部署脚本 fixture](../../tests/deploy/fixtures/README.md)，不依赖原有 dirty 参数化修改；仅该 fixture 通过同目录 `.gitattributes` 保持 LF，并验证真实安装源被转换 CRLF 时拒绝。原 AP 静态契约及 4 个脚本语法检查用 Git Bash 通过。首轮 USB 有一处 Windows CRLF 行断言失败，已改为逐行比较；旧静态检查最初误选 Windows WSL bash 出现编码异常，切换既有 Git Bash 后通过，没有改旧脚本或测试。

本次到货修正后USB定向回归为**23项通过**，覆盖小数频率、显式派生监管域及目标网卡启动规则；实际20台全关联/注册已有22:07:58及22:18:49两次快照，最终按用户当前开启的19台持续观察。未来36台及管理终端余量仍需实测。16真实ORDER环和正确四角、HTTP/UDP注册、USB重枚举与软件重启已分别取得证据；断电冷上电、无家庭网素材完整性、四真人配对与实体一局仍独立待验。只统计 `iw` 站点数或设置连接上限不算全部通过。

格子每次启动随机本地 Wi-Fi MAC，现有 181 地址池/12h 租约曾保留 63 条历史租约。重复整盘上电验收必须同时记录 DHCP 有效/保留租约与剩余地址，观察 32 板重复启动是否积累到池耗尽；不擅清租约或改客户端 MAC。本轮保持现有租约语义。

工具与文档由主任务统一审核和提交；服务器任务负责实际Pi网络变更，主任务独立校验私有候选及HTTP设备清单。当前未变更任何玩家/格子固件或手动游戏分配。
