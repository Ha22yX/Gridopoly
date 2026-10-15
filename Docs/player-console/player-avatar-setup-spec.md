# Gridopoly 玩家开局头像创建规格

状态：当前实现与验收目标；部署、实物与性能分别记账，见[当前玩家基线](current-player-console-baseline.md)和[验收状态](player-console-acceptance-tests.md)

更新日期：2026-10-15（本轮仅核对文档与源码/归档证据）

## 1. 目标

自动新局冻结1～4真人并补足4席后，先逐人配对棋子Tag；每名真人绑定成功即可在自己的2.1英寸圆屏创建头像，其他玩家可同时继续配对。单块未分配格子与非环形测试同样适用。
玩家可以组合发型、发色、面部、肤色和服装；机器人由权威服务端自动生成组合。只有本局
所有必需玩家都确认后，服务端才允许进入正式游戏。

头像系统只提供身份识别和个性化外观，不提供技能、初始资金、投骰、交易或其他规则加成。

本文冻结产品语义、页面结构和验收目标。协议消息、字段偏移、素材目录、GAVC 组件格式和
缓存预算以共享协议及 `Docs/firmware/avatar-component-protocol.md` 为单一来源。

## 2. 范围

### 2.1 当前功能包含

- 新对局的 `Avatar Setup` 会话阶段。
- 圆屏左侧 Focus Stack 编辑区与右侧静态半身预览区。
- 五类外观参数和确认操作。
- 真人确认屏障、机器人自动生成和断线恢复。
- 由同一份外观配方生成圆形玩家头像。
- 玩家列表、交易、付款、活动记录、拍卖和网页主控统一使用该头像。

### 2.2 不包含

- 自由绘画、照片上传、摄像头拍照或生成式人脸。
- 任意 RGB 调色盘；首版只使用审核过的离散色板。
- 服装、发型或面部带来的规则能力。
- 在对局进行中修改头像。
- 将完整头像位图写入常规 State/Auth/Roster 高频快照。

## 3. 会话流程

```mermaid
stateDiagram-v2
    [*] --> SeatAssigned
    SeatAssigned --> TagPairing: 新对局逐人绑定棋子
    TagPairing --> AvatarPreloading: 本席绑定成功
    AvatarPreloading --> AvatarEditing: 30 个组件校验齐全
    AvatarEditing --> AvatarGenerating: 玩家确认配方
    AvatarGenerating --> NameReview: 最终头像生成完成
    NameReview --> NameHandwriting: 编辑名字
    NameHandwriting --> NameReview: 确认输入内容
    NameReview --> WaitingForPlayers: 确认名字并 Ready
    WaitingForPlayers --> GameStarting: requiredMask == readyMask
    GameStarting --> InGame: 服务端锁定本局头像
    AvatarEditing --> AvatarEditing: 断线后恢复草稿或权威配方
    AvatarGenerating --> AvatarGenerating: 断线后恢复生成状态
    WaitingForPlayers --> WaitingForPlayers: 断线后恢复准备状态
```

权威顺序：

1. 树莓派冻结合格真人屏并补足四席，逐人配对 Tag；本席绑定成功后可进入身份设置，其余席位继续配对。
2. 进入 `AvatarSetup` 后，真人席位先显示统一素材准备页：当前配方三层优先，然后下载全部
   30 个 Hair/Face/Outfit 组件；机器人立即生成并确认配方。
3. 真人在本机编辑草稿；编辑过程只需本地预览，不把每一次旋钮变化发送给服务端。
4. 玩家按下 `CONFIRM` 后，终端提交完整配方和幂等请求 ID。
5. 服务端校验目录版本和全部预设 ID，记录配方并异步生成最终头像；只有原子发布成功后才
   设置该席位的 `avatarFinal` 并允许进入 Name Setup。
6. 真人确认姓名后，服务端同时设置 `nameFinal` 与 `ready`，并立即更新 Roster。
7. 所有必需真人都满足 `avatarFinal + nameFinal + ready` 后，服务端只创建一次统一 5 秒
   countdown deadline；断线不暂停或重置。
8. deadline 到达后进入 Active。对局开始后，任何终端都不能再修改本局配方或姓名。

每次新建对局都重新进入头像创建。未来可以增加“使用上次外观”快捷项，但不能绕过本局确认。

## 4. 480×480 页面布局

页面保持当前机械 60°、固件 0°基线，所有坐标均为正向 480×480 逻辑画布。

### 4.1 安全布局

- 页面眉题框：`x=100,y=55,w=280,h=32`；主标题框：`x=70,y=77,w=340,h=32`，与当前 `drawHeader` 一致。
- 编辑区采用下面的固定行和右侧裁剪视口，不沿用早期矩形工作区草案。
- 左侧 Focus Stack 编辑区：五行分别从 `x=70/58/52/58/70` 开始，宽度为
  `150/162/168/162/150`，右边界均为 `x=220`。
- 右侧裁剪视口为 `x=220,y=82,w=224,h=270`；内部 220×300 图像位置 `(-6,-12)`、缩放 `230/256`。视口禁止内容溢出，防止宽发型或服装遮住左侧控件。
- 不添加头像圆框、调试配方文本或浮动卡片套卡片。
- 文字和必需操作以主内容安全圆为布局目标；实际几何以 `ui_renderer.cpp` 为准，预览在右侧视口裁剪，最终可读性仍须实机确认。

