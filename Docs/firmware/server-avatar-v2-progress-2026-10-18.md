# Server avatar V2 implementation progress — 2026-10-18

Implementation contract, 2026-10-18. This document does not claim deployment or
acceptance of the production artwork. V1 remains readable with its original math
and immutable assets. V2 artwork is owned by the main project task.

## Identity and storage

The existing identity request/snapshot wire sizes and persistence layout remain
unchanged. `avatarCatalogVersion` accepts 1 and 2; other nonzero versions are
rejected. A confirmed seat retains its own recipe version even when a room uses
a different catalog. Unset recipes remain all-zero on the wire.

`GRIDOPOLY_AVATAR_CATALOG_VERSION=1|2` selects the catalog for new rooms and
compatibility identity initialization. The default is 1. Selecting 2 requires a
complete valid V2 component directory at initialization; there is no silent
fallback. Restoring a room preserves its catalog, confirmed recipes, revisions,
and content hashes. Changing this setting does not migrate an existing room.

| Purpose | V1 | V2 |
|---|---|---|
| Component environment | `GRIDOPOLY_AVATAR_COMPONENT_ROOT` | `GRIDOPOLY_AVATAR_COMPONENT_V2_ROOT` |
| Default component root | `/usr/local/share/gridopoly/avatar/components-v1` | `/usr/local/share/gridopoly/avatar/components-v2` |
| Component URL | `/assets/avatar-components/v1/<kind>/<id>.gavc` | `/assets/avatar-components/v2/<kind>/<id>.gavc` |
| Preview URL | `/assets/avatar-previews/v1/hH-cC-fF-sS-oO.rgb565` | `/assets/avatar-previews/v2/hH-cC-fF-sS-oO.rgb565` |
| Preview disk directory | `<asset-root>/avatar-previews/gavc-v1` | `<asset-root>/avatar-previews/gavc-v2` |
| Final URL | `/assets/avatars/<room>/pP-aR-HASH.ext` | `/assets/avatars/v2/<room>/pP-aR-HASH.ext` |
| Final disk directory | `<asset-root>/avatars/<room>` | `<asset-root>/avatars/v2/<room>` |

Explicit `/assets/avatars/v1/<room>/...` is also accepted as an alias for the
legacy V1 final files. `ext` is `png` or `rgb565`; `HASH` remains the existing
16-digit lowercase content hash. Versioned directory paths separate the caches
even when identical pixels happen to produce the same content hash. Old final
files remain addressable. URLs are origin-relative in Web state projection.

## GAVC V2

The 32-byte little-endian header retains the offsets specified by
[the V1 contract](avatar-component-protocol.md). Schema and encoding are both 2.
Each decoded sample is five bytes: `R, G, B, A, skinMask`.
`decodedBytes = width * height * 5`; the CRC covers all five original bytes per
sample. RLE tokens still count samples: a repeat token carries one five-byte
sample, and a literal token carries `count * 5` bytes. Trailing data, overflow,
zero tokens, wrong lengths/CRC, wrong catalog/schema/encoding, and path/header
identity disagreement are rejected.

The canvas remains 220x300. IDs retain their meaning:

- face kind 1: `face/f1.gavc` through `f10.gavc`;
- outfit kind 2: `outfit/o1.gavc` through `o10.gavc`;
- hair kind 3: `hair/h1.gavc` through `h10.gavc`;
- neck kind 4: only `neck/n1.gavc`, shared by every V2 recipe.

V2 composes neck, face, outfit, then hair using the existing integer straight-alpha
source-over operation. The neck is not a new selectable recipe field. V1 has no
neck component and retains face, outfit, hair order.

## Explicit skin material

Only V2 face and neck use the mask. For neutral gray skin material `R=G=B`, read
`shade=R` once before changing any channel. With target skin channel `t`, original
channel `c`, and mask `m`:

```
roundHalfUp(n, d) = floor((n + floor(d / 2)) / d)
tinted = min(255, roundHalfUp(t * shade, 178))
output = roundHalfUp(tinted * m + c * (255 - m), 255)
```

Mask zero preserves RGB, 255 fully applies the skin material, and intermediate
values blend the two. Alpha is unchanged. Transparent pixels are a no-op.
Hair retains the exact V1 integer hair algorithm; outfits are not recolored.
V1 face tinting retains the old classifier. The 20 hair colors and 8 skin tones
retain their V1 IDs and RGB values.

