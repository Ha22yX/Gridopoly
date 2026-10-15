# 玩家供电板：USB、FPC 与电量遥测独立复核

审查日期：2026-10-14（America/New_York）。本报告只读当前快照，不修改设计、不操作实物、不作为首件功能测试合格证。

## 结论

在本次分工涉及的电路中，未发现 USB 数据正负接反、SC18IM704/MAX17048 电源脚接错或 UART TX/RX 同向相接等确定性致命错误。**仍不能签署“任意电脑、无电池、直接烧录、全功能无需验证”的投产结论。** 已明确的功能限制是默认 USB 源只有 100 mA 输入档；FPC 物理接触与实际屏幕版本、负载瞬态、USB 首次/恢复烧录、遥测必须首件验证。旧订单 BAT 短路的铜层结论由主审报告处理，本报告不以逻辑网表证明实物无短路。

数据来源为本目录 PCB1-netlist.json、schematic-netlist.json、current-snapshot.json、P1/P2.source.jsonl。独立比较 243 个 PCB 引脚网络与原理图：0 个差异。

## 1. USB-C 电流识别：接线正确，默认电脑供电存在明确限制

实查 U5 TUSB320LAIRWBR：1/2 接 CC1/CC2，3 PORT 接地（UFP），5 ADDR 悬空（GPIO 模式），10 GND、11 EN_N 接地，12 由 U6 提供 3.3 V。VBUS_DET 经 R11/R12/R13 三个 300 kΩ 串联，合计 900 kΩ。R14 上拉 OUT1，Q2 PNP 把其低有效输出转为 USB_HIGH_CURRENT 高电平。

U1.EN1（6）固定接地；EN2（5）接 USB_HIGH_CURRENT。因此按官方真值表推导：

| 电源广告 | OUT1 | U1 输入档 | 实际含义 |
|---|---|---|---|
| 默认 USB 电流，包括普通 USB-A 转 C | 高 | USB100，最大100 mA | 电池可能补足负载；开机时不保证净充电 |
| USB-C 广告1.5 A或3 A | 低 | ILIM 电阻档 | R2=1.8 kΩ，典型约894 mA，容差上限约965 mA，低于1.5 A |

