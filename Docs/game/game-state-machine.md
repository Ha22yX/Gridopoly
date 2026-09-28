# Gridopoly 当前权威状态机

更新：2026-09-27。枚举以[GameModel.h](../../Firmware/libraries/GridopolyCore/src/gridopoly/core/GameModel.h)为准，转移以[GameEngine.cpp](../../Firmware/libraries/GridopolyCore/src/gridopoly/core/GameEngine.cpp)为准。身份、游戏、配对及传输会话是不同状态域，不用一个页面名代替多个权威状态。

## 开局身份和Tag

自动建局冻结设备席位，Identity保持准备阶段；逐人TagPairing席位阶段7完成后，该玩家可独立进入外貌/姓名，下一人继续配对。全部必需玩家绑定并ready才进入五秒倒计时，然后开启Core游戏。未获席位显示等待新局，不显示假金额主页。

配对状态为Disabled、WaitingReader、ClearRequired、AwaitingTag、SuccessRemove、AlreadyBound、MultipleTags、ReaderOffline、TimedOut、Cancelled、Complete。room/session/revision及新扫描共同隔离旧事件；成功取回提示可跨下一人的reader继续存在。详见[现行跨端契约](../firmware/ring-onboarding-contract-2026-09-26.md)。

## Core实际阶段

| 数值 | GamePhase | 当前含义 |
| ---: | --- | --- |
| 0 | Lobby | Core尚未正式开始，服务端身份流程可以仍在准备 |
| 1 | AwaitRoll | 当前玩家掷骰或处理限制区选择；允许规则限定的资产管理 |
| 2 | AwaitMoveConfirm | 有pendingMove，等待权威位置确认；自动到达另受首帧gate约束 |
| 3 | AwaitPurchase | 无主资产明确购买/拒绝 |
| 4 | AwaitAuction | 介绍就绪屏障后轮转竞价/放弃 |
| 5 | AwaitDebt | 强制付款、筹款、偿还或合法破产 |
| 6 | TurnEnd | 已完成当前落点，可结束回合及管理资产 |
| 7 | GameOver | 胜者确定，拒绝继续对局操作 |
| 8 | AwaitCard | 等待卡片Continue或对应效果/债务推进 |

```mermaid
stateDiagram-v2
    [*] --> Lobby
    Lobby --> AwaitRoll: 身份流程完成后start
    AwaitRoll --> AwaitMoveConfirm: 掷骰产生目标
    AwaitMoveConfirm --> AwaitPurchase: 确认到达无主资产
    AwaitMoveConfirm --> AwaitDebt: 确认到达付款格
    AwaitMoveConfirm --> AwaitCard: 确认到达卡片格
    AwaitMoveConfirm --> TurnEnd: 普通落点处理完成
    AwaitPurchase --> AwaitAuction: 拒绝购买
    AwaitPurchase --> TurnEnd: 购买并完成落点
    AwaitAuction --> TurnEnd: 拍卖结算
    AwaitCard --> AwaitDebt: Continue后付款
    AwaitCard --> TurnEnd: 效果完成
    AwaitDebt --> TurnEnd: 偿还或破产后续处理
    TurnEnd --> AwaitRoll: 下一个存活玩家
    TurnEnd --> GameOver: 胜负检查
```

该图仅概括主要路径，不是全部转移：双骰可由落点完成直接返回AwaitRoll，限制区第三次失败付款后继续移动，卡片移动还可触发新落点；破产可直接切换下一存活者或进入GameOver。具体continuation不得按图省略。

## 到达和财务边界

玩家动画只呈现结果；`ConfirmPosition`需匹配当前player/target。RFID自动到达由服务端调用同一权威事务，配对库存不能用于到达。Action17在MoveGuide真实首帧后报告，不推进位置或重复结算。

真人付款进入AwaitDebt，并由合法PayDebt动作完成。没有独立RentClaim枚举、20秒地主确认或Core持有的10秒扣款定时器。圆屏ForcedPayment本地倒计时可提交PayNow/PayDebt，不能保证屏幕离线后的服务器定时扣款；旧设计图与此不同。建筑库存/整组清算、破产 continuation与赢家均由Core决定。

## 恢复和验证

游戏、Identity、冻结席位、Tag绑定及未取回限制持久化；配对恢复生成新session fence。ORDER物理邻接/租约及人工锚点不沿用持久化旧边。UDP重连恢复同席位，不能把断线真人变为bot。

重复、过期room/session/revision、旧请求及乱序投影必须拒绝或幂等；页面恢复不能重新播一次已完成到达/卡片或重复扣款。主机和隔离集成已有回归；真实pendingMove断网、完整32块/4屏恢复与整局仍待现场验收。

[当前规则](game-rules.md) · [旧设计状态图](../archive/game-state-machine-design-before-2026-09-27.md) · [当前交接](../CURRENT-DEVELOPMENT-HANDOFF.md)