Example Ebony vectors (target 91,53,42): neutral 178 with mask 255 yields
91,53,42; shade 255 yields 130,76,60; shade 178 with mask 128 yields 134,115,110.
These cases include partial alpha without changing it.

## Rendering, publication and rollback

Preview remains RGB565 little-endian, 132000 bytes. Final rendering embeds the
transparent composite into 320x320 at (50,10), downsamples to 128x128 with the
existing bilinear algorithm, applies the circular mask, then emits RGBA PNG and
32768-byte RGB565. A version is pinned by each recipe/job. Runtime component roots
are fixed for the process and their contents must remain immutable.

Use the release-specific tooling, for example:

```
python3 Server/RaspberryPi/tools/verify-avatar-components.py --catalog-version 2
python3 Server/RaspberryPi/tools/stage-avatar-components.py /empty/candidate --catalog-version 2
python3 Server/RaspberryPi/tools/stage-avatar-components.py /existing/release --catalog-version 2 --verify-existing
```

`--runtime-root` optionally selects an independent candidate runtime directory.
The manifest is `avatar-components-v2.json` with schema 2, entryCount 31, exact
file identities, lengths, geometry, file SHA-256, and `rgbaSha256` over the decoded
five-byte samples. That field name is retained for compatibility. The staged
release contains the 31 components plus an identical `manifest.json`.

Every component is at most 131072 bytes. Their combined compressed size must not
exceed 2095296 bytes, reserving two 132000-byte previews within the console's
2304 KiB setup pool. This bound does not account for every transient device
allocation; actual firmware memory and visual acceptance remain required.

`deploy/install-avatar-components.sh <1|2> [runtime-directory]` publishes only the
selected complete catalog under a filesystem lock. Identical reinstallation is
allowed; changed bytes under an existing version are rejected. Publication keeps
other catalogs and does not change service configuration, restart the server,
or touch game state. The general legacy installer is not the V2 publication path.

Rollback keeps both catalogs and generated final assets available. Restoring the
new-room default to 1 affects future rooms only. Existing V2 rooms still require
this dual-version binary and V2 assets; downgrading to a V1-only binary would need
a separate identity migration. Game progress must remain intact.

## Verification boundary

Synthetic host fixtures exercise five-byte literal/repeat decoding, mask CRC,
malformed headers/paths, Ebony tint vectors, four-layer composition, versioned
preview/final isolation, immutable staging, full-library limits, identity
round trips and restoration, plus HTTP bytes/ETag behavior. V1 golden vectors
remain unchanged. Production-art all-combination review, actual player-screen
rendering, firmware deployment and live acceptance are separate main-task gates.


## 本端完成记录与交接（2026-10-18）

本轮实际修改服务端和共享协议，未修改 Firmware/PlayerConsole、Assets、旧旋钮/电量实现或共享索引。玩家端协作任务负责其版本继承、31组件缓存和渲染生命周期。V1源素材、V1固定像素结果保持不变。

### 验证结果

- 本机Python：V1组件严格校验、V1暂存回归、V2合成31组件校验/暂存回归通过。V2测试包括五字节CRC、错误schema/encoding、缺少neck、越界、单组件128KiB与全库2095296字节上限、已发布目录不同字节拒绝重装。
- 独立Pi native目录：8组相关host回归通过：protocol、identity_model、avatar_renderer、avatar_component_codec、avatar_v2、player_avatar_component_client、identity_authority、http_asset_integration。HTTP组包括V1与V2资源字节/ETag/404、V2最终图目录隔离，以及原有deadline、开发素材、debug持久化/并发回归。
- V2身份回归确认：配置为2恢复旧房间仍为1且旧头像不变；新V2房间拒绝V1配方、接受并完成V2；配置改回1恢复已存在V2房间仍保留2，下一新房间才使用1；缺失V2组件时显式配置2初始化失败。
- 正常server候选构建通过：g++ 14.2.0，C++17/O2/Wall/Wextra/Werror/pthread，未定义NDEBUG。
- `install-avatar-components.sh` shell语法检查通过；本轮没有执行安装脚本。
- 首次编译发现IdentityModel缺少新版本判断函数的namespace导入，修复后全部8组重跑通过。首次失败证据保存在 `C:/Users/kicof/GridopolyReleases/server-avatar-v2-20261018-045344/native.log`，没有将首次失败标成通过。

