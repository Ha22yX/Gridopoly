# 角落模块由 ESP32-S3 配置 STUSB4500（2026-09-21）

> 2026-09-22效力说明：本文保留原阶段证据；其中“当前/待实现”仅指当时。现行板为长边PCB2_1、角落PCB2_5，角落直接24V，PD电路、NVM及旧218器件布局不再作为交付。两板支路保护保留，以[供电复核](power-schematic-audit-2026-09-21.md)为准。当前采购/开发入口见[硬件索引](README.md)和[汇报](development-handoff-2026-09-22.md)。

> 2026-09-21 最新变更：用户已取消角落 PD→24V，改为 5.5×2.1mm 中心正极的直接24V输入；见[替代设计](corner-dc24-input-2026-09-21.md)。本文保留为历史记录，PD电路/固件/功率计算不再适用于当前板。

已修改角落原理图 ae9c8047ce85d7f8。此记录替代先前“必须通过独立 J101 编程”的生产流程。PCB 尚未同步，NVM 写入固件尚未实现；当前完成的是可供后续固件使用的硬件连接。

## 连接与器件变化

| 功能 | 最终连接 |
|---|---|
| PD SCL | U101.7 → CURRENT_SCL → U3.19 / GPIO11 |
| PD SDA | U101.8 → CURRENT_SDA → U3.20 / GPIO12 |
| PD RESET | U101.6 → PD_RST → U3.21 / GPIO13，高电平有效 |
| 系统供电 | U101.22 VSYS 从 GND 改接 3V3_SYS |
| VSYS 去耦 | 新增 C128，100nF / 50V / X7R / 0603，C14663 |
| I²C 上拉 | 复用原有 R32、R33，各 4.7kΩ 到 3V3_SYS |
| I²C 地址 | STUSB4500 为 0x28，INA226 为 0x40，无地址冲突 |

删除 J101、R108、R109；新增 C128，总数减少 2。GPIO13 是空闲普通 GPIO，不使用 GPIO0/3/45/46 启动绑带，也不使用 N16R8 模组占用的 GPIO35/36/37。原 R107 10k 下拉和 C106 100n 复位网络保留，ESP32 尚未初始化时 PD 不被持续复位。

C128 的国内商城库存查询为 9,971,250，满足至少 100；见 added-part-stock.json。库存未预留。PCB 上 C128 要紧邻 U101 VSYS/GND，原理图的参数区位置不代表 PCB 位置。

## 首次配置不需要专用下载器

1. 首次使用板上已有 J1 USB 调试口接电脑，下载未来包含 PD 配置功能的 ESP32 固件。
2. J1 的 5V 经现有 U7 → TPS2121 → 5V_SELECTED → 5V_IN → 3.3V 电源启动 ESP32；同时由 3V3_SYS 给 STUSB4500 VSYS 供电。不依赖 USB1 先协商 20V，首次写入可保持 USB1 未连接。
3. ESP32 通过 I²C 写入所需 NVM，并逐字节读回验证。复位后再次读取有效配置；成功后断开 J1。
4. 日常仅 USB1 PD 供电口接合格 20V/5A 电源，STUSB4500 自动协商，随后开启 24V 总线。

这是“通过已有 USB 调试口给 ESP32 下载程序，再由 ESP32 配置 PD”，不是未经配置就仅插 USB1 必然能够首次自启动。J1 与 USB1 是两个不同接口；此方案不增加额外的高压转低压启动电源。

STUSB4500 在 VDD 和 VSYS 都没有供电时会拉低 SCL/SDA，因此仅接通信线而仍让 VSYS 接地会影响共享 I²C。将 VSYS 接到主控的 3.3V，使主控工作时 PD 芯片也得到供电，是此次连接的一部分。

## 后续固件要求

- 沿用 Wire.begin(SDA=12,SCL=11)，推荐首次配置 100kHz；共享总线访问与 INA226 轮询串行化，禁止两个任务同时进行事务。
- GPIO13 初始输出低，普通开机不得无条件复位 PD。写入/复位 PD 可能中断其供电合同，所以配置必须在 J1 稳定供电的维护模式进行；不在整盘由 USB1 供电时自动改 NVM。
- 首次下载/配置模式保持 LED、屏幕背光、RFID 等大负载关闭，以免在 USB 调试口启动时超出预算。
- 按官方 STSW-STUSB004 的 NVM 操作序列与选定的配置镜像实现，先比较目标配置；相同则跳过，禁止每次开机擦写。检查 ACK、超时、写入结果、完整读回及复位后配置，失败不得报告成功。
- RESET 为高有效，可用 10ms 高脉冲后恢复低，等待至少 50ms 再访问；最终参数须依据对应数据手册与实测。写入时保持 J1 稳定供电。
- 不要单纯把 PDO 改成 20V/5A 就忽略先前电源设计要求的 POWER_OK2、供电门控、PDO 数量及 NVM 其他字段。目标镜像需要单独核对后固化。

## 验证

网表比较仅出现预期的 15 个引脚增删/换网：删除 J101 5 个引脚及两颗电阻 4 个引脚，U101 的 SCL/SDA/VSYS 换网，新增 GPIO13 RESET 和 C128 两引脚。其余原有连接保持不变。最终角落已连接引脚 685；长边网表未改变。

GPIO13 的 NC 已取消，U3 其余 15 个原 NC 标记保留。原理图 DRC 返回 2 条警告（原属性匹配类别未继续处理），未新增错误计数。导出后检查了主控与 PD 部分的图形。未修改 PCB，未下载固件，未做硬件写入测试。

## 依据

- [STUSB4500 数据手册](https://www.st.com/resource/en/datasheet/stusb4500.pdf)：I²C、RESET、VSYS、地址及掉电行为。
- [ST 官方对 VSYS 供电进行首次 NVM 编程的确认](https://community.st.com/interface-and-connectivity-ics-52/stusb4500-pd-controller-hardware-constraints-for-battery-charging-46406)。
- [STSW-STUSB004 NVM 配置库](https://www.st.com/resource/en/product_presentation/stsw-stusb004_quickstart.pdf)。
- [ESP32-S3-WROOM-1 模组手册](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)。
