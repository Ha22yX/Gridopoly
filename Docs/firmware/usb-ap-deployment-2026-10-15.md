# ALFA AWUS036ACM USB 专用热点准备

更新：2026-10-15。用户已购买 USB-A 的 ALFA AWUS036ACM（MT7612U），尚未报告到货。本轮仅交付只读预检、候选配置生成和原件恢复方案；**未安装、未切网、未重启、未验证 USB 实机或 20/36 台容量**。

## 范围与现状

目标是 USB 独立 PHY 提供 2.4 GHz `gridopoly`，板载 wlan0 保留管理/可选上网。游戏网继续 `10.42.0.0/24`，Pi 为唯一 `10.42.0.1`，HTTP 80、UDP 4242、广播 `10.42.0.255`，DHCP 网关和 DNS 均为 `10.42.0.1`。外出游戏不应等待家庭 Wi-Fi；此项仍需到货后离线冷启动验收。

[生成器](../../Server/RaspberryPi/deploy/usb-ap/usb_ap.py)复用现场 AP 脚本的独立父接口分支、dnsmasq 和 watchdog。现有[以太网外置 AP 工具](external-ap-deployment-2026-09-26.md)仍只支持 eth0，不能用于 USB；其自动定时回退也不适用于本工具。**本工具没有 apply、install、自动回退定时器或在线 restore 命令**。

现有调查读取 Pi 内核 `6.18.34+rpt-rpi-2712`，未保存本机 `modinfo mt76x2u` 证据。本轮按主任务要求不额外采样。上游 Linux 驱动/厂家支持是选型依据，不能替代这台 Pi 的已安装模块、固件及 USB 识别；这些由到货预检读取。驱动 128 项 WCID 是结构依据，不是 128 客户端保证或 36 台验收，见[选型记录](portable-wifi-selection-2026-10-15.md)。

当前 Pi 游戏程序仍以既有部署记录为准：SHA256 `ea6e1f517c597cc69f6f0724af0d501206cf3d57e457945c9cc138fc3f2d118d`；本轮没有更新它。板载 AP 的实际 7 台关联及 10/0/20/45/2 配置来源见[容量调查](ap-capacity-investigation-2026-10-15.md)。工作区原有 AP 参数化修改保持原状，生成器不会把现场 10/0 悄然换成缺参数时的 15/1。

## 到货后的只读准备

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

监管域检查保守要求报告的非 world 国家码一致。若另一 PHY 有不同监管域，先由维护者核实，不能跳过检查。国家码不会赋予硬件不支持的信道；出行换国家后要重新检查并生成对应候选，不能宣称一次配置适用于任意地点。

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
| 两份完整 AP/game service | 移除对 network-online/wait-online 的直接依赖；AP 等待并绑定 USB device，游戏原执行命令/运行用户保留 |