### 冻结候选与构建证据

- 本地候选目录：`C:/Users/kicof/GridopolyReleases/server-avatar-v2-20261018-045441`。
- 服务器独立候选目录：`/home/kicofy/gridopoly-avatar-v2-20261018.UPUw4z`。
- `gridopoly_server`：645672 bytes，SHA-256 `00c978afdb9fa19923c2458754cb69fd6e13d7f9f9fb186555a331c826c6bf20`。
- 549项构建输入清单 `source-manifest.json`，SHA-256 `6de1626b28ccc0dbe5f322c437155f7a5fdce8799a1c860919b4585841f2fe7e`。构建完成时远端与工作区清单均无漂移。清单内 `Firmware/PlayerConsole/avatar_component_math.h` 来自玩家端任务，本端只读取并使用，不认领其修改。
- `native.log`、`build-result.json`、`remote-verification.json`、`workspace-verification.json` 保存回归与来源证据。
- 构建前后生产PID均1890195，生产二进制SHA均 `fb03d800d45bd8759775404d792dcd9df88dd32893db6dad265fc63f23c9c7f7`。该记录只证明生产进程/二进制在此检查前后相同，不声称游戏状态未自然推进。

### 真实素材三方像素对照

使用玩家端冻结快照中的31个实际GAVC（合计1362537 bytes），每个文件与捕获的V2 manifest SHA匹配。捕获manifest SHA-256为 `00adb88624d340d138bfd55039e2463e03d3171ddb40e4b942b95ca700499df2`。后续主会话仅更新manifest审查元数据时，应对照31组件SHA清单；这些像素结果绑定组件字节，不将审查元数据变化误报为像素变化。

服务端真实C++解码/合成/AvatarRenderer、资产工具独立Python整数合成、玩家生产compositor均参与比较。两组配方的264000字节RGBA和132000字节铺底RGB565与Python逐字节零差异；透明区归一到0x07E0后，与玩家屏生产compositor的132000字节结果也逐字节零差异。128x128最终PNG/RGB565同时导出。

| 配方 | 服务端铺底RGB565 SHA-256 | 玩家/服务端色键RGB565 SHA-256 |
|---|---|---|
| H08/C01/F07/S08/O02 Ebony | `6a6d71d07e499c44ae2d20e38855604940411931e18ca967c9ecb4d50bdbc9a3` | `db280232c1afce91479d34078fa8f2c60a690a25ac2adafd2b1058c0c1ab49bb` |
| H01/C01/F10/S01/O07 Porcelain | `04b54fb43a2fec66cb65ef8a15404424fb3007d605c7761ac5a01783afe618fb` | `9d5b7a6de532319c0ed2d4edc6cd110735ed609f2c4ddcae48c1984282bd673a` |

证据在候选目录：`real-art-runtime/`、`real-art-source-manifest.json`、`real-art-native.log`、`real-art-output/`、`real-art-comparison.json`。比较报告SHA-256 `968c23dcb0c90da5e35eeab1b692dddc64b0369f95af5339ca66a63607be09a7`。运行检查采用独立TMPDIR与测试状态，未调用线上游戏接口。

### 准确修改文件范围

- `Firmware/libraries/GridopolyProtocol/src/gridopoly/protocol/Protocol.h`
- `Firmware/libraries/GridopolyProtocol/src/gridopoly/protocol/Protocol.cpp`
- `Server/RaspberryPi/src/AvatarComponentCodec.h`
- `Server/RaspberryPi/src/AvatarComponentCodec.cpp`
- `Server/RaspberryPi/src/AvatarRenderer.h`
- `Server/RaspberryPi/src/AvatarRenderer.cpp`
- `Server/RaspberryPi/src/IdentityModel.h`
- `Server/RaspberryPi/src/IdentityModel.cpp`
- `Server/RaspberryPi/src/AuthorityService.h`
- `Server/RaspberryPi/src/AuthorityService.cpp`
- `Server/RaspberryPi/src/HttpServer.cpp`
- `Server/RaspberryPi/src/main.cpp`
- `Server/RaspberryPi/tools/avatar_component_release.py`
- `Server/RaspberryPi/tools/verify-avatar-components.py`
- `Server/RaspberryPi/tools/stage-avatar-components.py`
- `Server/RaspberryPi/tools/test-avatar-components-v2.py`
- `Server/RaspberryPi/tools/build-and-test-native.sh`
- `Server/RaspberryPi/deploy/install-avatar-components.sh`
- `Server/RaspberryPi/deploy/server.env.example`
- `tests/host/avatar_v2_fixture.h`
- `tests/host/avatar_v2_tests.cpp`
- `tests/host/avatar_v2_artwork_tests.cpp`
- `tests/host/identity_authority_tests.cpp`
- `tests/host/http_asset_integration_tests.cpp`
- `Docs/firmware/server-avatar-v2-progress-2026-10-18.md`