圆屏边缘会裁掉矩形工作区四角，因此左侧控件使用靠中轴的 Focus Stack，右侧使用半身构图，
不能简单把普通横屏表单按比例缩小。

### 4.2 左侧固定六槽 Focus Stack

左侧从上到下固定为：

| 槽位 | 英文显示 | 中文含义 | 行为 |
| --- | --- | --- | --- |
| 1 | `HAIR` | 发型预设 | 选择发型 ID |
| 2 | `HAIR COLOR` | 发型颜色 | 选择离散发色色板 |
| 3 | `FACE` | 面部预设 | 选择五官与表情组合 |
| 4 | `SKIN TONE` | 面部/肤色 | 选择离散肤色色板 |
| 5 | `OUTFIT` | 衣服预设 | 选择服装轮廓与主配色 |
| 6 | `CONFIRM` | 确认外貌 | 提交配方并开始生成最终头像 |

五个参数槽位置固定，不因某项不可用或目录缺失而重排。五行 y 坐标固定为
`108/154/200/246/292`，行高 38 px；确认按钮固定为 `x=74,y=342,w=146,h=46`。
当前焦点行显示完整边框和强调色，非焦点行只保留文字、序号和分隔线，避免六张卡片挤占圆屏。

每个参数行显示类别短名、当前预设编号或简短名称、左右切换箭头。颜色项使用真实色块，不能只显示
颜色文字。左侧不显示长说明、功能教程或常驻操作提示。

### 4.3 右侧预览

- 使用 `220x300` 静态半身预览，显示几何见 4.1；右侧视口裁剪防止图像覆盖左侧控件，实机核对头部、颈部和肩部的可见范围。
- 编辑页不使用圆形裁切参考环。最终 128x128 公共头像才使用圆形裁切。
- 参数改变后后台经过 45ms 稳定期重新合成整张预览，再由 LVGL 所属任务切换双缓冲；当前未实现逐图层控件替换或 120～180ms 淡入。
- 无完整预览时显示加载环和 `LOADING PREVIEW`；已有上一完整图时保留并显示 `UPDATING PREVIEW`。错误角色、地产占位图或未标记的旧配方不可作为当前预览。
- 外貌确认过程中显示保存状态；完成后进入 Name Review，姓名确认后进入独立 Player Ready 页面，见下一节。

### 4.4 确认后的页面

短按 `CONFIRM` 提交完整配方。立即响应为 `AvatarGenerating` 时，原页显示 `SAVING` 并冻结
重复提交；服务端异步生成最终头像完成后，以更高版本 IdentitySnapshot 发布 avatarFinal，
客户端必须清除 pending 并进入独立的 Name Review 页面。不能因为后续完成帧 requestId 为 0
而永久停留在 SAVING，也不能把 State/Auth/Roster 误当作头像已保存。

Name Review 提供 Back 返回外貌编辑的路径；若组件已释放或尚未就绪，先经过预加载。再次确认仍须由服务端校验并发布新头像版本，不能只替换本地公共头像。姓名已确认或进入 Countdown/Active 后按权威阶段锁定。
确认名字后进入 Player Ready 页面；该页显示全部玩家最终头像、名字和准备状态。全部必需真人
准备后在同一页倒计时五秒，再统一进入游戏。

## 5. 输入模型

头像页继续使用旋钮主导的统一焦点模型。

### 5.1 旋钮

1. 非修改状态旋转：在五个参数行和确认按钮之间移动焦点。
2. 短按参数行：进入该行修改状态。
3. 修改状态旋转：按离散目录循环切换预设；预览实时更新。
4. 再次短按：退出修改状态并保留本地草稿。
5. 长按不承担普通选项修改，避免与全局危险确认语义冲突。

修改状态使用箭头和强调样式。当前 `wrapIdentityValue` 按各字段目录循环首尾，不产生无效 ID。输入每轮最多顺序处理 16 个事件，遇 ButtonUp 先结束本批；有积压时暂缓长按判定。焦点移动只更新旧/新行及必要的确认按钮，不重新获取或重绘头像；同页身份快照保留本地焦点和编辑态。

### 5.2 触摸

- 点击参数行先设焦点，再执行与旋钮短按相同的修改态切换；旋转负责改变预设。
- 当前行内左右半区没有独立上一个/下一个命中动作，预览没有放大手势。这两项不列为已实现能力。
- 点击 `CONFIRM` 等同旋钮短按。
- 页面不依赖滑动手势完成必需操作。

### 5.3 确认与撤销

头像没有资金或资产风险，使用短按确认，不使用 1.2 秒长按。服务端未接受前显示轻量提交进度；
收到权威最终头像完成状态后进入姓名流程。提交失败时保留草稿和焦点，不清空用户选择。

## 6. 外观配方

服务端和终端交换结构化配方，不上传或广播完整位图。下列为语义示意；实际 catalog 字段命名、宽度与 wire 布局以共享 `Protocol.h/.cpp` 为准。

