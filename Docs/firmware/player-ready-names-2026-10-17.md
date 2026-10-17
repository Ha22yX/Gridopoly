# 玩家等待页未命名占位修复 — 2026-10-17

用户照片：ROOM SETUP / PLAYERS READY 中 P1、P3 仍在 SETTING UP，姓名为两个方框；已确认的 HARRY 与 Bot 1 正常。

## 原因与显示规则

`ui_renderer.cpp::identitySeatName` 原先在身份快照姓名为空时调用 `appPlayerDisplayName`，该函数在没有有效名单时返回 `demo_data.cpp` 中的演示中文名字（P1 凌汐、P3 诺瓦）。卡片使用的 Montserrat 10 没有这些字形，因此显示缺字方框。此代码路径可以在主机上复现，不是本次照片所能证明的触摸或供电故障。

- 真人尚未确认姓名时显示 `PLAYER 1`、`PLAYER 3` 等实际席位编号；状态仍显示 SETTING UP / OFFLINE。
- 服务器确认姓名后原页面更新正式姓名；HARRY、Bot 1 等可显示的名字保持原样，READY 及人数逻辑不变。
- 只使用身份快照或有效的权威名单，不再从演示名单取名字；确认位按 playerId 读取。
- 对姓名逐字检查当前字体。未支持的字形、空白或控制字符使用席位占位，不显示乱码；这是显示回退，不改服务器姓名、Tag、房间或玩家资产，也不是新增完整中文字体支持。

## 验证

真实 LVGL 与生产 renderer 主机回归 **123 PASS / 0 FAIL**，新增 7 项覆盖：未命名双卡、已确认真人/机器人及人数、未确认草稿、同页确认更新、缺字回退且保留原姓名、权威名单延迟、六席布局按真实 playerId 取名。旧实现用最终布局刷新后的同一组用例复现 4 个失败，修复后通过（旧版日志 `old-renderer-final-fixture.log`）。离线帧缓冲预览已检查 PLAYER 1 / PLAYER 3 在卡片内完整显示；未联网拉取头像，不当作实体屏验收。

本机证据：`C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/ready-names-20261017/`；回归日志在同级 `ready-names-before-20261017.log` 和 `ready-names-after-20261017.log`。

## 构建与部署边界

16MHz 正常候选已构建成功：`C:\Users\kicof\AppData\Local\GridopolyPlayerTools-3311\runs\production-20261017-002801-c4576fafb78d4088834dd8644b6403ec\candidate.json`。

| 项目 | 实际结果 |
| --- | --- |
| 应用 SHA-256 | `70a910b6c927616c9c65cc5c2b76f8307c98ef94006d57e2941900f21d024071` |
| 应用文件 | 1939952 字节，比上一联合候选增加 224 字节 |
| candidate.json SHA-256 | `0711677b2eb09726059b76f541534bcb779c423901f365c60adfe1550afc961c` |
| 冻结源清单 SHA-256 | `fe17775911d157e4d08883b12496e5d8c7cdbe5e01efb0139c10abb651857d0d` |
| ELF SHA-256 | `76a71be7d2520c4db88c2a2e0952d642b018c592231675d0c5f06d1dd5d62722`，与应用内嵌值一致 |
| 冻结 Git 基线 | `dc53c1dfc67002409de540590d8fb08e2d6bd794`，包含本次冻结时未提交的 UI 修复，以逐文件 SHA 为准 |
| 源与产物核验 | 2145 项源快照、当前源、四个刷写段哈希一致，分区容量/边界及 HWCDC 检查通过 |
| 静态 RAM | 126368 字节，与上一联合候选相同；不是运行时可用堆测试 |
| 配置 | production、vendor 16MHz、core1/mode3、viewe-md80et-v10、displayDiagnostic=none |

相对上一候选 `91617134…`，冻结清单只有 `ui_renderer.cpp` 内容改变。保留电量兼容与有界触摸诊断，不改变触摸采样或识别算法。**这是16MHz候选，不能用于要求12MHz的COM29**；COM29升级时需另构建匹配配置。本轮未修改冻结发布包。

构建日志 `build-vendor.log`、独立核验 `candidate-verification-root.json` 位于上述本机证据目录。

用户此前要求三台充电、先理论验证，本轮未操作串口或设备。已询问充电/接线是否就绪，在收到回复前不升级。COM27/28 仍为 `8ef6c051…`，COM29 仍为 12MHz `df9f1282…`，COM7 未动；不能把源码修复说成照片中的屏幕已更新。COM28 手写与 COM29 电量实机问题继续见[手写调查](player-handwriting-com28-2026-10-16.md)和[电量兼容](player-battery-bridge-compat-2026-10-17.md)。

负责人：主会话完成源码、验证、Git 和升级协调；设备就绪后先核对 MAC/时钟配置，再进行有界升级及等待页目视验收。
