# 原理图43条警告与物料属性核验

用户已提供警告全文，见 schematic-warnings-user.txt。原生结果仍是致命0、错误0、警告43；本次未批量标准化、未删除告警、未把原理图写为“DRC零警告”。

两条网络警告分别重复 SCREEN_IO3V3 与 GND 同一名字，并没有列出两个不同网络。独立243脚网表和制造铜网核对未发现由它们引起的短接。它们属于同名重复标注，不能用来解释旧订单BAT短路。

器件属性警告分组列出68件。核对30个C编号：18个在当前库精确匹配；其余12个用供应商公开产品页核对C号、MPN、封装身份。库搜索未命中不等于缺货或料号不存在。SCH/PCB/BOM中的值、MPN、料号和封装一致；没有发现错料或阻容值错配证据。原生工具未给逐字段触发原因，不能宣称每个警告都已消除。

库中可见差异包括说明文字、数据手册链接、Voltage Rated/Voltage Rating字段、模型名称和本地封装UUID。Q2同一C2143在库中写MMBT3906 2A，实例另带RANGE:100-300，不能把标题中的2A当集电极额定2A；官方器件为200mA档。J1旧库给250mA，但同型号厂家旧图纸给3A、当前厂家产品页给5A，已按较低3A复核，见usb-telemetry-review.md §8；不能用旧聚合属性覆盖厂家规格。

## 料号身份记录

