# 角落 COM19 白灯：独立证据包

2026-10-15，仅保存本次只读核查证据，不修改其他工程或归档。

结论和后续责任见[独立问题记录](../../../Docs/hardware/corner-led-white-2026-10-15.md)。原故障板 COM19 / `a4:cb:8f:c6:26:24` 断电隔离；COM10 / `a4:cb:8f:c1:e1:34` 为另一块替换板，不能互相代替验证。

- `WS2812B-B-V6-manufacturer.pdf`：准确型号的 Worldsemi 原厂手册，来自立创托管；关键时序在第 4 页。
- `WS2812B-B-V6-page4-timing.png`：已视觉核查的时序页。
- `archived-led-power-extract.json`：角落、长边及 DC24 修复原理图的相关器件属性与逐脚摘录；不是实装 BOM。
- `telemetry-extract.json`：从既有串口日志提取 POWER 行、时间戳及范围；无新的硬件采集，不视为校准测量。
- `batch02-COM19-white-led-capture.json`、`batch02-corner-identity.json`：原采集及身份元数据副本。
- `manifest.json`：原输入路径、SHA-256、来源、状态和边界。
- `SHA256SUMS.txt`：本目录其他证据文件校验值。

HCT125 厂家 PDF 已通过网页工具阅读，但直接本地下载被 403 拒绝，本包未包含该文件；链接和页码保留在记录及 manifest 中。没有伪造本地副本。
