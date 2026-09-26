# 32 格子 / 4 玩家屏批量烧录

仅支持当前 ESP32-S3、16 MB Flash 的 `tile-long`、`tile-corner` 和 `player` 三种普通固件。
长边与角落使用不同构建 profile；当前两种 PCB 没有已确认的角色识别引脚，清单中的板型须与实物对应。不能根据 COM 编号推断板型。

## 准备候选发布包

三个候选 manifest 必须包含 schemaVersion、profile、chip、flashSize、flashMode、flashFrequency、完整 gitRevision、sourceDigest、sourceManifest 和 segments（offset/path/sha256），并明确标记 `normalFirmware: true`；拒绝诊断或未标明类型的候选。
`sourceManifest` 是文件名与 SHA 的源码清单；打包校验 sourceDigest，并复制为各 profile 下的 `source-manifest.json`。只复制清单，不读取或复制清单所指向的源码、Wi-Fi/SSH 凭据文件。
含未提交修改的构建仍是候选；Git HEAD 不能替代实际源码清单，也不能据此标记为已提交的正式发布。

```powershell
python Tools/release/prepare_release.py --candidate C:/build/tile-long/candidate.json --candidate C:/build/tile-corner/candidate.json --candidate C:/build/player/candidate.json --output C:/GridopolyRelease/32-board-candidate
```

输出目录必须不存在，工具不会覆盖旧发布包。二进制和源码清单先读入内存校验，再将这份数据写入包内。所有运行脚本随包复制；二进制、源码清单路径均改为相对路径。
固件可能含本地 Wi-Fi 配置，发布包和设备清单存本地，不加入 Git。

## 安装固定刷写依赖

使用 Python 3.11 或更新版本；实际操作只接受 `esptool==5.3.1`，不再支持 `--esptool exe` 或多次独立 CLI 连接。

```powershell
python -m venv C:/GridopolyRelease/venv
C:/GridopolyRelease/venv/Scripts/python.exe -m pip install esptool==5.3.1
```

工具使用此版本的 `ESP32S3ROM` 上下文和公共写入/校验 API。此版本没有较新文档中的 `connect_esp` 便利接口。

## 实物清单与执行

复制包中的 `inventory-template.csv`，填写 `label,port,mac,profile`。模板含 28 长边、4 角落和4玩家屏，MAC/端口留空，必须填写真实完整出厂 MAC。
可使用设备标签、已知 USB 序列或有界读 MAC 操作；读 MAC 本身可能复位设备。MAC 校验不能辨认实物形状，板型填反需要修正清单重新烧录。

先检查而不打开串口，无需安装 esptool：

```powershell
python batch_flash.py release.json inventory.csv
```

通常分小批连接 USB，将本批清单另存为 `batch.csv`，显式加 `--partial`：

```powershell
python batch_flash.py release.json batch.csv --partial --execute --python C:/GridopolyRelease/venv/Scripts/python.exe --log C:/GridopolyRelease/flash-log.jsonl
```

默认仅 dry-run。实际执行分两步：

1. 全批先逐板核对完整 MAC、16 MB Flash 与已有分区布局；全部通过后才开始写入。这一步会复位设备，但不写 Flash。
2. 每块板重新进行完整校验；在同一串口上下文内完成 MAC 核对、写入、Flash 校验和再次核对 MAC，最后复位。固件读取为不可变 bytes，SHA 检查、写入和校验使用同一份数据，不再重新打开二进制文件。

esptool 5.3.1 内部的自动断线重连已禁用（单次 WRITE_FLASH_ATTEMPTS），拔线导致本次失败，不会重新打开可能已绑定到其他板的 COM 口。
每个操作由独立 worker 进程执行，最多 240 秒；超时会终止该进程并停止整批后续操作。错误 MAC、版本、分区、写入、校验、复位或返回结果异常均不记录 VERIFIED。

失败或断电可能使当前板停在下载模式或只写入部分固件；工具不会自动重试，也不声称能回滚固件。检查设备后可重新执行明确清单。JSONL 日志记录每次操作的设备、profile、源码与二进制校验值；不能仅累计 VERIFIED 行数推断36块不同设备全部完成。

## 配置分区保护

校验 ESP 分区表的32字节条目、MD5、范围和重叠关系，只允许当前包的四个写入地址：`0x0` bootloader、`0x8000` 分区表、`0xe000` otadata、`0x10000` app0。
bootloader、表、otadata 和 app 都必须在各自边界内；NVS、文件系统和其它数据分区不能成为写入段。工具不执行 erase-all 或网络配置写入。

首次使用的分区表区域必须全为 FF，或已有分区布局与候选完全一致。旧布局损坏、无法读取或与候选不同会在写入前拒绝，交给单独的备份/迁移流程；普通批量工具不迁移配置。长边与角落布局相同可互换对应固件，玩家屏与格子布局不同不能直接互刷。
`otadata` 是显式更新段，因此会重新选择包中的 app0，不承诺保留旧 OTA 启动选择。

VERIFIED 只表示身份核对、写入、Flash 校验及复位命令成功，不证明普通固件已联网启动、整盘游戏、功耗或屏幕视觉验收通过。烧录后仍需在树莓派面板核对模块角色、在线屏数、闭环顺序，并进行真实 Tag → 外貌 → 姓名 → 对局验收。

## 工具回归

```powershell
python -B -m unittest discover -s Tools/release -p 'test_*.py' -v
```

回归使用临时文件与模拟设备，不打开串口。安装了固定 esptool 的解释器还会核对实际 API 签名；存在本机 Tile 构建产物时会只读验证真实分区表。没有这些本地依赖时，对应测试明确跳过。
