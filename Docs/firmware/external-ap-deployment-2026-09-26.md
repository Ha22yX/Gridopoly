# 外置 AP 可选部署（2026-09-26）

本部署包是经 `eth0` 连接独立桥接 AP 的可选方案，树莓派仍运行权威服务器、HTTP 和 UDP。
**此 eth0 包只完成配置生成器、切换/回退代码和20项离线回归，未在现场部署，也未完成36台容量验收。现役网络已改为USB专用热点，不能把本文当成当前安装步骤。**
默认操作仅显示配置。本文后续命令以当时 `ap0` 板载热点为迁移起点；未适配现役 `gpusb0`、USB guard及设备事件规则，不能直接apply或rollback现役USB配置。总指挥负责选择后续方案、安排窗口和统一Git。

## 2026-10-15 当前状态与历史边界

用户选择的 ALFA AWUS036ACM（MT7612U）已到货并部署，现役为 `gpusb0` 独立2.4GHz热点、US／信道6／HT20。SSID `gridopoly`、原密码及 `10.42.0.1` 保持，`wlan0` 保留管理。22:07:58与22:18:49 UTC均有16格＋4屏、20台同时关联/注册证据；USB设备事件恢复及22:19 UTC整机软件重启自启动通过。用户关闭COM7后，重启后的121.18秒独立观察按16格＋指定3屏通过。见[USB部署与启动验收](usb-ap-deployment-2026-10-15.md)、[选型依据](portable-wifi-selection-2026-10-15.md)。

16格唯一ORDER环ready、完整0～15自动映射、四角0/4/8/12及16 reader stable已有现场证据；同尺寸重开局复测正常，首次等待页根因仍未确定，见[观察记录](newgame16-assignment-observation-2026-10-15.md)。32格闭环、36台与管理终端容量、无家庭网冷上电及四真人实体整局仍待验。20台关联不等于四人已配对入局。

本日11:21:45–11:25:03 UTC的[板载容量调查](ap-capacity-investigation-2026-10-15.md)属于迁移前：已接24V，四次均7台活跃关联（3屏＋4格），DHCP未耗尽、服务无拒绝；不推广为所有Pi/固件永久七台上限，也不再列为当前16格上线阻塞。09:39 UTC四屏发现与旧房间冻结三真人的记录仍按原时刻留在[设备台账](current-device-connections.md)。

旧板载脚本原已硬编码10/0、watchdog运行20/45/2。现役USB复制并保留这些参数；工作区原参数化/模板修改与主动ARP提案未随USB部署上线。旧eth0工具回归不能作为USB现场验收，USB维护也没有使用本包的自动回退机制。

## eth0方案与迁移前依据

本方案原目标：32 块格子模块加 4 台玩家屏，共 36 台无线客户端。16 格试玩需要 20 台，40 格完整目录需要 44 台；维护用手机/电脑另留容量。游戏地图支持范围不证明 AP 关联容量，选择与验收应按实际目标规模。
2026-09-26 04:17–04:19 UTC 只读检查：Pi 5 Model B Rev 1.1，brcmfmac，
`brcmfmac43455-sdio` / BCM4345/6，启动固件 `7.45.265 / 01-b677b91b`。
`ethtool` 的 driver-version 字段另为 `7.45.16.144`，不是启动日志的固件版本。
内核 `6.18.34+rpt-rpi-2712`；hostapd `2:2.10-24`。

该次历史检查中，`wlan0=10.0.0.124/24` 用于家庭网上联和 SSH，`ap0=10.42.0.1/24` 提供热点。
两个无线接口共用一个射频、信道 2 / HT20；iw 给出的接口组合仅允许一个信道。
当时实际有 2 台关联客户端。hostapd 没设置 `max_num_sta`，驱动未公开关联数上限。
DHCP 的 181 个地址不能证明无线固件支持 36 台；没有证据将该固件上限直接写成 8、16 或 32。
`eth0` 当时没有 carrier、没有 IP，因此当时环境不满足本脚本 apply 条件。

```text
家庭网络 ── Pi wlan0（管理 SSH，保持现状）
                   Pi 权威服务器
                     │ eth0：10.42.0.1/24
                     │ 独立网线或独立交换机
                 外置 AP（桥接模式，DHCP 关闭）
                     ├── 32 块格子：HTTP :80
                     └── 4 台玩家屏：UDP :4242
```