### 当前边界及下一步

本端源码实现、相关host回归、正常候选编译和两组真实素材三方字节对照已完成。没有部署Pi、重启生产服务、烧录、操作串口、修改线上游戏或暂存/提交Git。当前线上版本仍是前述fb03d800；新候选尚未上线，默认新房间catalog仍为1，显式切换配置才会为新房间选择2。

主会话负责最终美术/浏览器审查与acceptance冻结、全局handoff和协议文档，以及后续是否启用/部署。玩家任务负责玩家源码和实机显示验收。本轮两配方一致性检查不替代全部视觉组合、真实显示屏观感或部署验收。本端没有阻塞；文件交由主会话统一审核和Git管理。


## 最终 hair 修订复核（2026-10-18，追加记录）

用户重新指出 H06/H08 太宽和 H10 贴脸问题后，主会话重开美术验收并修订这3个hair组件。上文 **1,362,537 bytes / 20261018-045441** 的真实素材对照保留为历史候选证据，不能作为此次修订后素材的验收结果；上文服务端代码回归和编译证据仍对应同一代码实现。

本轮按主会话要求只复用现成 C++ 真实素材检查目标，在新的独立输入/输出目录执行原有两配方。未修改服务端源码，未重新编译C++或重复8组代码测试，未部署、烧录、重启服务或操作线上游戏。31个GAVC按前后SHA检查后复制为独立快照；合计 **1,353,679 bytes**，与历史候选相比仅 `hair/h6.gavc`、`hair/h8.gavc`、`hair/h10.gavc` 改变，其余28文件完全相同。

- 本轮证据目录：`C:/Users/kicof/GridopolyReleases/server-avatar-v2-final-hair-20261018-check1`。
- 复用目标：`/home/kicofy/gridopoly-avatar-v2-20261018.UPUw4z/build-pi-native/gridopoly_avatar_v2_artwork_tests`；执行前后SHA-256均 `004ddd12ac072b3f0920a7e04a3211c14c16a483adcfecf06457ec5012c0ff9f`。
- 捕获manifest SHA-256：`441d37f6a34db86a41c5d35e4b0cc78ac6bb7a9de75b39ee1ac8bf3b528ba96e`；此值记录捕获时的审查元数据，后续acceptance元数据冻结与31组件像素SHA分开记录。
- `real-art-comparison.json` 保存31组件逐文件SHA、仅3hair变化、Python源快照、目标二进制、输出与逐配方对照；`real-art-native.log` 保存两个实际C++ PASS。

两组服务端与Python的原始220x300 RGBA各264000 bytes、铺底RGB565各132000 bytes，均逐字节零差异；128x128最终PNG/RGB565同时导出。配方仍为 H08/C01/F07/S08/O02 Ebony 和 H01/C01/F10/S01/O07 Porcelain，不扩展为对H06/H10的新增视觉验收。

| 配方 | 服务端RGBA SHA-256 | 服务端铺底RGB565 SHA-256 | 服务端色键RGB565 SHA-256 |
|---|---|---|---|
| h8-c1-f7-s8-o2 | `7e5cf162b6e3b81fa45bd2ef107e03278b954fe3a1bc6a956b57ece14b245638` | `1cb24e3bb888c08b945518607a9889638673f37c806c8ced6c5d61a6e8f574e5` | `96cb7014b4708d1b9859dcb3a34e754456fff3506723c388248cf178958e953e` |
| h1-c1-f10-s1-o7 | `f006f27bffd2986c60635c4dfa887e1bed59ed0f2c8233ed47fc634cc5e8e523` | `04b54fb43a2fec66cb65ef8a15404424fb3007d605c7761ac5a01783afe618fb` | `9d5b7a6de532319c0ed2d4edc6cd110735ed609f2c4ddcae48c1984282bd673a` |

