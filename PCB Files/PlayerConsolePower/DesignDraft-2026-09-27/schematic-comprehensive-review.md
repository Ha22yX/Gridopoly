# 玩家屏幕电池底板：全面原理图复核与按键修正

日期：2026-09-27。对象是玩家屏幕控制板的 Schematic2 P1/P2 与 PCB1 对应器件网络，不涉及长边/角落模块和 PCB 布线质量。本次依据当前 EDA 实际网表、符号引脚、PCB 焊盘几何及厂家资料检查。

**发现并修复了一类致命接线错误，涉及电源键 SW1 和复位键 SW2。其余已核查的电源、USB 和电量查询电路未发现新的确定性致命错误。但 41 条原生 DRC 警告明细、FPC 实物兼容性及样板性能仍未闭合，不能认定为“完美运行”或制造放行。**

## 已修复：四脚轻触开关的内部常通组

所选 XKB TS-1187A-B-A-B（C318884）内部 **A/B 常通，C/D 常通**；按下才连接两组。库符号为 1=A、2=B、3=C、4=D。PCB 焊盘坐标逆转置放角度后，1/2 在同一排，3/4 在另一排，与厂家 TOP VIEW 相符。[厂家系列图纸](https://atta.szlcsc.com/upload/public/pdf/source/20200227/C318890_053E50208A88A7BA1965D8746000774E.pdf)

| 按键 | 旧连接及后果 | 修正后的连接 |
| --- | --- | --- |
| SW1，PCB 显示 POWER | 1/3=POWER_KEY_N、2/4=GND；两内部常通组均把控制信号拉地，无法正常按键开机 | 1/2=POWER_KEY_N，3/4=GND |
| SW2，PCB 显示 RST | 1/3=SCREEN_RST、2/4=GND；屏幕复位一直被拉低 | 1/2=SCREEN_RST，3/4=GND |

已保存 P1/P2；只同步这两个按键的 2、3 号 PCB 焊盘网络，保留全部现有位置、角度、锁定状态及 POWER/RST 名称。原理图仍为 SW1/SW2，按 uniqueId gge19/gge58 关联 PCB；后续通用“导入更新”可能提出名称同步，不能把显示名变化误当成新增器件。

旧提交 97afc2d 含此缺陷。旧 79/79 检查只证明了连接符合当时的预期，没有建模机械开关内部短接，不能作为实际可运行证据。现新增两项厂家常通组回归；在隔离副本换回旧网表，两项检查均失败，修正后通过。详见 [switch-contact-review.json](switch-contact-review.json)，含符号名、实际焊盘全局/局部坐标和修复前后网络。

## 本次电气核对

| 范围 | 核对结果和适用条件 |
| --- | --- |
| U1 BQ24074 充电/电源路径 | BAT、OUT、IN、地及散热焊盘正确；CE 接地允许充电。R1=1.78k，充电约 500mA；R2=1.8k，高电流输入限流约 894mA；R20=68k，快充定时典型约 9.07h。电源路径优先供系统，充电电流可能被输入限流或热调节降低。[BQ24074](https://www.ti.com/lit/ds/symlink/bq24074.pdf) |
| USB-C 识别 U5、U6、Q2 | TUSB320LAI 设为 UFP/GPIO 模式，使用芯片内部 CC Rd。VBUS_DET 串联总阻值 900k，符合手册 855–920k。CC 广播 1.5A/3A 时，OUT1 低使 PNP 启用约 0.9A 档；普通默认 USB 电流广播仍为约 100mA 档。[TUSB320LAI](https://www.ti.com/lit/ds/symlink/tusb320lai.pdf) |
| U2 5V 升压 | TPS61023 引脚与 EN/FB 极性正确；732k/100k 得标称 4.992V，关闭时输出隔离。L1=1uH，厂家 Isat 最小 5.6A；低电池电压、5W 假设负载时估算峰值约 2.6–2.7A，有选型余量。此计算不等于整板或 FPC 已通过 1A 实测。[TPS61023](https://www.ti.com/lit/ds/symlink/tps61023.pdf)、[L1 厂家规格](https://atta.szlcsc.com/upload/public/pdf/source/20250613/CEE4AE5EC9F7EB97495A767C9ECB8960.pdf) |
| 电源电容 | C2/C3/C7/C8/C9 为 10uF、25V、X5R、0805（C15850）。C8/C9 并联标称 20uF；厂家典型曲线在 5V 下约剩 11uF，仍高于 TPS61023 的 4uF 有效容量下限。曲线为典型数据，温度、老化与瞬态仍须样板验证，不能以标称容量替代全角落容差证明。[三星特性图](https://media.digikey.com/pdf/Data%20Sheets/Samsung%20PDFs/CL21A106KAYNNNE_Char.pdf) |
| U3/U4/Q1 欠压和开关 | TPS3808 的低有效 RESET 经 Q1 反相驱动 MAX16054 高有效 CLEAR，方向正确。欠压会清除锁存，回升后不自动开机；R6=100k 对应典型 300ms 恢复延时，C5=10nF 滤波。SW1 接法已修正。[TPS3808](https://www.ti.com/lit/ds/symlink/tps3808.pdf)、[MAX16054](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX16054.pdf) |
| 欠压阈值公差 | 检测的是 SYS_RAW，标称 0.405×(1+715/100)=3.30075V。计入参考 ±2%、两只电阻各 ±1%、SENSE 漏电 ±25nA，保守估算约 3.16–3.44V；不是精确的电池端 3.3V，电源路径/导线压降另计。不存在独立精确 3.5V 恢复阈值。 |
| U7/Q3/D1 USB 数据通路 | D+/D−、ESD 跨接脚、选择脚和低有效 OE 正确；U7 使用随屏幕开关的 3.3V，无 VBUS 时 Q3 使其禁用，屏幕断电时使用器件的断电隔离能力。SW2 复位接法已修正。烧录前须先开屏；首次 ROM 下载还需屏幕自带 BOOT/旋钮按键配合复位实测。[TS3USB221A](https://www.ti.com/lit/ds/symlink/ts3usb221a.pdf)、[USBLC6-2](https://www.st.com/resource/en/datasheet/usblc6-2.pdf) |
| U6/U8 3.3V | AP2112 的 VIN/GND/EN/NC/VOUT 引脚与输入输出 1uF 电容配置正确。仅给 CC 与接口逻辑供电，不承担整个屏幕负载。[AP2112](https://www.diodes.com/assets/Datasheets/AP2112.pdf) |
| U9 SC18IM704 | RX/TX 与屏幕交叉相连；VDD/VREFP 为 SCREEN_IO3V3，GPIO6 与 RESET 有外部 10k 上拉，RESET 有 100nF；GPIO0/1 接 CHG/PGOOD，须由固件保持输入。[SC18IM704](https://www.nxp.com/docs/en/data-sheet/SC18IM704.pdf) |
| U10 MAX17048 | VDD 实测电池电压，CELL 连电池允许用于此型号；CTG、QSTRT、GND、EP 接地正确，ALRT 留空用于轮询。SDA/SCL 为 4.7k 上拉至屏幕侧 3.3V；电量计始终接电池，固件保持 EnSleep=0，关屏充电后继续估算电量。[MAX17048](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX17048-MAX17049.pdf) |
| J1/J2/J3 | USB-C 正反插两组数据脚合并正确；J2 保留用户要求的反向焊盘映射，1=DM、2=DP、3=RST、4=TX、5=RX、8=GND、10=5V，6/7/9 为 NC，11/12 接地。J3 为电池正负焊线孔。 |
| 全部引脚与 PCB 同步 | 68 件、243 脚逐一比较：222 连接脚、21 NC；J2 另核查 12 个端点。全部 NC 标记与实际网表相符；PCB 通过唯一编号对应，243 个焊盘网络一致。未改动 J2 的反向映射。 |

两线电池按用户要求不带 NTC；R21=10k 固定 TS 偏置会允许充电，**不检测电芯温度**。欠压关闭屏幕后，监控和电量计仍耗电，不能代替电池包的过放/过流/短路保护；本板也未实现电池反接保护。这些是现有方案的功能边界，不是本次新增功能。

## 工作状态检查

| 条件 | 预期行为 | 仍需验证的实际条件 |
| --- | --- | --- |
| 电池正常、USB 未接、屏幕关闭 | 充电器/监控/电量计保留低功耗供电，升压与屏幕关闭 | 静态电流和长期存放 |
| 按电源键 | 锁存打开，升压至 5V，接口 3.3V 与屏幕启动 | 启动浪涌、纹波及 Wi-Fi 瞬态 |
| 电池欠压 | 硬件强制关机，回弹不自动重启 | 实际阈值、负载压降、保持按键和抖动 |
| 接 USB-C 充电器、屏幕关闭 | 自动充电，屏幕保持关闭 | 电池允许的充电电流与温度范围 |
| USB-C 广播 1.5A/3A、屏幕开启 | 输入上限约 0.9A，系统优先，余量充电 | 满亮度、无线峰值及热限制下的净充电 |
| 普通电脑 USB、屏幕开启 | 默认约 100mA 输入，电池补充运行功率，数据通路打开 | 必须有足够电量；不保证无电池运行或边调试边净充电 |
| 屏幕关闭或无 VBUS | 数据开关隔离 | USB 枚举/拔插/挂起与回灌实测 |
| 屏幕查询电池 | UART→I²C 读电压/SOC，读 CHG/PGOOD | 固件源码及主机回归已接入，实板未验证；CHG 高不一定已充满 |

## 验证证据与未完成项

- [verification-result.json](verification-result.json)：更新后的 81/81 静态检查通过，68/243 原理图与 PCB 网络一致；不是 81 项硬件实测。
- [all-pin-nc-audit.json](all-pin-nc-audit.json)：243 脚的连接/NC 标记一致，零不一致项。
- [switch-contact-review.json](switch-contact-review.json)：厂家内部常通组、实物封装坐标和负向回归。
- 原生 DRC 本轮回读为 **41 条 warn**（旧快照为 40），API 未提供明细；已请求用户提供面板文字，尚未完成逐条分类。没有隐藏警告或调整规则来消除提示。已尝试文档化的详细返回参数、打开DRC面板、应用日志读取，并检索导出API及本地debug.log；均未取得本轮逐项列表。已阅读Computer Use技能，但本会话未提供其所需node_repl/桌面工具，无法直接读取面板。最小补充是当前DRC列表文字/导出或展开列表的清晰截图；无需重复81项检查。
- FPC 总厚度/接触面/载流、插入后的端到端通断，以及电池保护和充放电规格仍需实物确认。
- PCB 布局布线、散热、USB 信号完整性和样板测试不在本轮通过范围。当前快照不能直接作为可下单成品。

本次无新增器件、无固件接口变更、无现场烧录；不刷新或承诺采购库存。库存记录仍是原查询时的快照。修正已汇报 Gridopoly 项目总指挥。