外置 AP 的有线网段必须独立于家庭 LAN，不能把新 DHCP 接到家庭交换机上。
这里没有配置 NAT、转发、互联网 DNS 或树莓派无线桥接。AP 本身管理 IP 可用
`10.42.0.2/24`，避开 `.1` 和 DHCP 池；依设备手册通过它的管理界面设置。
无线 SSID/密码需与现有设备匹配，使用设备支持的 2.4 GHz 及认证方式，允许客户端访问有线 Pi，
允许必要广播。AP 容量、频段、隔离功能和 DHCP 关闭必须由操作者核实；脚本不控制未知 AP。

## 兼容边界

| 项目 | 保持的约定 |
| --- | --- |
| 树莓派服务地址 | `10.42.0.1/24`，从 ap0 移到 eth0 |
| 格子 HTTP | `10.42.0.1:80`；当前默认 `GRIDOPOLY_TILE_HTTP_HOST` |
| 玩家 UDP | `10.42.0.1:4242`；当前 `serverAddress_` 固定地址且过滤其它源 |
| 服务器环境 | `HTTP_PORT=80`、`SERVICE_IP=10.42.0.1`、`UDP_BIND=0.0.0.0`、`UDP_BROADCAST=10.42.0.255`（均带 `GRIDOPOLY_` 前缀） |
| DHCP | `.20` 至 `.200`，181 个，12 小时租期，DNS/网关为 `.1` |
| 租约文件 | `/var/lib/misc/dnsmasq.leases`，新旧 DHCP 严格互斥，沿用地址记录 |
| 原部署 | 原 ap/env/dnsmasq/unit 文件保留，主服务器不重启、不改对局 |

格子固件 `wifi_ready` 还检查 `gatewayIP()[0] != 0`。即使棋盘只需要本地服务，也必须保留
`dhcp-option=3,10.42.0.1`，否则现有格子会停留在等 DHCP 的状态。
Pi 的新 NetworkManager 连接自身则设置 `never-default=true`，不接管 Pi 的管理默认路由。
给客户端下发网关不代表这里已经提供互联网转发。

与上文对应源码：[格子网络](../../Firmware/TileModule/src/tile_network.cpp)、
[玩家地址](../../Firmware/PlayerConsole/wifi_udp_player_transport.h)、
[服务器启动](../../Server/RaspberryPi/src/main.cpp)。
源码地址约定不等于已逐台核验在役固件。

## 文件与生成方式

实现位于 [external-ap 目录](../../Server/RaspberryPi/deploy/external-ap/)：

- `external_ap.py`：单文件 Python 3 标准库实现，包含三个配置模板、只读预检、试运行、提交、回退。
- `external-ap`：可选 POSIX shell 入口，与 Python 入口行为相同，没有自动提权或自动安装。
- [离线测试](../../tests/deploy/test_external_ap_config.py)：状态化模拟 NetworkManager/systemd，实际系统命令仅用于本地 shell 语法检查。

在仓库根目录运行，普通用户即可：

```sh
python3 Server/RaspberryPi/deploy/external-ap/external_ap.py
python3 Server/RaspberryPi/deploy/external-ap/external_ap.py --dry-run
python3 Server/RaspberryPi/deploy/external-ap/external_ap.py --generate /tmp/gridopoly-external-ap-review
```

前两条不运行外部命令、不写任何配置；第三条仅新建指定的审阅目录，已存在的目录会拒绝覆盖。
生成三个文件，目标分别是：

| 目标路径 | 用途 |
| --- | --- |
| `/etc/NetworkManager/system-connections/gridopoly-external-eth0.nmconnection` | eth0 静态 `.1/24`，无网关/DNS改写，试运行 autoconnect=false，权限 0600 |
| `/etc/gridopoly/external-ap-dnsmasq.conf` | 只在 eth0 服务，排除 lo，保留 DHCP 池和原租约路径 |
| `/etc/systemd/system/gridopoly-external-dnsmasq.service` | 独立 DHCP/DNS 服务，未依赖旧 AP/watchdog |

生成目录不是 apply 的输入。修改模板应改 `configs()`、重新生成和回归；不会把任意编辑过的
文件隐式安装到系统。接口参数只允许 `eth0`，不支持任意网段、桥接、VLAN 或通用迁移。

