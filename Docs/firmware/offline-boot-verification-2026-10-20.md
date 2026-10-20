# 无家庭 Wi-Fi 启动核验 — 2026-10-20

结论：现役 Pi 的全部五个 Gridopoly 服务均已启用开机启动；经一次家庭网络不可达条件下的整机软件重启，游戏、模块热点、管理热点及 DHCP 正常自行启动。家庭 Wi-Fi 是可选上行，不是游戏启动条件。

## 服务与使用方法

| 服务 | 职责 | 自启 / 当前运行 | 异常恢复策略 |
| --- | --- | --- | --- |
| `gridopoly` | 游戏规则、存档、机器人、HTTP管理页/图片、玩家UDP | enabled / active | always，2秒后重启 |
| `gridopoly-ap` | ALFA USB上的 `gridopoly` 模块热点 | enabled / active | on-failure，3秒后重启；设备事件恢复沿用现役USB部署 |
| `gridopoly-dnsmasq` | 模块热点DHCP及本地DNS | enabled / active | on-failure，2秒后重启 |
| `gridopoly-ap-watchdog` | 模块无线连接恢复辅助 | enabled / active | always，2秒后重启 |
| `gridopoly-admin-ap` | 板载 `gridopoly-admin` 热点及独立DHCP/DNS | enabled / active | always，5秒后重启；内部监督hostapd/dnsmasq |

NetworkManager、SSH同样enabled/active。家庭连接的autoconnect为yes；游戏服务无 `network-online.target` 依赖。模块使用ALFA的 `gridopoly` / `10.42.0.1`；用户连接 `gridopoly-admin` 后浏览器访问 **http://10.0.0.1**，提示“无互联网”时保持连接。外出无需家庭路由器或互联网，USB网卡应保持插入。家庭网地址 `10.0.0.124` 不作为外出管理入口。

## 实测方法与结果

为避免把“服务已设置自启”当成离线启动证据，本次保留原家庭连接配置备份，临时将该连接目标改为不存在的测试SSID，保持autoconnect=yes，再软件重启Pi。由独立、有界的一次性恢复机制在启动后150秒恢复原配置，以免测试使远程入口长期失联。没有修改真实游戏、Tag、资产或模块分配。

- 游戏、模块AP、模块DHCP、watchdog在启动后约4.77～4.79秒运行；管理AP进程约5.98秒运行。五个服务均没有进程重启计数增长。
- 在启动后15.34、30.41、50.46、75.51、100.57秒采样，`wlan0`均为 `Not connected.`，普通路由表均无默认路由；五服务均active。
- 15秒时15块在线，ORDER尚在恢复；30秒及之后三次采样均为**16在线、16分配、ORDER ready**。开机初期恢复顺序需要时间，未将15秒样本写为已全部恢复。
- 本机电脑确实连接管理热点，经Wi-Fi接口取得 `10.0.0.57`。由于以太网也在 `10.0.0.0/24`，HTTP诊断明确绑定Wi-Fi接口，避免误访问家庭路由器。
- 08:55:53～08:56:32 UTC、家庭连接仍不可用期间，6/6次请求成功：3次 `/health`，3次完整管理页面，HTTP均200，页面136,392字节，请求约15～16ms。此前重启期间电脑尚未重新关联的4次探测失败保留在原始记录中，不计入已关联后的成功率。
- 恢复机制约153.37秒完成家庭连接；配置文件与备份逐字节相同，autoconnect仍yes。最终五服务enabled/active、无failed unit，16格及ORDER恢复正常。
- 原room993580123保留，状态版本72→73，资产、轮次、阶段、拍卖等业务字段保持。服务程序仍为 `06cff422b9bc106ecdfba7a52ada320526fbee7f68b9c7b43b76c503905ac5f3`，本次未替换游戏程序或重刷模块。

本次为**无家庭网络的软件重启和真实管理Wi-Fi访问**，没有实际拔掉Pi电源冷启动。四台玩家屏当时离线，未将此记录扩展成四真人一局或32格＋4屏容量验收。

## 仓库模板修正

现场游戏/AP单元此前已去掉家庭网络等待，但仓库通用模板仍有旧依赖。本次修正：

- `Server/RaspberryPi/deploy/gridopoly.service`：移除 `network-online.target`，保留本地AP/DHCP的启动顺序，与现场游戏单元一致。
- `Server/RaspberryPi/deploy/gridopoly-ap.service`：等待NetworkManager服务自身，不再等待 `NetworkManager-wait-online` 或网络上线。

两个模板在Pi上通过 `systemd-analyze verify`。现场ALFA的专用USB单元、接口命名、设备事件及guard保留，不以通用模板覆盖它；本次重启测试使用的是现役配置。

## 清理与证据

测试专用probe服务和restore服务/计时器已禁用并删除，重启前的临时恢复计时器随重启失效；没有新增长期或周期任务。电脑临时Wi-Fi配置已删除，并恢复原来无线断开、以太网联网状态。

本机私有证据：`C:/Users/kicof/AppData/Local/GridopolyBootAudit/`；Pi备份和采样：`/root/gridopoly-offline-boot-20261020/`。后者包含原家庭配置，不上传仓库。首次准备时超长测试SSID被NetworkManager拒绝，原配置已恢复；随后使用合法长度重新进行测试。该准备失败未作为启动失败或通过证据。
