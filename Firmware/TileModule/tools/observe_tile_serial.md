# 格子串口与离线证据采集

`observe_tile_serial.py` 把格子日志转换为逐行 JSONL，保留全部原行。
必须显式选择 `--input` 或 `--port`；无参数只显示用法，不打开串口。
离线模式只依赖 Python 标准库，live 模式另需 pyserial。

## 离线转换

```powershell
python Firmware/TileModule/tools/observe_tile_serial.py --input C:/capture/serial-v028.log --output C:/capture/serial-v028-events.jsonl
```

支持原先 `ISO时间戳 日志文本` 格式、无时间戳原始文本，以及本工具已生成的 JSONL。
无时区/无时间戳行仍保留，但 `epochMs=null`；不会用当前解析时间替代历史时间。
输出文件独占创建，已有同名文件会报错，避免覆盖原始证据；不指定 `--output` 则输出到 stdout。

## 下一次已协调的设备窗口

只有下面显式 live 调用才会打开所指定端口：

```powershell
python Firmware/TileModule/tools/observe_tile_serial.py --port COM6 --duration 120 --status-interval 15 --output C:/capture/tile-live.jsonl
```

默认 `--status-interval 0` 是被动采集，不写串口。显式指定非零周期时仅发送 `STATUS`。
端口以 115200 打开一次；DTR/RTS 在打开前均设 false，结束、异常或 Ctrl+C 后关闭。
工具不发送重启、RFID 场控制、Tag 写入或游戏命令，也不连接服务器。
历史 .NET 采集中曾观察到 USB reset；设置 DTR/RTS=false 不能保证所有驱动/硬件都不复位，
所以日志会单独解析 boot/reset，不能在出现复位时继续声称采集完全无扰动。

live 按实际换行组合串口碎片；时间记录于主机读到该行最后字节的那次 read。
65536 字节上限或结束时尚未换行的片段以 `lineComplete=false` 保留，不当作完整 UID 集合。
这是主机接收时间，可能受串口缓冲和线程调度影响，不是 RFID 物理识别瞬间。

## 与服务器 HTTP 观测对齐

与 `Server/RaspberryPi/tools/observe-movement-cue.py` 共用 `epochMs`：

| 字段 | 基准与含义 |
| --- | --- |
| `epochMs` | 采集主机接收该串口行时的 UTC Unix 整数毫秒；离线时来自原记录 |
| `timeSource` | `host_receive`、`recorded_host_receive` 或 `unavailable`；工具自身启停为 `collector_event` |
| `recordedHostTime` | 原记录携带的 ISO 时间，含原始时区偏移；无则 null |
| `device_uptime_ms` | 设备日志 `t=...ms`；设备开机计时，非 UTC、非服务器时间 |
| `rawLine` / `message` | 完整原行 / 去掉主机时间前缀后的设备文本 |
| `event` | tag_inventory、tag_status、movement_cue、reset、boot、fault、reader_diagnostic 等 |
| `uids` / `tagCount` / `tagOverflow` | 当前这条记录明确报告的 UID 数组、数量、溢出标志；不从其他行继承 |
| `inventoryComplete` | 本行完整且明确报告 PRESENT/NO_TAG，数量与去重 UID 集合匹配、未溢出 |
| `field_off` | 设备本行报告的 PASS/FAIL；不是主机测量结果 |
| `cue` / `playerId` / `movementRevision` | 本行明确报告的 movement cue；缺失不自动填上一帧 |
| `fault` | 本行明确包含 fault/ERROR/关场失败等指示；保留原事件类型与原行 |

建议两个采集工具同时在同一主机运行，服务器工具通过 `--base-url` 读取 Pi。
如果分开在 Windows 与 Pi 运行，应先记录时钟偏差，不能直接假设不同主机的 UTC 绝对同步。
HTTP 观测是多次 GET，须使用其 `sampleStartedEpochMs`、`sampleSpanMs`、
`requests[path].receivedEpochMs` 与 `snapshotAtomic=false` 判断观察区间。
服务端 `updatedAtMs/lastSeenMs` 是服务器 epoch 毫秒；仍须区分服务器与采集主机时钟。

不要把设备 uptime 加到文件处理时间上构造事件时间，也不要跨 reset/计数回绕推算连续时间。
本工具不推断 UID 已被服务器收到，不把 cue 日志当作已目视看到 LED，不自动认定到达成功。
下一次联合验收应同时保留串口 JSONL 与包含完整 Tag/绑定/闸门的 HTTP JSONL。

## 离线测试

```powershell
python Firmware/TileModule/tools/test_observe_tile_serial.py
```

测试使用保存的事件文本和假串口，不打开 COM6，不发 HTTP 请求。
覆盖历史时间/uptime 分离、缺失时区、完整多标签/溢出/损坏、reset/fault/cue、JSONL重放、
默认不采集、输出不覆盖、live碎片拼接、显式STATUS与异常退出时句柄清理。
