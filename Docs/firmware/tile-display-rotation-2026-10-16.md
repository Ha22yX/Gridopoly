# 格子屏180度旋转与低亮合并更新

更新：2026-10-16（America/New_York）。用户确认所有格子屏均需相对当前安装方向旋转180度，并要求将适合的相关改动一并更新。本次候选版本V0.36，覆盖长边和角落。

## 实现范围

- 统一LCD硬件方向由rotation0改为rotation2，仍是240×320。整页、图片、配对、连接、诊断页和底部局部刷新共用同一方向，画布保持rotation0，不再倒序图片或再旋转坐标。
- 实际使用的Adafruit ST7735/ST7789 1.11.0，在240×320初始化下四个行列起始偏移均为0；rotation2保留宽高，MADCTL由MX|MY|RGB改为RGB。该源码结论需要实板图文、边缘和方向目视验证。
- 一并修正初始化顺序：在安全8MHz下写入最终方向，再设置运行SPI速率。控制器恢复复用相同初始化路径，重置画布方向及差分影子，避免恢复后回到旧方向或局部页遗漏。
- 初始化及STATUS增加实际方向和宽高日志，方便确认所刷版本。LCD背光150、正常默认8MHz不变，旧单板40MHz白名单不扩大。
- 合并[已完成的V0.35低亮改动](tile-led-brightness-2026-10-15.md)：正常灯效驱动幅值为V0.34的四分之一、最终各通道16/255硬上限，维护RGB仍8/255。颜色语义、周期与优先级保持。
- 本次不改变ORDER顺序、角色、地图分配、RFID保护或服务器协议。旋转LCD不等于倒转环形模块顺序。

普通候选继续使用已部署59efe7a基线叠加LED、方向及版本差异，不将额外COM8的RFID试读入口加入批量固件。共享源码保留该独立诊断，直接构建当前完整工作区与本次隔离普通候选不同；实际来源清单分别记录。旧V0.35候选和v4冻结包不覆盖。

## 现场与操作范围

08:52:37 UTC（04:52:37 EDT）只读服务器快照为房间993580116、grid-city-16-v1；当时3格在线、未分配、ORDER stale，不能沿用昨日16块在线/闭环作为本轮现场证据。未发送创建新局、分配、Tag或玩家操作。

用户随后接入4块USB，枚举的完整身份与原16板台账一致：

| 本次端口 | 完整MAC | 角色 | 升级安排 |
| --- | --- | --- | --- |
| COM24 | a4:cb:8f:c2:99:6c | long | 先单板升级、核对方向/亮度 |
| COM25 | a4:cb:8f:c2:98:a0 | long | 单板反馈后继续 |
| COM26 | a4:cb:8f:c2:99:50 | long | 单板反馈后继续 |
| COM11 | a4:cb:8f:c6:25:e0 | corner | 单板反馈后继续，另验角落 |

COM24升级前20秒STATUS采集已关闭，实际V0.34、LCD8MHz、RMT发送失败0。INA226该窗口4.637～4.676V，读卡故障记录保留；不修改RFID阈值，也不把本次显示更新当作读卡或电气验收。采集使用DTR/RTS=false，没有主动复位。

本机现场证据：`C:/Users/kicof/AppData/Local/GridopolyRotationAudit/20261016-085237/`。所有烧录清单仅含这批已确认MAC，玩家屏不在清单。写验、正常启动/联网、用户目视和其余12板升级分别记录。

## 构建与部署记录

`firmware_boundaries`19项与`display_diff`8项合计27项通过；检查实际初始化/恢复/DisplaySink的调用顺序、画布方向、两处分配失败回退及完整/局部传输。软件边界检查不等于实物方向通过。

首次隔离源码比对在普通/诊断版STATUS差异处失败，编排随后误启动尚为V0.35的快照构建，已中止、记录exit1，不用于烧录。`firmware-build.log`保留。修正为普通STATUS只加入方向日志，确认无RFID-DIAG、rfid_diagnostics和试读入口后，在新`build-final/libdeps-final`完成双角色构建，均SUCCESS。每角色四段SHA/长度/分区、74项源码输入通过，旧V0.35候选及全部段哈希未变。

| 普通V0.36候选 | app SHA256 | app字节数 |
| --- | --- | --- |
| long | `5e277028f063c6b9f4acace010f81413663957a7f7a06188ca17e5ffacb61410` | 2714448 |
| corner | `4224e8f1415c628915d809c06d53db8f6ff8851b64827e75b96f96ca7a4fc44a` | 2714448 |

共同sourceDigest为`837dfecde4385ba5025e18145508c8dfa1c06ad97fa219e174d327e2305b9515`，基线59efe7a＋LED5522d179＋本次方向overlay。manifest明确标记rotation2/180度、默认8MHz、normalFirmware=true及rfidDiagnostic=none。

本机来源与构建证据位于`C:/Users/kicof/AppData/Local/GridopolyTileTools/display-v036-20261016/`，两个candidate在`build-final/tile_esp32s3_long/`、`build-final/tile_esp32s3_corner/`。更新包为`C:/Users/kicof/GridopolyReleases/tile-update-v036-2026-10-16/`；标准工具要求三profile，因此player仅原样引用旧v4以兼容包格式，**不用于本轮玩家升级**，不覆盖COM29的12MHz记录。实际烧录清单只包含已确认的格子MAC，私有固件和配置不进Git。

### 第一块COM24

09:05:25.463 UTC预检通过，09:05:48.878 UTC（05:05:48.878 EDT）四段写验及watchdog-reset完成。固定esptool5.3.1核对完整MAC、16MB及原分区；未erase-all或写NVS。之后35.005秒STATUS采集已关闭：实际V0.36、rotation2/180度、240×320、两个canvas为0、LCD8MHz、RMT失败0、HTTP200，窗口内未记录到boot/reset事件；不声称捕获全部开机过程或读回LCD寄存器。

服务器确认COM24在线V0.36，另外COM11/25/26在线V0.34；四格未分配、ORDER stale，不将其写为完整16环。前后房间993580116未变，API state的identity字段有变化；未主动发送游戏状态写入。RFID故障日志保留，本次显示更新不代表读卡通过。

已请求用户核对第一块方向、完整图文及亮度。**当前目视待反馈，余三块和其余12板尚未升级**；单板反馈通过后继续本批，角落另验方向/裁切。

## Git基线

已有用户命名的`gate-stable-16-full-test-2026-10-15`仍固定指向`5522d179c92a4c90bee6b1173ad395faa25b5e9d`。该检查点含V0.35源码而当时实板仍V0.34；本次V0.36另建提交，不移动标签。