```text
AvatarRecipe
  avatarVersion: u16
  hairPresetId: u8
  hairColorId: u8
  facePresetId: u8
  skinToneId: u8
  outfitPresetId: u8
```

约束：

- 所有 ID 只在指定 `avatarVersion` 的目录中解释。
- 显示名称不是主键，语言切换不能改变配方。
- 配方序列化必须固定字节序并保留未来扩展位。
- 目录升级不得静默改变已存档配方的视觉含义。
- 服务端拒绝越界 ID、未知版本和保留位非零的请求。

V1 已冻结目录规模：

| 类别 | 数量 | 说明 |
| --- | ---: | --- |
| 发型 | 10 | 短发、长发、卷发、辫发、光头等明显轮廓 |
| 发色 | 20 | 14 种自然/染发色与 6 种高辨识度城市强调色 |
| 面部 | 10 | 五官、眉形、脸型和友好表情组合 |
| 肤色 | 8 | 从浅到深均匀覆盖，不与面部预设绑定 |
| 服装 | 10 | 现代城市职业与休闲轮廓，不提供职业技能暗示 |

五字段共形成 `10×20×10×8×10 = 160,000` 种配方，其中结构组合为 `10×10×10 = 1,000`。目录、像素或色板变化须按版本协议发布。

## 7. 视觉与图层

头像采用面向欧美玩家的现代 2D 桌游卡通风格：自然眼睛比例、清晰平面轮廓、友好表情，
不使用日系动漫大眼、写实照片、3D 渲染或受保护角色造型。

### 7.1 单一注册母版

每个 `avatarVersion` 必须冻结一张正面中性注册母版。发型、面部和服装都以同一母版的画布、
头部位置、眼线、下巴、颈部和肩线为准，不得混用旧版完整角色图或另一名角色作为几何参考。

V1 样片的正式母版为
`Assets/GridCity/Avatars/V1/source/reference/avatar-registration-master-gridopoly-v2-chroma.png`：

- 源素材画布固定为 1254×1254；设备校准画布为 320×320。
- 母版采用正面、无透视、静态半身构图；生成新预设时不得改变相机、缩放、姿态和裁切。
- 生成图使用纯色键控背景 `#ff00e6`，随后转换为 RGBA；背景不得包含渐变、阴影、纹理或文字。
- 每次只生成一个独立图层，不得夹带其他部位。透明边缘不得残留洋红色溢色。
- 96×96、128×128 和 320×320 圆形裁切下都必须保持主体可辨识。

### 7.2 图层合同与遮挡顺序

V1 运行时固定由后向前合成：`face -> outfit -> hair`。也就是头发在最前，服装只在颈线以下
覆盖面部底层。未来若把头发拆成前后两层，可扩展为
`background -> back-hair -> face -> outfit -> front-hair -> highlights`，但不能改变可见遮挡语义。

| 图层 | 允许内容 | 禁止内容 |
| --- | --- | --- |
| `face` | 头部、耳朵、完整颈部、五官和肤色明暗 | 发型、服装、背景、文字 |
| `outfit` | 颈线以下的衣领、肩部和上身服装 | 头、脸、耳朵、皮肤、颈部像素 |
| `hair` | 发丝、发际线、鬓角和必要的面部边缘重叠 | 脸、皮肤、耳朵、颈部、服装、饰物 |

图层文件必须保持相同透明画布，不得先紧裁再依赖运行时猜测锚点。发型与面部不得用性别、族裔
或能力标签命名；玩家可以自由组合任何发型、肤色、面部和服装。

所有 `face` preset 的颈部接口必须逐像素兼容 `F01 Angular oval`。V1 构建器从源画布
`y=936` 开始平滑锁定，并从 `y=960` 到底部完全使用 F01 的颈宽、颈长和下边缘；AI 只负责该接口
以上的脸型与五官差异。这样既保留不同下颌、颧骨和五官，也保证所有服装领口可以无条件复用。

### 7.3 AI 生成共同约束

所有分层素材提示词必须包含以下硬约束：

- 正面正交视图、现代欧美 2D 桌游卡通、粗而干净的深色轮廓、克制的多边形赛璐璐明暗。
- 严格匹配注册母版的头部宽度、眼线、下巴、颈宽、肩点和构图比例。
- 只生成目标图层；无文字、Logo、道具、环境、投影或额外人体部位。
- 使用统一 `#ff00e6` 色键背景并保留完整 1254×1254 注册画布。
- 同一预设的颜色变体不得重新生成几何；颜色由程序或构建阶段从同一个已审核母版派生。

AI 输出只是候选源文件，必须经过透明度、边缘、对位和所有组合审核后才能进入 manifest。

### 7.4 发型生成规则

- 发型内侧发际线和左右鬓角必须贴合注册母版头部，留有少量遮缝重叠，但不得盖住眼睛或眉毛。
- 中央面部开口必须透明；不得用不透明底色填充头部轮廓。
- 左右外轮廓不能为了“蓬松”而无限加宽。若宽度或脸部开口错误，必须重新生成或重绘，不能靠
  运行时缩放修复。
- `H01 Angular crop` 已因旧版过宽更换为窄版
  `source/layers/hair/hair-h01-angular-crop-v3.png`；该版本是 V1 的验收基准。
- 发色只改变审核过的发丝色域，黑色轮廓、透明区和高光结构保持不变。