新增独立服务的原因：systemd drop-in 不能清空 `Requires=` 等依赖。
旧 `gridopoly-dnsmasq.service` 明确 Requires 旧 AP；用独立 unit 避免无效的空依赖覆盖。
原 `gridopoly.service` 的 After 排序不等于 Requires，本脚本仍会检查它是否新增了强依赖/拉起旧 AP 的 Wants。
参考 [systemd 官方 unit 文档源码](https://github.com/systemd/systemd/blob/main/man/systemd.unit.xml)。
NetworkManager keyfile 的路径、地址字段和权限依据
[官方 keyfile 文档](https://www.networkmanager.dev/docs/api/latest/nm-settings-keyfile.html)。

## 将来执行切换的操作顺序

以下是可执行操作说明，**本轮没有执行这些步骤**。需要先由总指挥安排维护窗口，部署代码审阅通过，
操作者确认外置 AP 已配置正确、可通过原 wlan0 管理 Pi。棋盘客户端会暂时断线。

1. 按 AP 手册设置桥接模式、管理地址、SSID/凭据、关闭 DHCP，检查可承载 36 台及维护余量。
   将 AP 的正确桥接口通过独立网线连接 Pi eth0，不连接家庭 LAN。避免另一个已启用的同名 SSID
   把设备提前吸引到未就绪的网络；按 AP 自身界面控制启用时机。
2. 从家庭网连接 Pi 的 wlan0 SSH 地址。现场审计地址为 `10.0.0.124`，正式操作前重新确认。
   不通过 ap0 或 eth0 管理入口执行切换。`SSH_CONNECTION` 必须通过 sudo 保留；不支持缺失该变量的控制台自动 apply。
3. 确认 eth0 有物理 carrier，但在 NetworkManager 中处于 disconnected，没有全局地址、活动 profile、桥主接口或路由。
   若插线触发自动 DHCP，应先确认这个接口确实只连外置 AP，再由操作者断开该 NM 连接；脚本不会替你清理已有网络。
   若无法证明管理链路独立，停止迁移并安排本地控制台协助。
4. 在 Pi 上普通用户生成并审阅配置，再执行只读预检：

```sh
sudo --preserve-env=SSH_CONNECTION python3 Server/RaspberryPi/deploy/external-ap/external_ap.py --check
```

预检检查 carrier、NM 状态、管理 SSH 的本地地址及返回路由、其它接口/路由与 `10.42.0.0/24` 的冲突、
三个旧 AP 服务均 active/enabled、主服务依赖、原 AP/server 环境、已有同名新文件/UUID，以及 HTTP 健康。
已有迁移状态目录也会拒绝再次 apply。预检失败不写系统；预检不能检测未知 AP 上所有设置或证明外部网络没有第二个 DHCP。

5. 确认独立网线、AP 桥接、DHCP 关闭后，显式启动 300 秒试运行：

```sh
sudo --preserve-env=SSH_CONNECTION python3 Server/RaspberryPi/deploy/external-ap/external_ap.py --apply --isolated-ap-ready
```

该命令先在 `/var/lib/gridopoly-external-ap/` 创建 0700 私有备份目录，保存原 env/conf/unit 与回退脚本、状态。
env 可能含凭据，备份文件为 0600，不打印内容、不提交 Git。然后先启动独立 systemd 回退计时器，再安装新配置、
运行 dnsmasq/systemd 静态解析、加载 NM profile、停止旧 watchdog/DHCP/AP。
确认旧 `.1` 已从 ap0 清除后才启用 eth0，再启动新 DHCP，检查独占地址、服务、HTTP、UDP/4242、DNS/53、DHCP/67。
不修改 wlan0，不重启 NetworkManager 或游戏服务器。

6. 在 300 秒内让真实客户端连接外置 AP，核对 DHCP 地址 `.20–.200`、网关 `.1`、访问
   `http://10.42.0.1/health` 和网页；核对实际格子心跳与玩家屏 UDP 会话。只看到本地 health 不能替代这一步。
   保留原 wlan0 SSH，验证通过后提交：

```sh
sudo --preserve-env=SSH_CONNECTION python3 Server/RaspberryPi/deploy/external-ap/external_ap.py --commit --ap-client-verified
```

提交先禁止旧三个 AP 服务开机启用，再启用新 DHCP、设置新 NM profile autoconnect=yes，原子记录 committed，停止计时器。
参数表示操作者已经做了真实客户端验证，不是脚本自行证明客户端通过。
提交后仍需安排重启验收和 36 台验收；本包不自动重启系统。

## 自动与手工回退

试运行报错时尝试立即回退；SSH/执行进程中断时，独立 systemd 计时器到期执行回退。
commit、apply、rollback 使用同一个 Linux 文件锁，避免计时器与人工提交并发写配置。
已 committed 的状态不会被迟到的到期回调撤销；重复执行已完成的 rollback 无操作。
systemd-run 的到期机制依据 [官方文档源码](https://github.com/systemd/systemd/blob/main/man/systemd-run.xml)。

随时可通过保留的 wlan0 SSH 手动恢复，即使已经 commit：

```sh
sudo python3 /var/lib/gridopoly-external-ap/external_ap.py --rollback
```

回退停止新 DHCP、断开 eth0、禁止新 profile 自动连接并删除它，移除本包三个新配置，重新加载 NM/unit，
确认 `.1` 未残留在其它接口后恢复旧 AP、DHCP、watchdog 的 enabled/active 状态。
原配置文件没有覆盖，不需要用备份还原它们；原租约文件继续使用。
若 eth0 无法释放 `.1`，回退会保留错误状态并拒绝重新创建冲突的 ap0 地址，操作者应在 wlan0 上处理，
不要盲目同时启动两个接口。备份/状态保留用于排查；重新迁移前先确认回退完成，再将整个私有状态目录移至有日期的备份位置。

只读排查命令：

```sh
ip -br address
nmcli connection show
systemctl status gridopoly-ap gridopoly-dnsmasq gridopoly-ap-watchdog gridopoly-external-dnsmasq
systemctl status gridopoly-external-rollback.timer gridopoly-external-rollback.service
journalctl -u gridopoly-external-dnsmasq -u gridopoly-external-rollback --no-pager -n 80
```

试运行的 NM profile 不自动连接，新 DHCP 不开机启用，旧服务仍开机启用，因此试运行阶段重启会走旧热点路径。
临时计时器不会跨重启保留；重启后要清理 pending 状态时可手动 rollback。
**commit 不是断电原子事务**：若持久化几个步骤之间断电，可能需要通过一直保留的 wlan0 SSH 手动回退。
提交顺序避免先让两个 `.1` 接口都开机启动。不能把回退脚本当作供电故障、上联失效或 AP 配错的可达性保证。

## 验证与尚未完成事项

本地运行：

```sh
python3 tests/deploy/test_external_ap_config.py
```

20 个离线用例覆盖配置/181地址池/网关兼容、默认不写系统、生成路径、接口/地址参数注入拒绝、
SSH入口与地址路由冲突、carrier/NM状态、已有配置和主服务依赖、试运行非持久化、定时回退、提交后手工回退、
逐个 apply/commit 命令边界故障恢复、旧 AP 清理失败、DHCP监听缺失和 eth0释放失败时避免重复IP。
POSIX wrapper 已通过本机 Git Bash `-n`。测试使用模拟主机，不连接 Pi、不执行网络写命令。
首次测试中的模拟文件系统缺少 `/var/lib`、把可重复 DHCP 选项当 env 唯一键解析这两个测试夹具问题已修复，再跑通过。

本 eth0 包尚未在真实 Linux 主机完成 NM 配置加载、systemd/dnsmasq 解析与超时恢复验证；apply 会在停止旧热点之前执行静态解析，
仍不能把模拟回归写成实机上线验收。完整交付验收由总指挥/服务器任务接续：

- 确认 AP 型号、实际关联容量、桥接/隔离/广播设置及管理地址。
- 现场试运行、真实 DHCP/HTTP/UDP、人工回退、超时回退、提交后的重启验证。
- 32 格子 + 4 玩家屏同时在线，随机上电、掉线/恢复及正常游戏与素材传输；记录真实设备版本。
- 保留旧热点回退能力，审查其它部署工具是否会重新启用旧 AP；不要未经适配重新运行原 install.sh 覆盖这套部署选择。

2026-09-26 原交付限定于新增 external-ap 目录、测试和本文；没有改写当时已有 dirty 的 AP 脚本/env/README、PCB、固件或 Pi 配置。2026-10-15 本次整合只改文档，原20用例与迁移前读数保留原日期，未重新测试或执行本包。当前USB网络的实施与证据另见其部署记录。
Git 审核、暂存和提交由总指挥负责。
