# 第四台玩家屏烧录记录（2026-10-15）

用户新增一块玩家控制模组并要求写程序。新设备随后枚举为COM29，PnP父身份与芯片ROM完整MAC均为`20:6e:f1:ba:c9:04`。已通过串口自动进入下载模式，完成原厂备份、分区迁移、v4普通固件写验与60秒正常联网观察。当前仍待用户确认新屏实际画面，不能把写验或运行日志代替显示验收。

## 当前设备

| 项目 | 结果 |
| --- | --- |
| 端口 / 完整MAC | COM29 / `20:6e:f1:ba:c9:04` |
| 芯片 / Flash | ESP32-S3 / 16 MB |
| 服务器名称 / deviceId | `GP-206EF1BAC904` / 1347248425 |
| 设备网IP | `10.42.0.46` |
| 普通应用SHA256 | `dfc4b7e547a129fd5b500396a2a1f0e3866edf80b53d6684f0830d0539340cb6` |
| 固件来源 | 冻结`trial16-2026-10-15-v4`，未重新构建 |
| sourceDigest | `2885640893da7a8b26f2e7e30f85ead131c8b601024382a955ed7f7b2335f19a` |
| release.json SHA256 | `a182593fe2a799e8bbec1a01ad4d6616f17e4e1c65525f528977dca9d97344f6` |

该候选包含已提交的输入排队、头像焦点刷新及等待页重复整屏重绘修复；core1/mode3/40行bounce、原显示时序保持，电池遥测关闭。候选此前的回归与COM27/28用户目视证据见[显示修复报告](player-new-screen-display-progress-2026-10-15.md)，本轮未重跑并冒称新的代码测试。

## 备份和写入

首次枚举只有旧COM27/28，没有新设备，未对旧设备盲刷。用户随后指定COM29，刷新枚举已出现新MAC后才打开串口。操作使用固定esptool5.3.1；其他串口未打开。

原厂分区与上两台相同：factory `0x20000/0x5DC000`、nvs `0x9000/0x6000`、phy_init `0xF000/0x1000`、fctry `0x10000/0x6000`，不兼容当前player布局。09:34:54.667～09:37:17.244 UTC（当地05:34:54～05:37:17 EDT）使用1 KiB读请求保存完整16 MB；读回镜像与芯片整片MD5一致，完整MAC再次核对。备份SHA256 `776087950c683537baf1e040ae9e75f99ad8208bbaa49dce404a5d2d8b9e4da0`、MD5 `94078d40136f77baa9d944a1561bf511`，与前两台原厂镜像相同不表示设备身份相同。

09:37:42.194～09:38:26.774 UTC（05:37:42～05:38:26 EDT），在同一串口连接内重核MAC、16 MB、未启用secure boot/flash encryption、原分区及芯片全片MD5，目标ffat/coredump区域原为FF。随后只初始化目标20 KiB NVS `0x9000..0xDFFF`，写入并读回校验0x0/0x8000/0xE000/0x10000四段，确认目标分区与MAC后hard-reset；结果verified。没有erase-all或写eFuse。原厂配置保留在私有完整备份中，不能称首次迁移保留了设备内全部原NVS。

## 运行与现有游戏

09:38:39.157 UTC起无主动复位采集60.062秒、5136 bytes，DTR/RTS=false、不发输入命令，最终error=null/closed=true。窗口内持续Wi-Fi status3/IP10.42.0.46，uptime约15～70秒，正常发现原房间；无窗口内重启/panic/brownout标记。采集晚于启动，未捕获本次首帧/build行，不将其写成完整启动过程。

09:39:03.375 UTC服务器确认4台玩家屏全部online/eligible：COM28 seat1、COM7 seat2、COM27 seat3、新COM29 seat0。此次操作前已有room993580113/version11，3真人＋1机器人；新屏等待用户下一次新游戏纳入，不抢占当前机器人席位。刷写前后`/api/sync`仅`identity.serverEpochMs`变化，其余字段完全相同。未创建新局、改变Tag/角色/资产，未重启服务器。

用户画面问题已在刷完后发出，仍待回答。四屏真实入局、Tag配对、完整游戏与电池功能不是本次单板烧录的通过项目。

## 证据

本机`C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/new-player-20261015-093244/`保存PnP核对、release验证、原厂完整备份与芯片MD5、迁移日志、串口原始数据/summary、HTTP前后快照和逐字段差异。原始Flash、游戏快照及本机工具不加入Git或普通发布包；v4冻结包保持不变。
