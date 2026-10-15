# ORDER 物理邻接协议 V1

固件/本协议文档 owner：格子任务。服务器 owner：HTTP解析、boot会话/租约、拓扑图及锚点分配。
当前复用Tile已有HTTP心跳，不把Wi-Fi注册次序当作物理顺序，不启用RS485。

## 电气与信标

上游GPIO10经Q1 2N7002开漏驱动：GPIO HIGH表示下游ORDER_IN被拉LOW，GPIO LOW释放。
输入GPIO9外部1k/10k/100nF滤波；不直接连接相邻3.3V输出，不改变上电LOW/释放状态。
接线必须OUT→IN并共地。未接线时输入上拉，不能凭模块都在线生成邻接。

每块模块独立在自己的OUT线上发信标，各邻接线可以并行，接收端不转发上游信标。
链路头是图中没有确认上游的节点，不依赖某块“先注册”或先收到网络广播。

- 开机随机非零64bit bootId，CPU每次重启更换，HTTP和物理帧使用同一值。
- 物理帧12字节/96bits：bootId大端8字节、seq大端2字节、CRC大端2字节。
- CRC-16/CCITT-FALSE，poly0x1021、init0xFFFF、非反射、xorout0，覆盖前10字节。
- 字节/bit均MSB先发；seq每次启动一帧递增，允许uint16回绕。
- 帧前线路空闲HIGH至少60ms；sync LOW90ms/HIGH45ms。
- 数据0 LOW15ms，数据1 LOW30ms；每bit后HIGH15ms。低速脉宽有意给RC滤波和调度留容差。
- 帧启动周期6000ms，首帧最早在启动100ms后；最长全1数据4320ms加同步/间隔仍小于周期。
- 接收容差：sync LOW70～115ms；sync HIGH30～60ms；0 LOW10～21ms；1 LOW24～39ms；
  数据HIGH9～25ms。宽度落在间隙或CRC错误丢整帧，不产生新上游证据。

## 调度与失效

独立esp_timer任务每1ms采样GPIO9边沿并推进GPIO10状态机；不依赖主循环显示/RFID或
HTTP请求返回，不迁移现有RFID中断。定时回调内无网络、日志、分配内存或长等待。
跳过积压回调；采样间断超过6ms放弃当前帧、释放OUT，等待下一完整周期。
这保证软件恢复调度时不会补发过期脉冲；CPU完全停机需依赖复位及Q1栅极100k下拉，
不能把软件定时器说成在CPU停机时仍有独立硬件截止。

连续输入LOW超过150ms标记stuckLow并立即撤销接收有效状态；输入重新HIGH后等新完整帧。
没有新完整序号满15000ms有效期失效；保留上次序号fence，相同seq不能在过期后复活。
CRC错误/未完成帧不更新新鲜度。新bootId或同boot的mod16严格新seq才能刷新物理证据。

## HTTP请求字段

现有POST `/api/tile-modules/heartbeat` Tag字段保持，新增可选`order`对象：

```json
{
  "order": {
    "version": 1,
    "bootId": "0123456789ABCDEF",
    "txSeq": 14,
    "upstreamBootId": "89ABCDEF01234567",
    "upstreamSeq": 12,
    "ageMs": 1432,
    "valid": true,
    "inputState": "idle"
  }
}
```

bootId/upstreamBootId为非零16位HEX，固件发大写，服务器接受大小写并规范化。
version/txSeq/upstreamSeq/ageMs为JSON非负整数，seq≤65535，version必须1。
inputState为idle/receiving/stuckLow；接收下一帧时旧完整证据可仍valid。
valid=false时upstreamBootId=null、upstreamSeq=0、ageMs=0。
ageMs在发送时根据本地最近接收完成时间计算，缓存或重试不能把旧观测年龄重置。
ageMs≥15000不得建边。缺order兼容旧固件，但不能成为物理线序证据。

## 服务器防陈旧与分配

