# 16 格整盘上电后的 AP 容量调查

更新时间：2026-10-15；本次现场只读窗口 11:21:45–11:25:03 UTC。树莓派日志原时区为 UTC+8，本文统一换算 UTC。

## 后续结果：USB热点已替代板载热点

22:07:58 UTC已实证ALFA网卡20台全部关联、认证、授权及16格＋4屏注册；后续修复设备自动恢复并完成Pi软件重启自启动。最终持续观察按用户开启的16格＋3屏进行。见[USB部署与验收](usb-ap-deployment-2026-10-15.md)。下文7台结论属于此前板载AP调查，不能作为当前网络状态。

## 结论与证据边界

用户报告 16 格使用 24V 供电后，4 块玩家屏在线时通常只有 3 格上线；关掉一块屏后可上 4 格。本次实际读取 `iw`，确认 **3 屏＋4 格＝7 台已关联、已认证、已授权客户端**，连续三个后续样本仍为同七台，接收字节均增长。HTTP 中四块格子全部在线，没有服务器拒绝或排队。结合用户的替换现象，当前瓶颈强烈指向板载无线的关联容量，而不是“游戏只允许三/四块格子”。

没有抓到未入网设备的空口 Association Response/status code，也没有读取出固件内部的 maxassoc 数值。不能把这次七台观测写成所有版本 Pi 5 的永久硬上限，或声称已捕获 `status=17`。当前标准固件与射频组合没有满足 20 客户端整盘要求的证据。

本轮没有重启 AP、切换固件、清除 station/租约、调整路由、刷写玩家或格子，也没有新建房间、修改 Tag/锚点或游戏。既有 AP 未提交改动保持原样，没有新增容量配置补丁；提高 hostapd 的人数数值没有针对本次瓶颈的依据。

## 本机分层结果

| 层级 | 实际读取 | 判断 |
| --- | --- | --- |
| 硬件/内核 | Pi 5 Model B Rev 1.1；`6.18.34+rpt-rpi-2712` | 与此前机器相同 |
| 无线驱动/固件 | brcmfmac，芯片 BCM4345/6，加载 brcmfmac43455-sdio；运行 `7.45.265` / `01-b677b91b` | ethtool 的 `7.45.16.144` 是另一版本字段，不能当作当前 WLAN 固件版本 |
| 无线接口 | `wlan0` 上联家庭网、`ap0` 提供 gridopoly，共享 phy0，信道 2 / HT20 | 该 STA+AP 接口组合要求同一信道；`iw` 的 `total <= 4` 是虚拟接口组合数，不是客户端数 |
| 关联 | 11:21:45 以及 11:22:58、11:23:03、11:23:08 均为 7 台 | 后三次每台 `associated/authenticated/authorized=yes`，RX 都增长，不能归为七个僵尸 station |
| hostapd | v2.10，包 `2:2.10-24`；真实运行配置无 `max_num_sta`、MAC 白/黑名单或控制 socket 设置 | 安装包示例说明未设置时默认 2007；示例文件的 255 不是本机生效值。没有配置为 7 的用户空间人数限制 |
| DHCP | dnsmasq 2.91；`.20`～`.200`，12 小时；租约文件 63 行 | 保留租约不是当前在线数；日志有正常 OFFER/ACK，没有地址耗尽证据。181 个候选地址不能证明无线可关联 181 台 |
| 玩家注册 | COM7、COM27、COM29 online；COM28 offline | COM29 仍为本局 seat0；当前 UDP peers=2 只统计已入席连接，不等于 AP 客户端数 |
| 格子注册 | 4 个模块 online，全部 V0.34 / long / reader stable | 未形成完整环；本窗口没有四角在线/16 节点闭环证据，stable 也不等于实体读卡验收 |
| HTTP 服务 | accepted 792、rejected 0、queued 0、active 6、timeouts 14 | timeout 是累计数；不能据此说无线之前从未异常，但本次没有服务容量拒绝 |
| 游戏 | room `993580113` / version 12，三真人＋一机器人 | 本轮只读；新观察不能覆盖早前 v11 的带时间记录 |
| Pi 供电状态 | `vcgencmd get_throttled` 为 `0x0` | 只说明 Pi 的该项读数，不证明整盘 24V、模块 5V 或线圈电源均已验收 |
| 备用有线链路 | eth0 carrier=0，NetworkManager 显示 unavailable | 当前没有独立有线管理/外置 AP 链路可用于网络切换 |