玩家端最终同包输出仍待其当前普通固件编译完成后生成；服务端已将本轮独立输出和组件SHA发给玩家任务。三方最终对照状态待下面续记。


### 最终 hair 三方对照闭环

玩家最终快照已交接：`C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/avatar-v2-final-hair-20261018/`。本端重新读取其31个GAVC文件并与服务端本轮快照逐SHA核对，全部相同；随后读取两份实际玩家生产compositor输出，和服务端透明区规范化为0x07E0的RGB565逐字节比较。两组各132000 bytes，**均0差异**。这完成了本轮最终hair包的服务端/Python/玩家三方像素对照。

- H08/C01/F07/S08/O02 Ebony：玩家与服务端色键输出SHA-256均 `96cb7014b4708d1b9859dcb3a34e754456fff3506723c388248cf178958e953e`。
- H01/C01/F10/S01/O07 Porcelain：玩家与服务端色键输出SHA-256均 `9d5b7a6de532319c0ed2d4edc6cd110735ed609f2c4ddcae48c1984282bd673a`，控制组保持历史结果。
- 本端 `real-art-comparison.json` 状态现为 `three_way_pass`，每组记录三项字节差异均为0。玩家另行保存了 `independent-final-comparison.json`，记录从服务端原RGBA/铺底RGB565独立归一化的反向复核。
- 本端最终比较报告SHA-256：`c9efab93d6419f179457e99ac9385fde2d2123afb1472c40c18955bf521cca39`。
- 玩家独立比较报告SHA-256：`8fd362207f6f4c5680e4c931bb09c7da2621f96511e299fb18ab1631e749306f`。

本轮唯一仓库修改是本进度报告的追加；本轮文件SHA另存到最终hair证据目录的 `handoff-files.json`。原1,362,537字节历史候选及其所有证据未覆盖。服务器源码和已交付 `00c978af...` 正常候选未修改或重编译，8组代码回归未重复；没有部署或操作线上状态。

玩家任务另报告首轮ESP32普通固件编译遇编译器内部错误，并继续处理等价修复、用户新增RANDOM按钮/NameReview问题；这些属于玩家端后续构建事项。本轮字节对照通过不宣称玩家固件已经构建或实机验收通过。最终acceptance元数据、浏览器美术验收、统一交接及部署决策仍由主会话负责。本端素材对照任务无剩余阻塞，报告交由主会话审核。

## Pi 升级预检及待启用方案（2026-10-18 02:16 EDT）

主会话已通知本轮普通12MHz玩家候选为 `99f6a840…`，四台玩家屏由玩家任务逐台写验；本端先准备Pi升级，**须等主会话确认四屏写验完成才启用**。这段记录为预检/准备状态，不是部署完成记录。仅追加本报告，没有修改其他仓库文件、Git索引或现有网络改动。

### 只读生产现况

