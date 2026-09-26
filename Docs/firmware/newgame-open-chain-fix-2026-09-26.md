# 新建局等待页与单模块配对核查

用户于2026-09-26反馈：网页点击开始新游戏后，玩家屏停在Waiting for new game，唯一格子没有配对提示；要求支持单模块及未闭环测试。随后用户重新建局并反馈已能成功。

## 现场事实与原因边界

主任务最初只读HTTP：room993580107/version13仍为原24格房间，tagPairing.disabled、frozenDevices为空；新玩家GP-DCB4D902D1DC在线eligible但seat0。COM6 V0.33在线、RFID稳定、未分配、无锚点/上游。此时服务端尚未创建新的自动对局，因此玩家等待与格子无配对下行相符。

代码和隔离真实HTTP确认两个缺陷：

1. 网页提供16/24/32/40格，但服务器newAutoGame硬性只接受32。旧生产代码使用实际数据副本时，24格请求返回409/automatic_players_require_32_tiles且房间不变，32格请求成功。
2. 网页捕获建局失败后立即刷新状态，原失败文字被连接状态覆盖，用户无法持续看到失败原因。

未记录用户首次失败请求的实际地图选择/响应，因此不能断言第一次一定由选择24格造成，也不能把仅连接一块板当作失败根因。

20:46–20:47 UTC，主、玩家任务均通过GET确认真实新32房room993580108/version11：新屏seat1在线、frozenDevices记录正确，Tag配对complete/boundMask1，P1绑定8EFA24DF并已确认头像、等待姓名。COM6仍单块、assigned=false/unanchored/closedRing=false。用户随后确认重建已能成功。主及各任务未发送线上新局、Tag、头像操作；该变化来自现场操作，证明单块未分配模块可以完成绑定，不扩大为32实体闭环或所有灯屏视觉已验收。

主快照：`C:/Users/kicof/GridopolyReleases/newgame-open-chain-20260926/live-user-game.json`。各端被动审核与串口扰动分别见[玩家记录](player-four-screen-setup-progress-2026-09-25.md)和[格子记录](tile-production-progress-2026-09-25.md)。玩家采集实际触发USB_UART_CHIP_RESET，短暂AUTH_EXPIRE后自行恢复；没有将其误列为最初新局失败原因，没有追加重刷或网络配置变更。

## 修复范围

- 自动组队按BoardCatalog支持16/24/32/40，真人冻结与补足4人规则不变；不支持的尺寸拒绝且不修改房间。
- 配对不依赖物理板数、地图分配或ORDER闭环。单reader可复用，但每个新会话必须先看到新鲜空场，再看到新的单Tag扫描。只有完整32环的自动编号需要满足4角落/28长边约束。
- 网页在新局按钮下保留独立的提交/成功/失败结果，正常轮询不会覆盖；没有合格屏、屏过多、请求超时给出明确提示。修复提交`c68fa71`。
- 玩家109df与格子V0.33审核未发现单板门控缺陷，本次不修改或重刷设备固件。

## 验证与部署

Web布局、组队状态与真实浏览器回归通过：HTTP409后立即及再次刷新仍保留失败信息；成功重试清除错误，24格选择准确传入请求。测试拦截HTTP，没有线上建局。生成资源html91899/gzip25206、36图目录一致。

服务端提交`53dab80`；三个针对性Linux目标（配对权威16例、HTTP、认证UDP/HTTP onboarding）及正常构建通过。新增案例逐一验证16/24/32/40从旧席位场景创建新局，唯一无邻接/无锚点/未分配reader先清空、绑定、拿回和开放身份编辑；每个地图案例验证非法23格在有合格屏时仍拒绝且房间/元数据不变。未把三个目标说成重跑此前完整19目标。

主独立核对190当前源和归档SHA无差异、binary SHA `2bc084d7a981ff9ca54c81f85933efed893b9c9151bf4b4e6883570b32adfa18`一致。部署前备份实际room108数据和旧b155二进制，隔离副本连续两次恢复通过。2026-09-26 20:53:55 UTC仅原子替换服务器binary并重启gridopoly，peers恢复1。完整sync差异只有version13→16、identity revision14→16、时间和配对session/revision重建；房间、32图、冻结席位、Tag、头像、姓名状态、现金、位置和资产均保留。

候选/完整差异/服务日志：`C:/Users/kicof/GridopolyReleases/server-newgame-maps-20260926-205040/`；Pi旧binary和当前数据备份`/var/backups/gridopoly/server-newgame-maps-20260926-205040`。没有线上newgame/Tag/分配POST或AP变更。

20:55:17 UTC主以只读GET白名单打开实际部署网页：1真人3机器人、配对1/1、头像4/4、姓名3/4，新局结果提示元素存在，390px无溢出、无脚本错误/写请求。证据`C:/Users/kicof/GridopolyReleases/newgame-open-chain-20260926/live-patched-web.json`。

## 后续玩家交互缺陷

用户随后报告头像旋钮漏选/回跳，以及手写提前识别、断笔、无结果。该问题已继续交玩家任务全面修复，与本次已部署的新局兼容补丁分开记录。初审发现identity.focus与导航焦点失同步、周期状态推送使手写画布重建清空；正在做生产路径复现和修复。用户确认单板配对成功不代表这些后续编辑页面已通过验收。