### 7.5 服装与颈部安全区

- 服装必须以注册母版的颈宽、原始领口和肩点为几何基准。
- 完整颈部以及原始领口以上的所有像素必须透明；衣服不得重新生成皮肤或脖子。
- 领口不得过宽，也不得用过大的 V 形开口。衣领、翻领或立体结构可以存在，但不得进入颈部
  Alpha 安全区或遮住下巴。
- `O01 Utility bomber v2` 是窄领口基准；`O02 Operations jacket` 已确认合规并保留。
- 工作提示词和 Alpha 检查详见
  `Assets/GridCity/Avatars/V1/OUTFIT_GENERATION_SPEC.md`。

### 7.6 发色与肤色派生

V1 运行时使用中性 GAVC 和 canonical 整数着色，圆屏预览与服务端最终头像共用算法。不能因性能不足静默改回预生成全部颜色变体；应在保留协议和 golden 的前提下优化合成。

- 肤色只处理暖色、高饱和度的皮肤平面和对应阴影。
- 眼白、虹膜、眉毛、睫毛、嘴部线稿、黑色轮廓、头发和服装不得被肤色算法染色。
- 明暗关系必须由源图保留，换色只能调整色相/饱和度/明度映射，不能覆盖成单一平涂色。
- 色板使用稳定 ID 和审核过的离散 RGB 值；颜色变化不重新调用 AI 生成。
- 每种肤色与每个面部预设都要组合检查，不能只检查默认面部。
- V1 肤色目录固定为 `Porcelain`、`Fair`、`Warm`、`Golden`、`Olive`、`Bronze`、
  `Deep` 和 `Ebony`。`warm` 与 `deep` ID 保持兼容，其余 ID 只扩展目录，不改变面部几何。

### 7.7 每个预设的独立对位

本节是素材制作/校准工具的规则，不是玩家圆屏上的编辑项。每个发型、面部和服装预设都持有独立的设备像素 `x/y` 偏移。校准工具在选中某个 preset 时，
右侧等高位置显示该 preset 的水平和垂直滑块；切换前保存当前值，切换后载入新 preset 自己的值。
偏移只用于精确平移，已接受的绝对基线以 manifest 为准（下表包含 O08 的 y=52，不能按早期 ±48 草案截断）。素材宽度、缩放或内轮廓错误必须回到源图
重新生成，不能增加 scale 参数掩盖问题。

校准工具通过 `COPY ALL OFFSETS` 导出 schema 2 JSON。V1 当前校准基线为：

```json
{
  "schema": 2,
  "unit": "device-pixels-320",
  "hair": {
    "H01": { "x": 0, "y": -11 },
    "H02": { "x": 0, "y": -24 },
    "H03": { "x": -2, "y": -28 },
    "H04": { "x": 0, "y": -20 },
    "H05": { "x": 0, "y": -33 },
    "H06": { "x": 0, "y": -33 },
    "H07": { "x": 0, "y": 2 },
    "H08": { "x": 0, "y": -15 },
    "H09": { "x": 0, "y": -24 },
    "H10": { "x": -1, "y": -10 }
  },
  "face": {
    "F01": { "x": 0, "y": 0 },
    "F02": { "x": 0, "y": 0 },
    "F03": { "x": 0, "y": 0 },
    "F04": { "x": 0, "y": 0 },
    "F05": { "x": 0, "y": 0 },
    "F06": { "x": 0, "y": 0 },
    "F07": { "x": 0, "y": 0 },
    "F08": { "x": 0, "y": 0 },
    "F09": { "x": 0, "y": 0 },
    "F10": { "x": 0, "y": 1 }
  },
  "outfit": {
    "O01": { "x": 0, "y": 31 },
    "O02": { "x": 0, "y": 33 },
    "O03": { "x": 0, "y": 31 },
    "O04": { "x": 0, "y": 31 },
    "O05": { "x": 0, "y": 31 },
    "O06": { "x": 0, "y": 31 },
    "O07": { "x": 0, "y": 48 },
    "O08": { "x": 0, "y": 52 },
    "O09": { "x": 0, "y": 48 },
    "O10": { "x": 0, "y": 31 }
  }
}
```

接受后的偏移写入 `Assets/GridCity/Avatars/V1/avatar-layers.manifest.json` 的 `deviceOffsets`。
校准页面重新打开时滑块显示 manifest 的绝对基线值。设备图层已经烘焙该基线，所以页面预览只应用
“当前滑块值减去基线值”的差值，避免重复偏移；导出结果始终是可直接写回 manifest 的绝对值。

### 7.8 素材发布门禁

- 逐项验证 1254×1254 源文件与 320×320 设备文件的尺寸、RGBA、透明四角和无色键残留。
- 静态审查保留 271 张两两覆盖结构样本，并分别生成 H01-H10、F01-F10、O01-O10、全部发色和
  全部肤色对比图；运行时仍支持完整 `hair × face × outfit` 组合。进入设备固件前再执行全目录
  组合自动检查，不要求把 1,000 张冗余合成图长期纳入素材包。