实际游戏程序仍为 PID 61309、SHA256 `ea6e1f517c597cc69f6f0724af0d501206cf3d57e457945c9cc138fc3f2d118d`。AP、DHCP、watchdog PID 为 819/820/821，四服务 active/running，`NRestarts=0`。源码模块注册容量为 64，见 [TileDebugAssignments.h](../../Server/RaspberryPi/src/TileDebugAssignments.h)；不以这个软件容量替代无线容量。

格子固件启动时使用临时本地管理 Wi-Fi MAC，而 HTTP moduleId/deviceId 来自固定 eFuse 身份。因此租约中多个历史 `a6:cb:…` 地址不等于这么多实体板，也不能直接用 station MAC 与 HTTP deviceId 字符串比对。对应实现见 [tile_network.cpp](../../Firmware/TileModule/src/tile_network.cpp)。频繁重启会积累 12 小时保留租约，但本窗口未发生地址池耗尽。

## 日志实际说明了什么

读取 11:00 UTC 起 hostapd 的 210 行日志：成功关联、WPA 握手和 inactivity 清站可见，没有明确 `AP full`、`too many stations`、关联拒绝状态码。COM28 在 11:12:56–11:12:58 被 inactivity 清除，随后 11:13:10 起有格子关联成功；旧格子临时 MAC 也陆续清除，新格子至 11:13:40 完成握手。该时序与用户关屏、重上电的叙述相符，但不凭日志反推每一次实体操作。

同期 watchdog 单元日志无清站条目。内核历史有 `GET STA INFO failed, -52`，这是查询失败记录，不是已证实的“人数已满”拒绝码。当前日志级别及工具没有暴露内部 maxassoc：`iw phy` 未报告最大 AP station 数，`iwpriv ap0` 返回 no private ioctls，系统没有 wl/nexutil，也没有 brcmfmac debugfs 目录。本轮没有为抓包添加 monitor 接口或改变驱动调试配置。

## 运行配置与既有未提交修改的来源

真实 `/run/gridopoly/hostapd.conf` 已是 `ap_max_inactivity=10`、`disassoc_low_ack=0`；watchdog 进程环境为 20/45/2 秒。这里必须区分**早前实际部署的硬编码配置**和**仓库尚未提交的参数化修改**，不能说“工作区未部署，所以本机尚未使用 10/0”。

| 项目 | 本次读取 |
| --- | --- |
| `/usr/local/libexec/gridopoly-ap` | 硬编码输出 10/0；SHA256 `0538095d81181708fd67f7bfb090205b0c0fd64915a1674e17b8359c09592930`；文件 mtime 为 2026-08-25 18:14:57 UTC，mtime 本身不是完整部署审计 |
| 工作区 `gridopoly-ap` | 已有修改将 inactivity/low-ACK 参数化并校验；缺参数保留 15/1 fallback；本次开始时 SHA256 `180d17d168b4935d1db03a8937c6f5475bbaa7d09bc5d9f85d721c36fab81842` |
| `/usr/local/libexec/gridopoly-ap-watchdog` | SHA256 `8ecd5bf2b3fb9949126d23f36a7967cbf693d6e5e40b4741b14dd610c60ccdf0`；mtime 为 2026-08-07 11:20:09 UTC；运行 env 覆盖 fallback 为 20/45/2 |
| 工作区 `gridopoly-ap-watchdog` | 已有差异仅注释，未增加主动 ARP；本次开始时 SHA256 `44842be339644010fc5bf0d05fd5bad24e2536d6dbc1b552cfa274cc8d350ed3` |
| `ap.env.example` / `test_gridopoly_ap_config.py` | 已有 10/0/20/45/2 模板和静态门禁；前者 dirty、后者原本 untracked，本轮均未覆盖或声称新实现 |