- 02:14:49和02:16:00 EDT两次只读检查：生产磁盘与运行进程均为 `fb03d800d45bd8759775404d792dcd9df88dd32893db6dad265fc63f23c9c7f7`，644832 bytes，PID1890195。游戏、AP、DHCP和watchdog均active/enabled，网络服务PID分别718/719/720。
- `/etc/gridopoly/server.env` 没有头像版本或组件目录覆盖项；旧服务默认V1。`components-v1`现有31文件（30组件及manifest），`components-v2`尚不存在。只记录env整体哈希及非敏感配置，未输出PSK或SSH凭据。
- 当前房间 `993580119`、游戏version33、16格棋盘、4真人0机器人，身份处于AvatarSetup。直接只读解析 `identity.bin` 并验证schema/长度/CRC，确认房间catalog=1、identity revision46。P1/P2/P3头像已确认、P4未确认，全部姓名未确认；没有正在生成的头像。
- 三个已确认头像分别为P1 revision1/hash `e5cf7fbc099456f0`、P2 revision4/hash `94dd3d786c3b5d51`、P3 revision3/hash `be3df1ace877e276`，配方均catalog1。原始配方、姓名、席位状态及文件哈希已保存到本地证据。
- 四台设备均online/eligible，席位与设备身份保留；配对complete、boundMask15。16格全部V0.39、online/assigned/reader stable，ORDER ready，四角0/4/8/12。
- 持久化原件均为0600：`state.bin` 1952 bytes/SHA `0fe482125e7bcad50ab6bb4e8358f9d17e7328a6906f4ed4c20bd8dccb8b8a95`；`identity.bin` 1730 bytes/SHA `32717ae3ac3e4c2735768962b332e2135588c6e619d33149a8001b117c93d660`；`authority.meta` 295 bytes/SHA `82e7381a3404dda6c40628b9048817c0762f536dc97aee7b6af06fc7101bec7c`；`device-seats.bin` 52 bytes/SHA `2837706e6c6faf053cfee4311e6fd683ec0dcc9ca8babd962ff1df1cad50c9e7`。这是现场时点快照，后续玩家操作可能自然更新。

证据：`C:/Users/kicof/AppData/Local/GridopolyAvatarV2Deploy/audit-20261018-061447/audit.json`、`audit-20261018-061558/audit.json`。目录可能由Windows应用映射至该应用LocalCache，但上述原路径可读取。

### 已冻结的升级载荷

重新对现成候选做文件哈希检查，未编译或重复回归。服务器645672 bytes/SHA `00c978afdb9fa19923c2458754cb69fd6e13d7f9f9fb186555a331c826c6bf20`；当前仓库、最终hair快照及本轮部署快照三处31组件逐文件相同，共 **1,353,679 bytes**，外部和内嵌manifest均为 `441d37f6a34db86a41c5d35e4b0cc78ac6bb7a9de75b39ee1ac8bf3b528ba96e`。

本地待部署载荷在 `C:/Users/kicof/AppData/Local/GridopolyAvatarV2Deploy/payload/`，逐文件清单为同目录上一级 `payload-manifest.json`。窄范围部署及回退程序 `apply_remote.py`、上传入口 `deploy.py` 已准备并通过Python语法解析；未执行部署入口、未上传、未写Pi或重启服务。

### 确认后的执行边界

1. 重新检查实际旧二进制、当前有效V1身份和配置，上传并逐项核验载荷。以目录锁及同文件系统原子重命名安装完整V2目录；如已存在不同字节，拒绝覆盖已发布版本。
2. 仅停止 `gridopoly` 游戏服务，让其正常落盘；在独立受限备份目录保存旧二进制、原env、全部游戏数据压缩包、停止后的身份及文件清单。AP、DHCP、watchdog不停止或重启。
3. 原子替换游戏二进制；仅向原env追加 `GRIDOPOLY_AVATAR_COMPONENT_V2_ROOT=/usr/local/share/gridopoly/avatar/components-v2` 和 `GRIDOPOLY_AVATAR_CATALOG_VERSION=2`，保留其他配置。启动游戏服务后检查实际进程二进制和生效的这两项配置。
4. 必要部署验证仅包括健康状态、31个V2组件HTTP返回字节、旧V1组件/已确认头像可访问、原房间catalog/身份保持、四屏及16格恢复、网络服务PID及配置/旧素材哈希保持。旧房间继续V1，下一次用户开新局才用V2；不自动开局或迁移已有配方。
5. 部署失败时，当前仍为有效V1房间可恢复旧二进制和原env，保留当前游戏数据及已安装不可变V2组件。若已经产生V2房间，拒绝直接回退不支持V2的旧服务；先保留数据并报告，由主会话确定后续恢复。不会用旧数据快照覆盖玩家已完成的操作。

当前本端准备完成，唯一启用前提是主会话明确通知四屏本轮写验完成。共享设备台账、全局交接及Git由主会话维护；新头像实屏观感、RANDOM和姓名页现场验收仍由主会话/玩家任务闭环。

## Pi V2 配套部署完成（2026-10-18 02:19–02:21 EDT）

主会话已独立核对COM28/27/29/7四份 `flash.json`，四屏均为 `99f6a840…`、各4段写验成功且NVS未写，COM7最后写验于06:18:58 UTC结束；随后明确通知本端启用已准备的窄范围部署。本端依此执行，未重编译或重复已通过的回归。