- 检查头顶、鬓角、耳朵、下巴、颈部、领口和肩点无缝隙、穿帮或不应有的遮挡。
- 预组合预览和运行时独立图层必须使用同一 manifest 偏移并得到相同结果。
- 在 96×96 与 128×128 最终圆形头像上复查辨识度；只在大图正确不算通过。
- 新文件只能通过 manifest 稳定 ID 上线；不得静默替换旧 ID 的视觉含义。

### 7.9 V1 发型与发色目录

V1 测试目录已扩展到 10 个发型：`H01 Angular crop`、`H02 Asymmetric bob`、
`H03 Textured quiff`、`H04 Curly fade`、`H05 Classic side part`、`H06 Short coils`、
`H07 Shoulder waves`、`H08 Braided crown`、`H09 High bun` 和 `H10 Long straight`。

`H05`、`H06`、`H08` 使用收窄后的 `v2` Alpha 母版；修正同时收窄外轮廓和脸部开口，
不允许在运行时增加独立缩放参数代替源图修复。

20 个发色 ID 为 `graphite`、`copper`、`raven`、`espresso`、`chestnut`、`golden`、
`platinum`、`city-teal`、`ash-brown`、`auburn`、`mahogany`、`honey-blonde`、
`strawberry-blonde`、`silver`、`snow-white`、`burgundy`、`rose-pink`、`violet`、
`cobalt-blue` 和 `emerald`。其中后六种为高辨识度城市强调色，其余覆盖冷暖自然色与常见染发色。
所有颜色都从同一发型 Alpha 母版程序化派生，不单独调用 AI，因此轮廓和对位完全一致。

### 7.10 V1 面部目录

V1 面部目录为：`F01 Angular oval`、`F02 Soft square`、`F03 Round youthful`、
`F04 Heart taper`、`F05 Elegant oblong`、`F06 Diamond angular`、`F07 Broad strong`、
`F08 Soft triangle`、`F09 Narrow refined` 和 `F10 Mature sculpted`。

F02 已重新生成，必须通过更宽的软方下颌、较短下巴和不同眼鼻比例与 F01 明确区分。F05 使用
女性化的修长脸型、柔和下颌和细致眉眼，不再使用 Long rectangle 名称。其余预设
分别用脸型轮廓、颧骨、眉眼、鼻梁和下巴比例建立差异；不得仅修改阴影或肤色冒充新 preset。
所有面部共用 F01 的颈部接口和同一眼线注册点，肤色仍由同一源图程序化派生。

### 7.11 V1 服装目录

V1 服装目录为：`O01 Utility bomber`、`O02 Operations jacket`、`O03 Streetline hoodie`、
`O04 Metro varsity`、`O05 Civic blazer`、`O06 Workshop vest`、`O07 Signal knit`、
`O08 Denim commuter`、`O09 Rainline shell` 和 `O10 Gala tailored`。

O01 是窄领口注册基准，O02 保留用户确认合规的既有版本。O03-O10 均为服装独立 Alpha 图层，
不得包含皮肤、脖子、头部、头发或手；完整颈部以及注册母版原始领口以上区域必须透明。所有新服装
先继承 O01 的注册和设备偏移，再通过 O01/F01/H01 组合审查图校准 X/Y；如果领口宽度、几何或
肩线错误，必须回到源素材重做，不能通过运行时缩放修复。

静态构建产物保留 271 张两两覆盖结构样本，并使用 `review/avatar-outfit-presets-v1.png` 专门审查
O01-O10 的领口、颈部和肩点。运行时仍可独立组合全部 10×10×10 种结构配方。

## 8. 圆形头像派生与使用

确认后的配方在各端渲染成相同的圆形头像：

- 圆屏常规头像目标为 96×96 或 128×128。
- 网页可使用 128×128 或更高分辨率，但裁切中心和图层顺序必须一致。
- 外圈使用玩家席位色，头像本身不依赖颜色表达唯一身份。
- 玩家名独立渲染，不烘焙进头像位图。
- 交易、付款、拍卖、玩家详情、活动记录和胜利页都引用同一个配方。
- 系统/City Bank 使用独立系统图标，不伪装成玩家头像。

## 9. 素材分发与缓存

头像素材由树莓派 Wi-Fi 资源服务提供，组件格式、路由、CRC、着色、合成算法和 golden
以[头像组件协议](../firmware/avatar-component-protocol.md)及其引用的 manifest 为单一来源。公开组合预览只作
兼容，正式圆屏编辑器使用中性 GAVC 组件。

当前实现：

- 组件路由固定为 `/assets/avatar-components/v1/hair/h1.gavc`、`face/f1.gavc` 和
  `outfit/o1.gavc` 形式，ID 为 1..10 且禁止前导零。
- 编辑页打开前显示独立 `PREPARING AVATAR` 进度页；当前配方三层优先，4 个低优先级
  HTTP worker 下载并校验全部 30 个唯一组件。
- 30个组件和两张220×300 RGB565预览使用独立2304KiB临时PSRAM池。组件
  文件总计 1,921,970B，身份阶段不做 LRU 淘汰。