这些部署脚本最近一个已提交历史为 `64b68e7f`；未提交工作区修改不能由 Git 确认作者/完整历史。原差异与现场脚本已留在本机证据中。是否采用参数化方案、主动 ARP 或调整清站时序，应另行验证，它们不扩大芯片关联容量。

## 本机 minimal 固件是否可用

11:25:03 UTC 只读检查确认，两种文件均已由现有 `firmware-brcm80211` 包 `1:20250410-2+rpt1` 安装；`update-alternatives --query cyfmac43455-sdio.bin` 为 auto，选 standard（优先级 50），minimal 为可选项（优先级 10）。Pi 5 的专用 `.bin` 链接最终指向这个 alternatives 入口，当前加载的是 standard。

| 文件 | 文件内版本 / FWID | 大小与 SHA256 |
| --- | --- | --- |
| `cyfmac43455-sdio-standard.bin` | `7.45.265` / `01-b677b91b`，2023-08-29 | 609309 B；`d608f866582519c0a28d86db43040f4f1b98dd1d153e72e9752586546b4a36c3` |
| `cyfmac43455-sdio-minimal.bin` | `7.45.241` / `01-5209c09`，2021-11-01 | 548728 B；`3075cb0bdc4b28ed4f08e01b1a216d0ebc70f4022d9d3272a4a43b3c90456e60` |

