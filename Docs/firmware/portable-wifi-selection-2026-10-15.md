# 便携棋盘的 USB Wi-Fi 选型

更新：2026-10-15。用户希望带出门后仍能独立游玩，已确认购买 **ALFA AWUS036ACM Long-Range Dual-Band AC1200，MediaTek MT7612U，双天线、USB 3.0**。现已到货、识别并部署；20台同时接入/注册、设备自动恢复与Pi软件重启自启动已有证据，最终持续观察为16格＋3屏（另一屏由用户关闭）。36台和完全脱离家庭网的冷上电仍待实测，见[部署记录](usb-ap-deployment-2026-10-15.md)。

## 需求与现状

- 当前16格＋4屏需要20台无线设备；最终32格＋4屏需要36台，另需留手机/电脑管理连接余量。
- 现有ESP32-S3模块使用2.4GHz 802.11 b/g/n；5/6GHz标称速率和Wi-Fi 6/7等级不能直接说明本项目的接入容量。[Espressif规格](https://www.espressif.com/en/products/socs/esp32-s3)
- 用户已给16格接24V；Pi内置热点连续只观察到7台活跃关联（3屏＋4格），用户关闭一屏后多一格上线。DHCP未耗尽，服务端无拒绝注册证据，精确芯片限制未直接读取。详见[实机调查](ap-capacity-investigation-2026-10-15.md)。

## 选型依据与边界

[ALFA产品规格](https://www.alfa.com.tw/products/awus036acm_1)确认AWUS036ACM使用MT7612U，支持2.4/5GHz、Linux和USB 3.0；[厂家Linux支持页](https://docs.alfa.com.tw/Support/Linux/MT7612U/)列出Raspberry Pi OS支持及主线内核驱动。已核对的[Amazon商品](https://www.amazon.com/dp/B08BJS8FXD)标题与芯片匹配。购买和到货记录按准确型号核对，其他相似后缀不自动视为同一硬件。

Linux 6.18的[mt76x02定义](https://github.com/torvalds/linux/blob/v6.18/drivers/net/wireless/mediatek/mt76/mt76x02.h)含128项WCID资源，[站点分配代码](https://github.com/torvalds/linux/blob/v6.18/drivers/net/wireless/mediatek/mt76/mt76x02_util.c)用该表分配关联站点，包含USB/AP路径。这是选择它进一步验证36台接入的驱动依据，**不是厂家保证128个客户端，也不是本项目36台稳定联网证据**。供电、固件、现场2.4GHz干扰、广播和实际HTTP/UDP负载仍需验收。

截图中的TP-Link AC600/AX1800/BE6200/BE6500不能按数字选容量最大者。只有AC600的T2U Plus具体型号在截图中展开，其他选项不能仅凭速率档认定硬件revision；查到的对应官方产品页没有给出Pi Linux下36台AP的保证。后续已由用户明确选择AWUS036ACM，不再把这些型号列为采购待选。

## 计划部署方式

树莓派保持唯一游戏服务器；新USB网卡专门提供2.4GHz `gridopoly` 热点及设备网 `10.42.0.0/24`，服务器继续 `10.42.0.1`、HTTP 80/UDP 4242及既有广播发现。内置Wi-Fi只保留管理/可选上网用途。这样棋盘通信由本地局域网承载，外出正常游玩不依赖家庭路由器或互联网；外出冷启动、素材缓存和完整游戏流程仍要实际验证。

到货前仅准备配置与离线回归，不切换当前无线接口。设备名不得假定永远为 `wlan1`，实际按硬件身份绑定；先验证USB ID、驱动和AP能力，再协调一次有回退路径的迁移。旧AP与新AP不能同时持有相同服务器地址。DHCP网关、HTTP素材、玩家认证报文来源及UDP发现都要保持兼容；只更换SSID不构成完成迁移。

初次目标为20台全部上线、反复断电启动与重连、真实配对/ORDER环/移动流程；后续扩到36台并加入管理终端、素材加载和长时间运行。配置64等连接限额只是软件设置，不构成对应数量实测通过。到货后的USB部署由服务器会话实现，主会话审核与协调设备窗口。

已新增[USB热点准备工具与迁移/恢复步骤](usb-ap-deployment-2026-10-15.md)：到货后只读核验硬件身份、驱动、国家码、地址与独立管理连接，再生成私有候选和原件备份。工具不执行切网，也没有自动安装或回退定时器。候选移除AP及游戏服务对家庭网络上线的直接等待；离线冷启动仍需实机确认。

相关记录：[当前交接](../CURRENT-DEVELOPMENT-HANDOFF.md)、[旧以太网外置AP方案](external-ap-deployment-2026-09-26.md)。旧外置AP离线测试不代表新USB路径已验收。