- 30 个组件校验齐全后准备状态保持完成并自动进入 Focus Stack；初始合成自动请求，可以稍后交付，但不得要求玩家先旋转才能结束 `LOADING PREVIEW`。
- 外貌、姓名、准备和倒计时共用该临时池；进入 Active 且 LVGL 不再引用临时图后统一释放。
- 游戏期素材使用严格 1 MiB：棋盘图片 640 KiB、最多六张最终头像 384 KiB。
- Hair Color 与 Skin Tone 使用 canonical 整数算法本地换色，不产生 HTTP 请求。
- 无完整图时显示 spinner；已有上一完整图且当前配方正在合成时保留它并显示 `UPDATING PREVIEW`。前台图及待交付完整图不可被 worker 复写，过期代次不能发布为当前配方。
- 下载失败保持有界重试和进度状态；不得因后台无关资源完成触发当前页面整屏重绘。

## 10. 权威服务端语义

### 10.1 状态

服务端以 Identity 状态而不是本地页面作为权威，至少持有：

```text
roomPhase / identityRevision / stateVersion
requiredMask / avatarFinalMask / nameFinalMask / readyMask / onlineMask
seatRevision / avatarRevision / final recipe / final avatar URL
selfStage / countdown deadline
```

bit n 对应 playerId `n + 1`。机器人在建局时生成确定性随机配方与名字并自动完成；真人必须
依次完成外貌、名字和 Ready。`AvatarSetup`/`Countdown` 阶段 Heartbeat 的 applied-state version
表示已经完整应用 IdentitySnapshot，而不是普通 State/Auth/Roster。

### 10.2 幂等和版本

- 身份修改使用 `IdentityRequest`，必须带非零 request ID、房间、席位、当前非零 stateVersion
  和 seatRevision；字段及偏移只以共享 `Protocol.h/.cpp` 为准。
- 相同 request ID 与完全相同 payload 重发必须返回第一次结果，不重复推进版本；同 ID 不同
  payload 必须拒绝。
- `ConfirmAvatar` 的立即结果可进入 `AvatarGenerating`。生成完成通过同房间更高版本、
  requestId=0 的 `IdentitySnapshot` 发布；本席 `avatarFinal` 后客户端必须清除 SAVING/pending
  并进入 Name Setup，不能继续等待组件。
- `ConfirmName` 只有在最终头像可用时合法；成功后同一事务写入公开玩家名并同时成为 Ready。
- 普通 Heartbeat、State/Auth/Roster 重同步不得清除最终配方、名字或 Ready。
- 新 room/session 必须清除上一局草稿、请求 pending、倒计时和页面生命周期。

### 10.3 机器人

机器人使用 `roomSeed + playerId + avatarCatalogVersion` 生成确定性随机配方，保证服务重启后外观不变。
在可用组合足够时，服务端应尽量避免机器人之间完全相同的配方，但不能因去重失败阻塞开局。

### 10.4 断线恢复

- `avatarFinal` 或 `nameFinal` 尚未完成的真人断线：服务端保留席位；重连后按
  `IdentitySnapshot.selfStage` 恢复外貌、姓名或生成中页面，仍需完成剩余步骤。
- `nameFinal + ready` 已完成的真人断线：保留最终配方、最终头像、名字和 Ready；重连后直接恢复等待页。
- 全员确认后的锁定阶段断线：游戏不回退到编辑页。
- 目录版本不匹配：显示升级页，禁止提交未知配方。

## 11. 已实现协议边界

- `IdentityRequest` 为消息 `0x29`，`IdentitySnapshot` 为消息 `0x2A`；固定布局由
  `Firmware/libraries/GridopolyProtocol/src/gridopoly/protocol/Protocol.h/.cpp` 定义。
- 新认证UDP会话的Pair/full resync先发送TagPairSnapshot，再发送IdentitySnapshot，随后仍按协议发送pending Card、State、
  Authority、Roster、Trade 和事件历史。
- AvatarSetup/Countdown 阶段，只有 IdentitySnapshot 可以完成该版本的身份投影；进入 Active
  后恢复普通三投影、交易和事件游标完整性。
- 二进制投影只传配方、版本、mask、名字和最终头像定位信息，不传 GAVC 或头像位图。
- 组件及最终头像通过 HTTP immutable 路由分发，完整 URL、room、player、revision 和 hash
  共同构成缓存键。
- 倒计时按快照中的 `serverEpochMs` 与 `countdownDeadlineEpochMs` 换算为本机 monotonic
  deadline；幂等重放不得把五秒倒计时重新开始。
- Wi-Fi/UDP HMAC、packet sequence、重放窗口、请求重发和新房间恢复语义保持不变。
## 12. 页面与全局行为

- `AvatarSetup` 是开局顶层阶段，普通首页、资产、玩家、交易和活动入口不可进入。
- 断线图标仍是最高层全局状态；离线时可以继续预览本地草稿，但不能确认。
- 触摸、旋钮和服务端快照都通过同一个 reducer 更新页面，不在 renderer 内修改权威状态。
- 收到重复快照不得重播全员锁定动画。
- 游戏开始事件到达时，尚在局部修改状态的控件必须安全退出，使用权威已确认配方。

## 13. 验收矩阵

以下是发布门槛，结果以[验收状态](player-console-acceptance-tests.md)及带版本的报告为准，不表示已经全部通过：