服务器用当前在线模块唯一bootId映射把接收者报告转换为“上游→接收者”有向边。
源boot未知、重复boot、自环、分叉不能被解释为确定有效邻接；完整有向环需由服务端明确按闭环规则验证，不能默默丢弃一条边当链。
双方模块租约以及物理观测新鲜度共同约束边；接收时减ageMs推算观测时间。
同接收者boot/上游boot下seq差(mod65536)需位于1..32767，重复seq不延长截止。
固件valid=false、观测过期或模块失联撤销物理派生，不能覆盖manual。
运行期模块更换boot立即撤销旧映射/边，保留退休boot与seq fence防延迟旧心跳复活；
服务器重启后的注册/会话处理由服务器实现，不复用持久化旧邻接。

地图分配由服务器负责：最近上游manual作为每一段锚点；首锚前按负偏移回推；地图下标
按boardSize取模。所有已manual节点保持原指定值，多锚点不是强行覆盖其他手动配置。
尚无物理边/锚点的模块应明确显示该状态，不用注册顺序伪装按线自动分配。


## V0.33 板型与 16 / 32 格闭环

物理order.version仍为1，boot/seq/CRC及波形均不变。各模块独立发送自身身份，所以 16 或 32 个有向独立链段可成环，不需要共享总线竞争或帧转发。2026-10-15 的 16 格扩展在服务端实现，格子 V0.33 的帧不需要变更。GPIO13在已核验的两种板网表均NC，不具备板型探测功能。

心跳顶层moduleRole为long/corner/unknown，firmwareVersion为诊断字符串。由普通固件构建profile固定板型，默认unknown；地图位置与角色独立。闭环拓扑规则归服务端：16 节点为 4 corner 与 12 long、相邻角落之间 3 long；32 节点为 4 corner 与 28 long、相邻角落之间 7 long。当前地图须与物理环格数相同，每点唯一上下游且会话/租约新鲜。四个同构角落没有绝对START区别，默认选择稳定 moduleId 字典序最小的角落作 START，沿 OUT 为正方向；一致手动锚点可旋转全环，未知板型或不一致角落分布不能冒称整盘正确。role不能由临时随机boot或注册顺序推断。24/40 节点完整环不在本轮支持范围，开链规则不变。[开发/部署/实体证据](trial16-release-2026-10-15.md)分别记录。

## RFID 新鲜扫描与配对提示扩展

请求顶层增加tagScanSeq(u32)、tagAgeMs(u32)、tagFresh(bool)、pairingRoomId(u32)、pairingSessionId(u32)。scanSeq每次完成报告递增且跳过0；tagRevision仍只在内容状态变化时递增。只有完整可信stable且无overflow、age严格小于5000ms，才tagFresh=true。重复心跳不更新扫描完成时刻。不完整碰撞和为视觉稳定保留的缺失消抖库存不能刷新fresh；服务器不得拿它们绑定或确认拿离。

room/session echo必须在实际扫描开始时锁定并随库存保存，未配对用0；新响应不能把新session盖在旧库存上。服务端比较当前room/session/scanSeq与扫描新鲜性，先新空库存清场，再新唯一Tag才绑定。模组不自决归属，不写Tag。

独立响应pairing字段为{active,roomId,sessionId,revision,state,playerId,playerName,leaseMs}，数值均无符号，lease为本地接收后剩余提示时长。state枚举waiting_reader/clear_required/awaiting_tag/success_remove/already_bound/multiple_tags/reader_offline/timed_out/cancelled/complete。缺字段兼容旧服务器但不进入配对模式；未知active状态或非法数值拒绝响应。active=false或断网/租约过期撤销提示。active配对优先于movement；服务器配对消费与正常到达必须隔离。

成功拿回提示可与另一个模块下一位玩家的配对并行，旧模块继续原session直到新的完整空库存；单模块复用必须清空后再下一session。普通用户画面显示配对动作与玩家编号，不显示协议字段；STATUS日志保留会话及新鲜度供联调。