RPi-Distro 的[当前 trixie 原始 README](https://raw.githubusercontent.com/RPi-Distro/firmware-nonfree/trixie/debian/added-firmware/cypress/README.txt)说明 minimal 为提高 AP 客户端数而裁减功能，同时仍支持 STA，**预期支持 19 个 AP 客户端**；移除了高级漫游、DFS radar、ACS 相关功能和天线分集。这里的 19 是上游预期，不是这台 Pi 实测成功值，也不是已读取的硬上限。官方打包的 [43455 standard/minimal alternatives 变更](https://github.com/RPi-Distro/firmware-nonfree/commit/35844a4)包含 Pi 5 链接；[官方内核仓库讨论](https://github.com/raspberrypi/linux/issues/3010)亦记录同类容量限制及 Pi 4/5 使用同类无线芯片，不能替代本机验证。

因此不能以“本机有 minimal 文件”承诺完整 16 格＋4 屏＝20 台，更不能承诺 32 格＋4 屏＝36 台。若用户另行选择暂时降低范围，可将 16 格＋不超过 3 屏作为待实测实验目标，仍要给其它关联设备留余量；没有证据保证停用 STA 后会多出第 20 个名额。当前用户尚未授权降低完整需求或进行固件切换。

切换 alternatives 只改下次加载选择；使新固件运行还需要重新加载无线芯片或重启。wlan0 管理 SSH 和 ap0 热点会一并中断，不能在没有独立恢复通路时把它当作几秒不中断的配置修改。本轮只查询，不下载、覆盖、切换、卸载驱动或重启，也没有安排未获授权的网络断联窗口。

## 现有家庭路由器能否直接复用

单纯把 Pi 网线插进家庭 LAN，不能让当前设备自动改用路由器 Wi-Fi：设备仍寻找编译时的 gridopoly SSID，且固件固定服务地址。

| 组件 | 当前约束 |
| --- | --- |
| 玩家 UDP | [wifi_udp_player_transport.h](../../Firmware/PlayerConsole/wifi_udp_player_transport.h) 固定服务器 `10.42.0.1`；[cpp](../../Firmware/PlayerConsole/wifi_udp_player_transport.cpp) 在认证前检查来源 IP 与 4242 端口，发送也使用固定地址 |
| 玩家素材 | [remote_tile_cache.cpp](../../Firmware/PlayerConsole/remote_tile_cache.cpp) 与 [remote_avatar_cache.cpp](../../Firmware/PlayerConsole/remote_avatar_cache.cpp) 固定 `http://10.42.0.1/`，只改 UDP 地址不够 |
| 格子 | [tile_network.cpp](../../Firmware/TileModule/src/tile_network.cpp) 的 SSID/HTTP_HOST 是构建配置，默认 HTTP `10.42.0.1:80`；当前没有已验证的运行时配网迁移入口 |
| Pi | [main.cpp](../../Server/RaspberryPi/src/main.cpp) 默认 `SERVICE_IP=10.42.0.1`、UDP_BIND `0.0.0.0`、UDP_BROADCAST `10.42.0.255`；服务端主动每秒广播 Discover |

可审阅的路径有三种，均未执行：

1. **保留设备固件与独立游戏网段。** 使用经验证可承载至少 20 台并留余量的 AP/第二无线接口；保持 SSID、凭据、`10.42.0.1` 与广播网段。既有[外置 AP 工具](external-ap-deployment-2026-09-26.md)适用于 Pi eth0 接独立桥接 AP，带 300 秒试运行回退机制；不能直接把其 DHCP 接入家庭主 LAN。当前无空闲 AP 且 eth0 无 carrier，因此尚不具备切换条件。
2. **复用现有路由器的独立网络功能。** 只有型号确认支持额外 2.4 GHz SSID、独立子网/VLAN、有线端口或 tagged VLAN 接入、正确 DHCP/广播及客户端互通时，才可能在保留家庭网络的同时提供独立游戏网。普通 guest SSID、只改名字或 Pi 加一个地址别名不足以证明可用；需先读型号/功能，规划 `10.42.0.1` 地址归属与单一 DHCP，再审阅迁移步骤。
3. **将所有游戏设备迁移到现有家庭 LAN。** 需要同步更改 16 格与 4 屏的 Wi-Fi 配置和服务/素材地址，给 Pi 固定可达地址，并调整服务发现广播；保持设备身份和原对局，逐台构建/部署/恢复验收。家庭主 SSID和网段无需为此改名，但这是跨端迁移，工作量与断联窗口取决于设备连接条件，当前未授权执行。

稳定整盘架构应让经过目标客户端数验收的接入设备承担 Wi-Fi，Pi 继续权威规则、持久化、HTTP 和 UDP。暂时继续板载标准固件可做小规模联调，不能标为完整整盘连接问题已解决。minimal 是潜在受限实验，不是已经完成的 20/36 台方案。

## 交付与后续负责方

本轮只新增本文；未修改部署脚本、tests、客户端固件、Git 索引或现场配置。所有 SSH/采样已结束；不留下后台轮询。原始日志、地址/MAC 清单、HTTP 快照、脚本内容和固件哈希保存在本机私有目录 `C:/Users/kicof/AppData/Local/GridopolyNetworkAudit/ap-capacity-20261015-112144/`，不将原始现场数据打入公开发布包。

主会话负责取得家庭路由器型号/布线条件，与用户选定完整网络方案或明确受限实验范围，再协调网络维护与 COM29 正常固件窗口。服务器负责选定方案的预检、配置审阅、恢复路径和服务验收；玩家/格子负责必要客户端迁移与实物在线验证。验收至少分开核对：全部 Wi-Fi 关联 → DHCP → HTTP/UDP 注册 → 16 节点真实 ORDER 环 → 四真人配对与实体游戏。

## 后续选型决定

用户后续明确希望携带外出独立游玩，已购买ALFA AWUS036ACM（MT7612U），不再等待家庭路由器型号作为唯一下一步。服务器准备USB专用热点预检、配置与回退，当前未报告到货、未迁移，20/36台仍待实测。详见[便携USB选型](portable-wifi-selection-2026-10-15.md)。