| ID | 场景 | 通过标准 |
| --- | --- | --- |
| AVATAR-01 | Focus Stack 圆屏布局 | 五个参数和确认按钮可读可操作；右侧裁剪视口中半身像无异常缺失或对控件的遮挡 |
| AVATAR-02 | 旋钮焦点 | 六槽顺序固定；修改状态只改变当前参数，退出后恢复导航 |
| AVATAR-03 | 触摸操作 | 行点击与旋钮短按共享焦点/编辑态，确认命中正确；未实现的半区切值和预览放大不算通过 |
| AVATAR-04 | 实时预览 | 缓存命中后的最新配方预览目标不超过 100ms，仍待实测；45ms 合成稳定期内旧完整图必须标注更新中，无撕裂或过期结果覆盖 |
| AVATAR-05 | 图层遮挡 | 固定 `face -> outfit -> hair`；全部组合无头发穿帮、颈部遮挡或领口错位 |
| AVATAR-06 | 确认屏障 | 少一名真人未确认时不开始；最后一名确认只启动一次 |
| AVATAR-07 | 阶段返回 | 外貌未提交时可继续编辑；Name Review 可按产品流程返回外貌阶段；Countdown/Active 后不可编辑 |
| AVATAR-08 | 机器人外观 | 同一 room/seat 重启后配方不变，不阻塞真人确认 |
| AVATAR-09 | 断线恢复 | 未确认恢复编辑；已确认恢复等待；不重复锁定动画 |
| AVATAR-10 | 幂等重发 | 丢首个结果后重发同 request ID，只确认一次、版本只推进一次 |
| AVATAR-11 | 新房间 | 清除旧配方生命周期，重新要求本局确认 |
| AVATAR-12 | 目录不兼容 | 显示升级页，不提交或错误渲染未知 ID |
| AVATAR-13 | 素材失败 | 显示加载环并禁用确认；恢复后原地显示正确预览 |
| AVATAR-14 | 缓存压力 | 2304KiB身份临时池容纳30个GAVC和两张预览，身份阶段不淘汰；进入 Active 后一次性释放且当前头像不闪烁 |
| AVATAR-15 | 全局一致 | 圆屏、网页、交易、付款、拍卖和玩家详情显示同一配方 |
| AVATAR-16 | 文化与可访问性 | 预设可自由组合，无刻板标签；黑白检查仍能区分玩家轮廓 |
| AVATAR-17 | 独立对位 | 每个 preset 保存自己的 X/Y；切换正确恢复，导出后与 manifest 一致且不重复应用 |
| AVATAR-18 | 程序化换色 | 发色/肤色不改变几何，不污染眼睛、线稿、头发以外区域或服装 |

## 14. 当前实现与维护基线

### 当前玩家端实现基线（2026-10-15）

- 圆屏已实现 `AvatarSetup -> NameReview/NameHandwriting -> PlayerReady -> Countdown`
  顶层生命周期，开局阶段不能进入游戏首页。
- Avatar Setup 使用 B. Focus Stack；当前五行、确认按钮和右侧裁剪视口的准确坐标统一见 4.1～4.3，不再沿用旧版原点或宽度。焦点切换保留同页控件。
- 头像预览不再下载整张组合图。独立 `PREPARING AVATAR` 页面以进度条先缓存当前三层、再
  缓存全部 30 个中性 GAVC 组件；进入编辑页后切换 preset 不再等待网络。发色和肤色使用 canonical 整数算法在本地
  处理，不产生 HTTP。合成顺序固定为 `face -> outfit -> hair`，背景固定为 `#061017`。
  45ms 稳定期后后台合成；首次完整图前显示加载，后续保留上一完整图并明确标注更新中。
- Ready 页面只在 `AvatarFinal` 后请求带 room、player、revision 和 hash 的
  `128x128 RGB565 LE` 最终头像。服务端在每次合法外貌确认后发布对应版本，同请求幂等重发不重复生成；姓名确认前返回外貌重提仍按新请求校验。
- 名字草稿在圆屏本地；真实触摸驱动保留接触边界，同一笔不因UI采样处理延迟超过100ms断开，不同接触不串笔。真实抬笔及最后书写后完整1200ms静默才提交；读失败不得伪装可靠抬笔。同页周期快照保留画布和识别代次。通用几何匹配、int8 EMNIST与模板共同识别；拒识保留墨迹。DELETE优先清当前墨迹/撤销旧结果，空画布才删末字；DONE先等待墨迹提交成功再回Name Review，拒识时留在画板。只有Name Review确认发送`ConfirmName`。
- 倒计时不使用本机 wall clock。接收 IdentitySnapshot 时按
  `remaining=max(0, deadlineEpoch-serverEpoch)` 换算为本机 monotonic deadline；同请求
  幂等重放刷新 serverEpoch 但不刷新权威 deadline，因此不会重新开始 5 秒。
- 游戏期棋盘与最终头像共享严格 1 MiB PSRAM 上限：棋盘 640 KiB，最终头像 384 KiB。
  Identity Setup临时池上限2304KiB，保存30个GAVC（1,921,970B）及两张`220x300 RGB565`预览（每张132,000B），合成时保留上一完整预览；进入Active后统一释放。
