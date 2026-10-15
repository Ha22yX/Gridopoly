# 玩家触摸屏 UART GPIO 的厂家证据

> 2026-10-15现行效力：下文保留9/27厂家图纸与固定提交的证据。TX43/RX44现已在[板型配置](../../Firmware/PlayerConsole/battery_board_config.h)显式实现，[UART适配](../../Firmware/PlayerConsole/battery_hardware.cpp)在启用板型时将UART1映射到RX44/TX43、9600 8N1；USB仍承担日志/烧录。默认构建及当前四台玩家屏仍为`BatteryBoard=none`，表示遥测关闭，不表示没有物理电池。UART端到端、桥接查询、电压/SOC与充电状态仍未完成实物验收；后续须基于最新输入/显示修复构建启用候选，不直接部署旧启用版。见[遥测开发记录](../firmware/player-battery-telemetry-progress-2026-09-27.md)及[当前交接](../CURRENT-DEVELOPMENT-HANDOFF.md)。

核对日期：2026-09-27。适用资料：VIEWESMART **UEDX48480021-MD80ET** 触摸版，V1.1规格书与V1.0原理图。结论是该资料对应的板型使用 **屏幕TX=GPIO43、屏幕RX=GPIO44**。这是厂家图纸与乐鑫模块引脚表的交叉核对，尚不等于用户实物已测通。

## 证据链

1. 厂家 [V1.1 SPEC.pdf](https://github.com/VIEWESMART/UEDX48480021-MD80ESP32-2.1inch-Touch-Knob-Display/blob/830a74e6354aa482d382401bdb222a5efe82c917/information/UEDX48480021-MD80ET%20V1.1%20SPEC.pdf)，**第6/13页，2.1 Hardware Description，USB power supply and burning interface表**：外部接口6=RX、7=TX，电平0–3.3V；接口10Pin、0.5mm。表中ADC为GPIO4；不能沿用非触摸网页的GPIO3描述。
2. 厂家 [UEDX48480021-MD80ET-V1.0 SCH_00.png](https://github.com/VIEWESMART/UEDX48480021-MD80ESP32-2.1inch-Touch-Knob-Display/blob/830a74e6354aa482d382401bdb222a5efe82c917/Schematic/UEDX48480021-MD80ET-V1.0%20SCH_00.png)，单张原理图：**上部U2（S3-W）右侧**，37脚TXD经R2连接UARTTX，36脚RXD连接UARTRX；**下部J2（CON12）** 的6脚标UARTRX，7脚标UARTTX。查图应按位号/网络名定位，不从旁边J1的另一套编号推断外接FPC。
3. 乐鑫 [ESP32-S3-WROOM-1/WROOM-1U Datasheet v1.8](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)，**第12页，Table3-1续表**：模块37脚TXD0的功能为U0TXD/GPIO43；模块36脚RXD0为U0RXD/GPIO44。

厂家两份资料固定到仓库提交 `830a74e6354aa482d382401bdb222a5efe82c917`。本次读到的原图SHA-256为 `229d1d14c30f75303b1438483bf3273b930767c38d8bfb8ebed3c14706f3a701`；规格书为 `2e310b8f5349567bb515482b7c6279b9f0b265a605c45e6e54f70a8dc9125bf4`。第三方完整原图/PDF不拷入本次Git提交，保留上游固定链接及核对定位。

## 本底板的对应关系

| 方向 | 屏幕内部 | 厂家外部接口 | 底板 |
| --- | --- | --- | --- |
| 屏幕发送 | GPIO43 / U0TXD | 屏幕逻辑FPC7，TX | 底板J2.4 → U9.10，SC18IM704 RX |
| 屏幕接收 | GPIO44 / U0RXD | 屏幕逻辑FPC6，RX | U9.11，SC18IM704 TX → 底板J2.5 |

固件可为此明确板型提供TX43/RX44配置。当前部署设备是否就是上述版本、GPIO是否已被其他功能占用以及实物排线通断尚未验收，不能因此自动开启未知板型或直接烧录现场设备。上电ROM日志可能出现在UART0；应用日志应留在USB，桥接查询需完成空闲间隔、缓冲清理、超时设置和版本握手。

原图J2用CON12符号、规格书外部表为10Pin；这不改变表中6/7的逻辑信号对应，但不能据此冻结母座机械焊盘编号。用户确认的金手指左右方向仍保留，FPC尾端总厚度、母座接触面/载流和端到端测通仍待实物核对。用户随后明确要求将底板J2的1–10脚网络反转，当前原理图/PCB已按此修改，屏幕内部GPIO定义不变。本文没有解除机械实测及制造限制。