| C编号 | 位号 | MPN | 封装 | 证据 |
|---|---|---|---|---|
| C15849 | C1,C11,C12,C14,C15 | CL10A105KB8NNNC | C0603 | [供应商](https://www.lcsc.com/product-detail/C15849.html)；库精确命中 |
| C15850 | C2,C3,C7,C8,C9 | CL21A106KAYNNNE | C0805 | [供应商](https://www.lcsc.com/product-detail/C15850.html)；库精确命中 |
| C1591 | C4,C6,C10,C13,C16,C17,C18,C19,C20 | CL10B104KB8NNNC | C0603 | [供应商](https://www.lcsc.com/product-detail/C1591.html)；库精确命中 |
| C1589 | C5 | CL10B103KB8NNNC | C0603 | [供应商](https://www.lcsc.com/product-detail/C1589.html)；库精确命中 |
| C7519 | D1 | USBLC6-2SC6 | SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BL | [供应商](https://www.lcsc.com/product-detail/C7519.html)；库精确命中 |
| C165948 | J1 | TYPE-C-31-M-12 | USB-C_SMD-TYPE-C-31-M-12_1 | [供应商](https://www.lcsc.com/product-detail/C165948.html)；库精确命中 |
| C408332 | L1 | MWSA0402S-1R0MT | IND-SMD_L4.4-W4.2 | [供应商](https://item.szlcsc.com/397734.html)；产品页补查 |
| C8545 | Q1,Q3 | 2N7002 | SOT-23-3_L2.9-W1.3-P1.90-LS2.4-BR | [供应商](https://www.lcsc.com/product-detail/C8545.html)；库精确命中 |
| C2143 | Q2 | MMBT3906 2A(RANGE:100-300) | SOT-23-3_L2.9-W1.3-P1.90-LS2.4-BR | [供应商](https://www.lcsc.com/product-detail/C2143.html)；库精确命中 |
| C22849 | R1 | 0603WAF1781T5E | R0603 | [供应商](https://www.lcsc.com/product-detail/C22849.html)；库精确命中 |
| C4177 | R2 | 0603WAF1801T5E | R0603 | [供应商](https://www.lcsc.com/product-detail/C4177.html)；库精确命中 |
| C23237 | R3 | 0603WAF7153T5E | R0603 | [供应商](https://item.szlcsc.com/23964.html)；产品页补查 |
| C25803 | R4,R5,R6,R7,R8,R10,R16,R17,R18,R19 | 0603WAF1003T5E | R0603 | [供应商](https://www.lcsc.com/product-detail/C25803.html)；库精确命中 |
| C23239 | R9 | 0603WAF7323T5E | R0603 | [供应商](https://item.szlcsc.com/23966.html)；产品页补查 |
| C23024 | R11,R12,R13 | 0603WAF3003T5E | R0603 | [供应商](https://www.lcsc.com/product-detail/C23024.html)；库精确命中 |
| C25804 | R14,R15,R21,R24,R25,R26,R27,R28 | 0603WAF1002T5E | R0603 | [供应商](https://www.lcsc.com/product-detail/C25804.html)；库精确命中 |
| C23231 | R20 | 0603WAF6802T5E | R0603 | [供应商](https://www.lcsc.com/product-detail/C23231.html)；库精确命中 |
| C23162 | R22,R23 | 0603WAF4701T5E | R0603 | [供应商](https://www.lcsc.com/product-detail/C23162.html)；库精确命中 |
| C318884 | SW1,SW2 | TS-1187A-B-A-B | SW-SMD_4P-L5.1-W5.1-P3.70-LS6.5-TL_H1.5 | [供应商](https://www.lcsc.com/product-detail/C318884.html)；库精确命中 |
| C54313 | U1 | BQ24074RGTR | QFN-16_L3.0-W3.0-P0.50-TL-EP1.7 | [供应商](https://www.lcsc.com/product-detail/C54313.html)；产品页补查 |
| C919459 | U2 | TPS61023DRLR | SOT-563_L1.6-W1.2-P0.50-LS1.6-BR | [供应商](https://www.lcsc.com/product-detail/C919459.html)；产品页补查 |
| C79401 | U3 | MAX16054AZT+T | TSOT-23-6_L2.9-W1.6-P0.95-LS2.8-TL | [供应商](https://www.lcsc.com/product-detail/C79401.html)；产品页补查 |
| C19653 | U4 | TPS3808G01DBVR | SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BR | [供应商](https://www.lcsc.com/product-detail/C19653.html)；产品页补查 |
| C132554 | U5 | TUSB320LAIRWBR | X2QFN-12_L1.6-W1.6-P0.40-TL | [供应商](https://www.lcsc.com/product-detail/C132554.html)；产品页补查 |
| C51118 | U6,U8 | AP2112K-3.3TRG1 | SOT-25-5_L2.9-W1.6-P0.95-LS2.8-BL | [供应商](https://www.lcsc.com/product-detail/C51118.html)；库精确命中 |
| C128396 | U7 | TS3USB221ARSER | UQFN-10_L2.0-W1.5-P0.50-BL | [供应商](https://www.lcsc.com/product-detail/C128396.html)；产品页补查 |
| C5190383 | U9 | SC18IM704PWJ | TSSOP-16_L5.0-W4.4-P0.65-LS6.4-BL | [供应商](https://www.lcsc.com/product-detail/C5190383.html)；产品页补查 |
| C2682616 | U10 | MAX17048G+T10 | TDFN-8_L2.0-W2.0-P0.50-BL-EP1.2 | [供应商](https://www.lcsc.com/product-detail/C2682616.html)；产品页补查 |
| C2856798 | J2 | FPC-05F-10PH20 | FPC-SMD_10P-P0.50_XUNPU_FPC-05F-10PH20 | [供应商](https://www.lcsc.com/product-detail/C2856798.html)；产品页补查 |
| C131337 | J3 | B2B-PH-K-S(LF)(SN) | CONN-TH_B2B-PH-K-S | [供应商](https://www.lcsc.com/product-detail/C131337.html)；库精确命中 |

## 保留的放行要求

实际下单时仍要核对30料号、68件、所选标准型SMT的库存/代采及装配预览，不能沿用9月库存或把公开页面缓存当锁料。J1为混合USB座应保留表贴信号脚与外壳固定脚的装配，J3为单独通孔焊接。P2仍留有J2不装配、J3/J4焊线盘等过期文字，应在下次发布源文件时修正；本轮报告明确当前J2/J3均装配且无J4探头，避免旧注释指导生产。