- `ConfirmAvatar` 的立即响应允许停在 `AvatarGenerating/SAVING`；随后同 room 的更高版本、
  request-id-zero IdentitySnapshot 只要证明本席 `avatarFinal`，就必须清除 transport 与 UI
  pending 并进入 Name Setup。AvatarSetup/Countdown 阶段只有 IdentitySnapshot 可以推进
  applied-state version，State/Auth/Roster 不能提前确认这次身份转换。

### 历史实施阶段 A：目录和视觉样片

以下 A～D 保留早期实施拆分，不能作为当前版本逐项实机通过记录；具体结果以带日期报告及验收表为准。

- 先制作 3 个发型、3 个发色、3 个面部、4 个肤色和 3 个服装的垂直样片。
- 在 480×480 实机上验证图层、肤色着色、96 px 圆形裁切和 40/60 布局。
- 冻结 manifest 和配方版本策略。

### 历史实施阶段 B：本地编辑器

- 只实现本地草稿、旋钮/触摸、静态预览、加载和缓存。
- 用 Demo Lab 覆盖所有组合和素材失败，不接权威开局屏障。

### 历史实施阶段 C：协议与树莓派权威状态

- 冻结消息编号和 payload。
- 实现确认、撤销、机器人配方、持久化、重连和全量同步。
- 完成主机测试与双客户端真实 UDP 集成测试。

### 历史实施阶段 D：全局头像替换

- 将玩家列表、付款、交易、活动、拍卖和网页中的固定头像替换为配方渲染。
- 保留当前五名固定角色作为兼容默认预设或旧存档回退，不同时维护两套身份规则。

### 阶段 E：实机发布与持续验收（未全部关闭）

- 已有正常候选部署及部分历史实机确认，但不能标作本文件全部 AVATAR、性能、断线恢复和四屏同局门槛已通过。
- 持续执行本文件验收矩阵，并在协议、资源或硬件方向变化时同步更新当前基线。

## 15. 已冻结决策与后续扩展

- 预设范围固定为 Hair 10、Hair Color 20、Face 10、Skin Tone 8、Outfit 10。
- 头像图层使用中性 straight-alpha RGBA8888 GAVC，玩家端与服务端共用定点着色和
  source-over 算法；不得退回服务端预生成每一种颜色组合。
- 身份设置期使用2304KiB临时组件及双预览池；进入 Active 后释放它，再使用游戏期严格 1 MiB
  棋盘/最终头像 LRU。后续不得另建无上限图片缓存。
- `nameFinal + ready` 玩家断线时是否允许房主强制移除席位。
- 是否允许玩家在等待阶段一键使用上一局配方。
- 网页房主是否可以为无屏幕真人代选头像。

后续扩展不得由客户端单方面猜测；任何目录、状态或缓存语义变化都需要同时更新游戏状态机、
Wi-Fi/UDP 协议、素材清单、圆屏 UI 规范和验收测试。


## 2026-10-15 配对与输入验收状态

[配对契约](../firmware/ring-onboarding-contract-2026-09-26.md)定义TagPairing席位阶段7、room/session/revision与CanEditIdentity；旧调试局可不启用该门禁，不能把旧行为当自动新局默认。全部必需真人绑定并完成头像/姓名/ready后才统一倒计时。

Avatar编辑行/编辑态在周期身份快照中保持本地焦点；旋钮双相非法跳变恢复保持可信定位点，不增加反转冷却。当前COM7为 `22ca08e6…`、COM27/28为 `dfc4b7e5…`（16MHz），COM29为 `716ff623…`（12MHz正常游戏诊断构建，120秒日志已结束），均包含每轮最多16事件、积压长按保护和焦点局部刷新。普通12MHz `3da4c493…` 已构建核验、未部署。粗略四组共24个解码步与消费对应不证明精确物理卡点；Avatar慢/快正反手感仍待复验。9月手写有用户确认，本次四台仍需对应版本验证。COM29静止等待页正常反馈不替代Avatar、姓名、动态页面或24FPS/42ms性能验收，core1历史Retarget max57ms仍未关闭。见[当前基线](current-player-console-baseline.md)、[本次输入记录](../firmware/player-16-trial-progress-2026-10-15.md)、[COM29记录](../firmware/player-com29-display-progress-2026-10-15.md)和[历史手写记录](../firmware/player-input-handwriting-fix-2026-09-26.md)。

USB热点已在22:07:58及22:18:49 UTC实证16格＋4屏/20台接入，旧7关联限制解除。18:47 EDT（22:47 UTC）最新room993580116/version9中COM28/29/27为席1/2/3，三真人Tag配对complete、boundMask7、AvatarSetup；COM7此前由用户关机。09:39旧房间中COM29等待新局属于历史，不能继续写成当前阻塞。唯一16节点环及reader stable已有证据，但配对complete不证明每屏拿回提示、旋钮编辑、姓名、ready、四真人倒计时或实体对局全部通过。见[设备台账](../firmware/current-device-connections.md)及[USB部署](../firmware/usb-ap-deployment-2026-10-15.md)。

四台在用固件及普通3da4候选均 `BatteryBoard=none`，只表示软件遥测关闭；实际接电池反馈不构成电量/充电/温升实测通过。身份流程不得显示假电量，启用遥测后还需单独验证其更新不抢焦点、不破坏手写画布。
