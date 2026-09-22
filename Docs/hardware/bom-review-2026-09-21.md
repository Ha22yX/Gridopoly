# BOM 警告与采购交接

文档更新：2026-09-22。汇总此前对用户提供的角落模块13张贴片匹配截图、当时角落原理图网表及器件资料的核查；本轮未重新读取EDA、查询库存或做硬件测试。以下是选料匹配结论，不代表已点击确认、替换物料、下单或完成PCB焊接朝向审核。

| 图片 | 位号/料号 | 核查与处理 |
| --- | --- | --- |
| 1 | LED1 / C125095 | LTST-C190KSKT黄色0603，LED0603-RD是封装名称，不强制红色；Pin1阴极到GND，Pin2阳极。需要黄色可保留 |
| 2 | R31 / C346923 | CSSH2512FT10L0，10mΩ（0.01Ω）±1% 2W，2512与RES-SMD_L6.3-W3.2为对应尺寸命名；保留Kelvin采样 |
| 3 | C10/C27/C29/C31/C39 / C19702 | 10µF ±10% 10V 0603；实际均为3.3V/5V域。C27/C29为两路5V输入侧电容，未接24V；重复行可共用料号 |
| 4、7 | C56/C40 / C14663 | 100nF ±10% 50V 0603；C56跨BOOT–SW，C40为5V对地；同料号不代表可把网络改成一样 |
| 5、8 | C202/C301 / C28323 | CL21B105KBFNNNE，1µF ±10% 50V 0805；C202接HP_DT/HP_RTN，C301接24V_BUS/GND，选料匹配 |
| 6、9 | C302及C5/C7/C8/C9/C12/C21/C22/C23/C24等 / C1591 | CL10B104KB8NNNC，100nF ±10% 50V 0603；C302为24V入口去耦，耐压选型匹配；其他同料号位号全部保留 |
| 10 | R36 / C25803 | 100K识别为100kΩ，±1%、0603匹配 |
| 11 | U12～U21 / C52941391 | WS2812B-B-V6，RGB可编程5050四脚，非固定单色；1VDD/2DOUT/3VSS/4DIN，仍须核对实际贴片方向 |
| 12 | X1 / C42448900 | OH2EL89CFI-111YLC-4M / YSO130HR，5V、4MHz、CMOS有源振荡器、3225四脚；1OE上拉至5V、2GND、3RFID_CLK_4M、4=5V_RFID，与当前网表匹配；不可换无源晶体或仅支持3.3V型号 |
| 13 | 10kΩ组 / C98220 | RC0603FR-0710KL，10kΩ ±1% 0603；11个位号核对一致，可保留 |

10k组完整位号：R3、R6、R11、R12、R13、R14、R17、R22、R24、**RIO0**、R204。RIO0是字母I/O和数字0，不是R100。

同一料号在多行BOM出现不等于重复放置器件。C39/C40等原属性与明确MPN写法不同可能分行；保留全部位号和正确用量，不为清除警告删件。陶瓷电容有效容量仍受直流偏压影响，本次料号匹配不是动态电源仿真。

## 需要处理的缺料

第一张截图露出的C165751库存为0，对应R26/R30：RC0603FR-0723K7L、23.7kΩ、±1%、0603，用于电源分压。**尚未替换**。C22912 / 0603WAF2372T5E是同阻值/精度/封装候选，当前贴片库存未锁定，仍须复核额定功率、工作电压、温漂及库存；不可随意换成22k或24k。

## 当前与历史采购范围

当前U23=LMR16030SDDAR/C136648，两板保留U201及配套支路保护；角落6件直流入口见[直接24V设计](corner-dc24-input-2026-09-21.md)。旧USB-PD区72件已经删除；J101、C128、R120等旧PD记录不能作为当前采购清单。

[Hotplug库存快照](../../PCB%20Files/ModulePower/Hotplug/BOM-stock.csv)含已取消PD料号；[DC24新增BOM快照](../../PCB%20Files/CornerModule/DC24-2026-09-21/input-bom-stock.csv)仅覆盖入口6件，不代表整板有货。下单应重新导出当前原理图/PCB同版BOM、坐标和Gerber，并分别确认商城库存与实际贴片库库存。

## 本轮核对资料

- [LiteOn LTST-C190KSKT厂家手册](https://optoelectronics.liteon.com/upload/download/DS-22-99-0189/LTST-C190KSKT.pdf)：黄色LED。
- [Stackpole CSS/CSSH厂家尺寸](https://www.seielect.com/catalog/SEI-CSS_CSSH.PDF)：2512尺寸系列。
- [YXC YSO130HR厂家手册](https://atta.szlcsc.com/upload/public/pdf/source/20251107/B63E8094D25763FD2F6341B5FB7B924C.pdf)：5V有源、OE/OUT/VDD/GND定义。
- [WS2812B-B-V6厂家手册](https://atta.szlcsc.com/upload/public/pdf/source/20251224/2F6ED8FFFF9D3044AED3CCAA3E4BF0D9.pdf)：RGB及四脚定义。

实时网表只读捕获保存于本机临时目录gridopoly-corner-bom-audit/current.net；长期供电证据为[两板网表与检查结果](../../PCB%20Files/DesignAudit-2026-09-21/)。未将临时下载缓存提交为工程设计。