### 实际部署和只读验证

- 部署入口exit0/status `DEPLOYED`；实际运行PID **2086790**，`/proc/<pid>/exe` SHA-256 **`00c978afdb9fa19923c2458754cb69fd6e13d7f9f9fb186555a331c826c6bf20`**。只短停/启动 `gridopoly`。
- 最终31个V2组件 **1,353,679 bytes**，内嵌manifest SHA **`441d37f6a34db86a41c5d35e4b0cc78ac6bb7a9de75b39ee1ac8bf3b528ba96e`**，已安装到 `/usr/local/share/gridopoly/avatar/components-v2`。安装后逐文件及31个 `/assets/avatar-components/v2/…` HTTP响应SHA均与冻结清单相同，缓存头为不可变资源策略。
- 原env仅增加V2组件目录和 `GRIDOPOLY_AVATAR_CATALOG_VERSION=2` 两项；直接读取新进程环境确认两项已生效。旧env SHA `6a75fec645c2b68bd69a6b9babb3d73b0cc9435e20efe2b0c3b26214fbb4838c`，新env SHA `521c083e8036e91b87f9a6bb8f22325d4e0142ad24ba401b5f32a696fe616ca5`，其余字节保持。
- **当前房间仍为 `993580119` / catalog1**；停止后备份身份与恢复后身份除在线位和房间identity revision外完全相同。配方、席位revision、头像revision/hash、姓名和身份阶段均保持，业务字段差异为空。游戏version41→50、identity revision54→62包含服务恢复/设备上下线变化，不声称版本号不变。
- P4在02:16预检后、真正部署前已在原V1局确认头像（revision1/hash `9f772c3343b60277`）；本次以停机前最新数据为准保留此操作。P1/P2/P3仍为前述原头像。四个既有头像HTTP响应与停机备份文件SHA全部相同；V1组件HTTP访问通过。
- 四台玩家屏均恢复online/eligible，席位1～4与原deviceId对应保持。16格全部online/assigned/reader stable、V0.39，ORDER ready。线上恢复不替代实屏观感或电池异常验收。
- AP、DHCP、watchdog PID仍分别 **718/719/720**；相关配置/服务文件、V1组件和地产素材逐文件SHA全部保持。未改AP/热点、未应用工作区未提交网络修改、未新建房间、未修改Tag或主动提交头像。

### 备份与证据

- Pi受限备份：`/var/lib/gridopoly-avatar-v2-deploy/gridopoly-avatar-v2-deploy-20261018-061933`，目录0700；含 `previous-gridopoly_server`、`previous-server.env`、`candidate-server.env`、停止后的完整 `game-data.tar.gz` 和前后状态/组件检查清单。
- `game-data.tar.gz` 为0600、7,581,663 bytes，SHA **`fcbf98814816c13f63ce1017f3aec99c5542d682b64f1184cf462aff44870c42`**。只读打开压缩包并对全部 **300个游戏数据文件** 重新计算SHA，与停止后的清单完全相同；旧二进制备份SHA再次确认是fb03d800完整值。没有执行回退，也没有写回旧数据。
- Pi本轮上传载荷及窄范围回退程序保存在 `/home/kicofy/gridopoly-avatar-v2-deploy-20261018-061933`。旧服务回退仍受V1有效房间保护；如果用户后来创建V2房间，不可直接降回不支持V2的旧服务。
- 本地证据：`C:/Users/kicof/AppData/Local/GridopolyAvatarV2Deploy/deployment-20261018-061933/`，含 `result.json`、`before.json`、`after.json`、`summary.json`、`component-http.json`、`existing-avatar-http.json`、`stopped-identity.json`、`stopped-file-hashes.json`、`protected-before.json`、`backup-verification.json`；stderr为空。

本端这轮服务器/组件部署已完成，无部署阻塞。**下一次新局默认V2；当前V1局不会自动换成新头像。** 本端没有开新局，V2新局真实选头像及显示验收仍由主会话在用户允许的现场流程中推进。COM28/29电池通信异常由主会话/玩家任务安排物理恢复，本次不认领为已修复。仓库仅追加本报告，统一台账、交接和Git由主会话负责。