systemd 依赖不能用空 drop-in 重置，所以必须审阅完整单元候选，而非只添加 `After=`。原件均备份，已有未支持 drop-in 会使准备拒绝。[systemd 官方说明](https://raw.githubusercontent.com/systemd/systemd/main/man/systemd.unit.xml)；永久地址匹配与 NM 单设备规则分别依据 [systemd.link](https://raw.githubusercontent.com/systemd/systemd/main/man/systemd.link.xml) 和 [NetworkManager.conf](https://networkmanager.dev/docs/api/latest/NetworkManager.conf.html)。

## 迁移与回退的审阅顺序（本轮不执行）

1. 主任务取得到货身份与当前现场结果，审核候选差异、私有备份完整性及独立管理/本地恢复通路。现场配置有变则重新生成，不能直接套旧包。准备好可执行的维护窗口恢复步骤；本工具没有自动回退保障。
2. 维护时先安装经审阅的 `.link` 和 NM 单设备配置，仅重读相应配置并重新插入 USB；核对 `gpusb0` 的永久 MAC、driver、独立 PHY，以及实际采用的 `.link`。不能全局触发 udev 重命名、重启 NetworkManager 或断开 wlan0 来代替核对。其它更早规则可能抢先匹配，生成成功不证明重命名已生效。
3. 停 watchdog、DHCP、旧 AP，确认旧 ap0 的 `10.42.0.1/24` 已消失，再安装其余候选并 daemon-reload、启动新 AP/DHCP/watchdog。任何时候都不得让两个接口同时拥有游戏地址。游戏服务仅更换单元依赖不要求立即重启进程；不得动存档或注入对局动作。
4. 检查实际 hostapd 国家码/信道/10/0、三处接口、唯一地址、非零 DHCP 网关、UDP 4242/HTTP 80 与真实客户端。初次维护现场还需审查运行后 systemd 依赖图；本轮只测试文本候选，不能证明没有其它系统服务间接等待家庭网络。
5. 若失败，保持 wlan0 管理，先停 USB watchdog/DHCP/AP。guard 会尝试撤销 USB `.1/24`；**再次读取地址确认其已消失后**才能恢复旧 ap0。身份不符时 guard 故意拒绝，不能盲删其它接口地址。
6. 校验私有包：将四个替换文件（ap.env、dnsmasq.conf、AP/game service）从 `original/` 按原字节、mode、uid/gid恢复；删除本次新建且清单标为原本不存在的四个文件（USB AP、guard、link、NM）。其余备份仅作参照，未经修改不必写回。若文件已被其它维护改动，先核对，不能覆盖。
7. daemon-reload、重读 NM 配置；仍连接且已确认身份的 USB 可显式交回 NM 管理，不连接任何 STA 配置。必要时仅重新插拔 USB 恢复普通命名。启动旧 AP、DHCP、watchdog，核对 ap0 唯一 `.1/24`、原 10/0 和健康接口；不重启/重建对局。恢复文件字节不等于在线恢复验收。

USB 拔除会停止绑定的 AP，DHCP/watchdog 随依赖停止。当前候选没有设计“任意热插拔自动恢复整组服务”；重新插入后的服务启动/恢复，以及带 USB 冷启动均留到货实测。30 秒 AP 启动作业超时也不能代替故障恢复验证。

## 验收与交付

离线回归：`python3 tests/deploy/test_usb_ap_config.py`，覆盖身份、缺硬件拒绝、地址/路由冲突、国家码/AP 能力、固件缺失、文件未改、双服务依赖、DHCP 契约、备份字节/权限元数据、候选损坏及窄范围 USB 地址清理。测试不访问 Pi，不将模拟清理或文件恢复映射写成真实网络回退成功。

本轮结果：USB **20 项通过**，原以太网方案 **20 项通过**；另以 Git HEAD 部署文件（包含旧 15/1 脚本）与本轮新文件组成私有临时副本，两组均再次通过。测试使用[无凭据的精确部署脚本 fixture](../../tests/deploy/fixtures/README.md)，不依赖原有 dirty 参数化修改；仅该 fixture 通过同目录 `.gitattributes` 保持 LF，并验证真实安装源被转换 CRLF 时拒绝。原 AP 静态契约及 4 个脚本语法检查用 Git Bash 通过。首轮 USB 有一处 Windows CRLF 行断言失败，已改为逐行比较；旧静态检查最初误选 Windows WSL bash 出现编码异常，切换既有 Git Bash 后通过，没有改旧脚本或测试。

到货由服务器任务预检、主任务审阅并协调迁移窗口，玩家/格子配合实际端到端验收：20 台全关联与注册，再到 36 台并留管理终端余量；断电重启/断连恢复、离线素材、HTTP/UDP、真实 ORDER 环、四真人配对与实体一局都需分别记录。只统计 `iw` 站点数或设置连接上限不算全部通过。

格子每次启动随机本地 Wi-Fi MAC，现有 181 地址池/12h 租约曾保留 63 条历史租约。重复整盘上电验收必须同时记录 DHCP 有效/保留租约与剩余地址，观察 32 板重复启动是否积累到池耗尽；不擅清租约或改客户端 MAC。本轮保持现有租约语义。

本轮准备工具及文档由主任务统一审核、提交；设备尚未部署。下一步需要 USB 到货并接入，随后由服务器任务执行只读预检、主任务协调迁移和实机验收。当前无需用户改线、重启或停用旧热点。