该逻辑不是 USB PD 协商，不请求9 V等高电压。TUSB320 断电默认 Rd，故 U6 由 VBUS 供电没有形成“没电就不能发现USB-C”的启动闭环。[TI TUSB320LAI §5、§7.2.4、§7.3.3](https://www.ti.com/lit/ds/symlink/tusb320lai.pdf)；[TI BQ24074 表7-2、§9.3.4](https://www.ti.com/lit/ds/symlink/bq24074.pdf)。

厂家屏幕 SPEC P9 给出最大背光条件下**典型320 mA@5 V**、背光关闭**典型100 mA@5 V**，不是320 mA保证最大值。考虑底板和升压损耗，默认USB100源不能覆盖这些工作条件。已接正常电池时可能正常烧录，但不能据此承诺普通电脑无电池烧录或开机边用边净充电。[厂家SPEC P9](https://github.com/VIEWESMART/UEDX48480021-MD80ESP32-2.1inch-Touch-Knob-Display/blob/main/information/UEDX48480021-MD80ET%20V1.1%20SPEC.pdf)

**投产影响：** 若要求普通电脑无电池也能直接启动并烧录，这是必须解决的功能缺口。可保留当前板并明确使用有电电池或提供1.5 A广告的USB-C主机；若要消除限制，须增加符合USB授权电流规则的枚举/电流档控制或独立安全供电路径，并验证启动功耗。不能单纯把EN2拉高强取大电流，也不能假设固定500 mA在任何USB源上合规。

## 2. USB 数据路径、ESD 与关机隔离

J1 A6/B6 同接 DP、A7/B7 同接 DM，CC1/CC2分别独立，未复用数据线充电识别。D1 USBLC6-2SC6 的1/6接DP、3/4接DM、2接GND、5接VBUS，与保护拓扑相符。[ST USBLC6-2 引脚图](https://www.st.com/resource/en/datasheet/usblc6-2.pdf)

U7 TS3USB221A：8/7为主机DP/DM，1/2到屏DP/DM，9选择脚接地，3/4第二通道空置。U8由5V_SCREEN产生3.3 V；R19把OE拉高，Q3仅在VBUS存在时拉低OE。由此推导：屏幕开机且USB接入时通道接通；仅电池开机、USB断开时通道关闭；屏幕断电时U7具备规定的power-off leakage隔离。器件级Ioff并不等于整板绝对零漏电。[TI TS3USB221A §4、§5.5、§7.4](https://www.ti.com/lit/ds/symlink/ts3usb221a.pdf)

几何快照中，USB HOST DP/DM 铜线总长约24.013/23.817 mm，SCREEN DP/DM约22.968/25.047 mm（总和包含焊盘短接支线，不能当作精确传播路径）；DM两段各有2个过孔，DP无过孔，宽度有0.2/0.25/0.6 mm变化。它不是严格对称、已证明90 Ω的差分布线。目标ESP32-S3是12 Mbps full-speed，因此不能仅凭这些不对称断言必然失败，也不能把U7的480 Mbps额定值视为整板480 Mbps认证。

**投产影响：** 未发现数据通路的逻辑阻塞。须用实际线缆/屏幕验证USB两种插入方向、枚举、整片烧写校验、连续日志、屏幕关闭后主机不反向供电。现有D1保护数据/VBUS，CC口无独立外置ESD器件；整机端口抗扰度不能由芯片HBM等级代替。若有整机ESD指标，需测试并视结果增加合适低电容CC保护。

## 3. 原生USB下载、调试和恢复路径

当前J2逻辑定义：1 DM、2 DP、3 RST、4 SCREEN_TX、5 SCREEN_RX、6/7 NC、8 GND、9 NC、10 5V_SCREEN，11/12机械固定脚接地。SW2连接SCREEN_RST与GND。与用户指定的接插件反序关系一致；它不是允许排线翻面随便插的电路。

ESP32-S3 USB Serial/JTAG可自动进入下载模式，但应用重配置USB脚/关闭控制器后，需要GPIO0拉低并复位恢复。[Espressif官方USB Serial/JTAG说明](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/usb-serial-jtag-console.html)

厂家MD80ET V1.1 SPEC P12原理图中，旋钮按压SW1的SWKEY连接GPIO0；因此**按住旋钮并按底板SW2复位**是该厂家版本的合理恢复路径。不能因为FPC没有GPIO0就断言无BOOT；也不能未经实物版本验证就保证所有同外观屏相同。本次已保存厂家PDF和P12横向渲染图，图中K1/K2/K3为其他并联按键点，实际SW1同SWKEY。[厂家SPEC](https://github.com/VIEWESMART/UEDX48480021-MD80ESP32-2.1inch-Touch-Knob-Display/blob/main/information/UEDX48480021-MD80ET%20V1.1%20SPEC.pdf)

**投产影响：** 目前无证据要求另加底板BOOT按键；需首件确认屏幕版本及恢复动作有效，调试控制台配置走USB，避免占用给U9的UART。

## 4. J2机械和载流资格尚未闭合

当前料号 XUNPU FPC-05F-10PH20/C2856798 为10针0.5 mm下接触连接器；供应商所列适配FPC厚度0.3 mm、额定电流0.5 A。制造商PDF本次下载被403拒绝，未用商家参数代替实物配合验证。[供应商料号记录](https://www.lcsc.com/product-detail/C2856798.html)

单个5 V接点和单个GND接点承载屏幕电流；机械焊脚接地不能当作额外FPC地触点并联。320 mA典型小于500 mA额定，**不构成已证实过载**；但没有最大电流和温升测试，不能拿推荐1 A电源反推实际必过载，也不能视为峰值余量已充分。

**投产影响：** 原有厚度未测、接触面/插入方向未完成实物贯通验证，必须在放行前闭合。断电核对屏幕5 V最终落在J2.10、屏幕地落在J2.8，并确认厚度、锁扣、插入力和绝缘间距；随后以最大亮度及无线发送测输入峰值、压降和接点温升。若超出接点额定，需换额定足够且机械相容的连接器/排线供电方案。

## 5. UART → I²C → 电量计与状态

当前连接证据：U9 SC18IM704 RX10接SCREEN_TX，TX11接SCREEN_RX；VDD12/VREFP15均接SCREEN_IO3V3，VSS13接地；GPIO6/3经R24上拉，RESET4由R25/C20构成上电复位；C18去耦。GPIO0/9读取CHG_N、GPIO1/16读取PGOOD_N，两网各10 kΩ上拉到同一受控3.3 V。SDA1/SCL2接U10.8/.7，R22/R23=4.7 kΩ上拉到受控3.3 V。SC18IM704不需要用户下载底板固件，默认9600-8N1、上电发送OK，GPIO默认为输入。[NXP SC18IM704 §6–9](https://www.nxp.com/docs/en/data-sheet/SC18IM704.pdf)

U10 MAX17048：VDD3接BAT_PACK，C19=100 nF去耦；CTG1/GND4/EP9接地；CELL2同BAT_PACK（该型号此脚内部不连接）；QSTRT6接地，ALRT5不使用。针脚拓扑与官方基本应用一致；它提供模型估算SOC与电压，没有电流取样电阻，不能被解释为实际充电电流测量。[ADI MAX17048 引脚说明与应用电路](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX17048-MAX17049.pdf)

固件要求：

- 与U9建立通信并设置/确认GPIO0、GPIO1仍为输入。读MAX17048地址0x36的电压、SOC；依据电池型号验证模型误差，掉电/无电池/读失败应显示不可用，不能展示上次值为当前真实值。
- PGOOD低表示输入电源有效，CHG低表示充电活动；CHG高还可能是禁用/未充电/故障，**不能单凭CHG高宣称“已充满”**。结合电压、SOC和状态变化显示“供电/充电/未充电”，需要真正故障原因时当前两信号不足。
- 维持默认约99 kHz（100 kHz级）I²C。依据MAX17048最大60 pF、SC18IM704约15 pF估算，仅器件电容与4.7 kΩ已给出约299 ns上升时间，尚未计走线；所以没有375/400 kHz的时序裕量证明。100 kHz允许1 µs上升时间，方案合理；高速需求则应改上拉并检查VOL及波形。

**投产影响：** 未发现遥测硬件必须改线的确定性问题；功能仍依赖屏幕固件。不能把“底板无需烧录”误写为“屏幕无需适配”。本轮不审软件实现或执行固件烧录。

## 6. 原理图注释与制造交付

P2源文件仍含“J2 excluded from PCB/BOM”“J3/J4 are wire pads”“J4 probe pending”等旧草稿注释，以及J2名称中的CANDIDATE/HOLD。这与当前已装J2、J3端子、TS固定电阻的实际网表不符。它们不会形成电气短路，但正式制造文档必须统一，否则易造成装配/复核误解。警告条目由主审结合用户提供的43条明细逐项核验，本报告不把其称为未分类ERC通过。

## 7. 本分工建议的放行条件

1. 锁定修复后制造包及校验值，绝不混入旧订单铜层；该项由主审完成。
2. 对实际屏幕/排线确认反序针脚、0.3 mm配合与供电接点峰值/温升。
3. 首件用有电电池和数据线完成USB正反插、完整烧写+校验、持续日志、旋钮BOOT+复位恢复；另外明确测试默认USB100和1.5 A广告两类供电条件。
4. 验证屏幕关机隔离及电池电压/SOC/充电状态；在100 kHz确认I²C通信。
5. 若产品要求普通电脑无电池运行，先修订供电/电流授权方案，不能以仅更新说明书代替该需求。

快照SHA256：PCB1-netlist `7fee63bcac5412992a08d29fca70b60ab0c1b5f33aa76a67780f44741f8a7125`；schematic-netlist `d9e4665240d30dd23e839e6bb02fd68e9e5dcf05a7ec9245d7a2b428de0e7098`；current-snapshot `514525b69079c1752307edff7b40681068bd6d8691afb7db4ee0688ce93eb4a3`。后续若修改电路，必须重审相关结论。

## 8. 追加：J1 的250 mA库属性冲突已核查

主审发现在线库把 TYPE-C-31-M-12/C165948 的 `Current Rating - Power (Max)` 写为250 mA，同时链接文件名前缀为C2682493的PDF。对此没有直接略过，也没有按250 mA判定过载。

已下载并视觉读取该PDF（保存为 `j1-library-linked.pdf`、渲染为 `j1-library-linked.png`）：图纸右下角确为 **HRO TYPE-C-31-M-12，2020-11-09**；右上角主要特性明确为 **3 A、20 V**，表中A4/B9/A9/B4为VBUS，A1/B12/A12/B1为GND。文件名前缀不同，不等于内容是另一种连接器。[库链接的厂家图纸](https://atta.szlcsc.com/upload/public/pdf/source/20210111/C2682493_D8CCFED08835E878D16ACCF4460E43B8.pdf)

HRO厂家当前官网同型号页面列为 **DC20 V、5 A**；JLCPCB C165948当前网页也列5 A。厂家现行family PDF下载返回403，本次不假称已读取该份PDF。250 mA与已读旧厂家图纸的3 A、现行厂家页面5 A均矛盾，应列为库属性问题；没有可靠证据把250 mA进一步解释为“信号触点额定”，不采用这种猜测。[HRO官方产品页](https://en.krhro.com/Product-Details/726.html)、[JLCPCB精确料号记录](https://jlcpcb.com/partdetail/C165948)

当前PCB几何与网表显示：

| PCB焊盘 | USB接触位置 | 当前网络 | 焊盘中心（mm，USB朝下的板坐标） |
|---|---|---|---|
| A4B9 | A4+B9两触点 | USB_VBUS | x37.600，y72.964 |
| B4A9 | B4+A9两触点 | USB_VBUS | x42.400，y72.964 |
| A1B12 | A1+B12两触点 | GND | x36.800，y72.964 |
| B1A12 | B1+A12两触点 | GND | x43.200，y72.964 |

因此四个VBUS触点、四个电源地触点均连接，采用每两触点合并宽焊盘，USB正反插均有电源触点，并非只接一半。外壳四脚另接GND。R2=1.8 kΩ±1%且BQ24074 KILIM最大1720 AΩ，芯片输入限流最大约 `1720/(1800×0.99)=0.965 A`，另加U6/U5等少量直接VBUS耗电；仍明显低于旧图纸较保守的3 A连接器额定。

**本项结论：J1不是250 mA电源瓶颈，本板约0.9–0.97 A输入设计未发现连接器载流不足，不因此阻塞生产。** 正式BOM仍应锁定HRO/C165948与采购批次，纠正库元数据；此结论不替代接点焊接质量、铜线温升和实际线缆压降验证，不应把“5 A连接器额定”写成“本板可取5 A”。
