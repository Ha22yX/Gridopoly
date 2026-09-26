# 2026-09-26 玩家头像输入与手写修复

## 状态

用户报告头像旋转后焦点不动或倒退，手写 H 未完成就识别、笔画断裂以及写完无结果。本记录对应后续修复；开始时 COM7 运行普通固件 `109df53f3941db281cc710a282fd5cbea700a70c73d6ef3316ab8350d55537b0`。源码、构建和主机验证已完成，21:31:55 UTC新普通固件efbe3d08四段写验通过；用户实际旋钮/手写体验待反馈，不能用主机测试替代实机体验。

现场 room993580108 已真实完成单块未分配格子的 Tag 绑定；玩家头像已确认、姓名待完成。Pi 已部署新局地图兼容修复 `2bc084d7`。本轮不重建房间、不注入身份或游戏动作。

## 已复现的问题

| 问题 | 生产代码原因 | 修复方向 |
| --- | --- | --- |
| 旋钮选中项回跳 | 本地 `setFocus` 没有同步 identity.focus；周期 Identity 快照用旧焦点恢复页面 | 保持当前身份页的本地焦点，不改变已确认的机械旋钮方向 |
| 书写中笔迹丢失、识别取消 | 同页周期 Tag/Identity 更新走完整 rebuild，销毁 canvas，清空样本并取消识别代次 | 同 room/seat 的手写页保留 canvas，按需更新名字、焦点和提示 |
| 连续一笔被切开、分笔又串联 | 主循环重复读取 LVGL 当前状态，会漏掉期间的抬笔；100ms 采样间隔条件又将处理停顿当成抬笔 | 从真实触摸驱动采样入口记录按下/抬起，以接触边界连接笔画 |
| 过早结束字母 | 主循环等待 LVGL 锁前的旧时间戳被用于最后接触时刻 | 用实际采样时刻及观察到的抬笔时刻计时；服务函数在获锁后读取当前时间 |
| 快速笔画消失 | 少于4点一律拒绝，但主循环仍清空画布 | 允许有几何长度的少量采样；失败保留墨迹并提示继续补写或清除 |
| DONE 丢掉最后一个字，DELETE 与识别竞态 | DONE 立即离开手写页；DELETE 依赖整页重建副作用，异步结果可能尚未返回 | DONE 等待当前墨迹提交；DELETE 优先清当前墨迹、撤销旧识别，空画布时才删已有字符 |
| 完整三笔 H 被识别成 B | 几何模板选 H，但神经网络 logit 差超过旧门槛并覆盖模板 | 检查通用几何匹配规则及既有字母回归，不用单个 H 调置信阈值 |

头像保留渲染进一步避免焦点变化时反复设置未改变的头像图片和文字。手写识别原本已在低优先级后台任务执行；本次不能描述为首次异步化。保持现有 RGB/LVGL core1 配置，不以旧 core0 帧率数据证明新固件显示稳定。

## 验证边界

初始独立审查直接编译生产 `ui_handwriting.cpp` 和实际模板/神经网络，外设使用主机替身，复现5项旧问题。证据：`C:/Users/kicof/AppData/Local/Temp/gridopoly-handwriting-audit-20260926/run.cmd`。

新识别代次调度独立审查13/13通过：worker忙时续写/DONE不会误标为已识别，旧结果不会覆盖新墨迹，DELETE/换页撤销旧结果，失败后可补写重试，结果只消费一次。证据：`C:/Users/kicof/AppData/Local/Temp/gridopoly-handwriting-generation-audit-20260926/run.cmd`。这仅验证调度核心，页面接线由后续生产 renderer 测试覆盖。

触摸 SDK 的 `readPoints` 返回负数表示读取失败，0才是成功读取且无触点。此前两者都变成抬起；新过滤器短时保持控件状态，持续失败后释放控件，并禁止用失败采样自动提交墨迹。目前没有现场 I2C 错误证据，不将它列作已证实的硬件故障。

主修复20份生产/测试文件已提交 Git `967a93e`。生产 reducer 4/4、真实 LVGL 运行时最终43/43、建局身份83/83、旋钮15/15通过。最终运行时结果位于 `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/input-runtime-final-v4-20260926.log`。连续接触100/240ms两点仍连接，真实抬笔再按下则分笔。

后续复现同一批触摸队列的两次 DELETE 被布尔请求合并。Git `18a75e2` 在触摸队列前落实实体按钮请求，并逐个触摸动作立即服务手写状态，保留顺序；新增两次DELETE、DELETE后DONE、实体DELETE后触摸DELETE三例通过。Git `9f720d7` 单独修正下面记录的硬件触摸port编译接口。

独立页面复核发现并修正 DONE 拒识后仍挂起、导致补写第一笔过早再次提交的问题。最终有界复验7/7通过：拒识结束本次DONE但保留墨迹，补写后1199ms仍保留，1200ms才正常提交；后台识别中仍等待；成功则先追加末字再离页。新证据 `C:/Users/kicof/AppData/Local/Temp/gridopoly-handwriting-done-recheck-20260926/done-recheck.log`；原集成失败日志保留，没有改写成通过。

通用几何门控未更换网络权重或修改原神经网络0.45门槛。独立A–Z笔迹312组合成变体从284个正确增至301个，原正确样本无退步，12组H稀疏/密集采样结果一致。剩余11组合成误判仍保留；这不是实际用户手写准确率，也不保证任意笔迹都识别正确。

正常构建使用冻结 run `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/runs/production-20260926-171245-ebcd9d9f5f3046d4a44323b06a1cf129/`，构建开始时HEAD为`f6cfc39`，不能改标签冒充提交后的重新构建；实际源以2127文件清单及后续逐SHA校验为准。第一次编译发现触摸port使用了错误的LVGL字段层级，改为仓库LVGL8.4实际存在的`input->driver->read_timer`；主机runtime未编译硬件port，因此未覆盖这个编译错误。失败记录保留，仅更新该冻结源并重新生成清单，其余2126文件保持原哈希，在同一隔离目录继续构建。

硬件接口修正后底本完整编译通过，随后保留到run下`before-action-order/`，再仅更新ino，另2126文件不变，最终编译通过。中间底本不用于部署；每阶段清单、变更记录和旧产物均保留。

## 最终构建与批量包

- 普通玩家app：`efbe3d08ca59cf73d395202105dc91a4e874cea53e05c8eac90a69e3162deea7`，文件1,905,536B，program1,905,390B，RAM125,672B；HWCDC检查通过。
- sourceDigest：`7ea4c62f984d30dbf983769845ec5320b2f11ea8de48221d0e2997b27a2c9c1e`。主任务在21:30:44 UTC独立重算2127冻结/当前文件及4段，全部匹配；证据 `C:/Users/kicof/GridopolyReleases/player-input-20260926/candidate-verification.json`。
- bootloader、partition、boot_app0与已部署109df相同。4段偏移0、0x8000、0xe000、0x10000，DIO/80m/16MB；NVS/文件系统不在写入清单。
- 新批量包：`C:/Users/kicof/GridopolyReleases/batch32-2026-09-26-v3/`，release.json SHA `333637703e126e14dee7b936634f0ae6d8525bc9d5b832072c9181dddd7bb9f2`。long/corner仍为V0.33，仅player替换为本修复。打包时全部profile源码清单/二进制/分区校验通过，单COM7清单dry-run通过；v2和旧109df回退文件保留。

## COM7更新与实机验收

2026-09-26 21:31:36 UTC预检通过，21:31:55 UTC批量工具完成四段写入、逐段读验和完整MAC复核，状态VERIFIED；使用固定esptool5.3.1、16MB分区，目标仅COM7 `dc:b4:d9:02:d1:dc`。证据目录 `C:/Users/kicof/AppData/Local/GridopolyPlayerTools-3311/input-fix-com7-20260926/`，其中`flash.jsonl`记录实际偏移、SHA及写验结果，`rollback-109df/`保存已核验的旧普通固件。

用户已被告知更新将重启、未确认本地手写草稿可能需重写。启动采集UTC21:31:55.308–21:32:40.317，实际45.000秒并关闭；正常build/ready各一次，UDP paired seat1/room993580108，WiFi 1.870秒取得IP、5–40秒保持10.42.0.37，无panic/abort/Guru或第二次启动标记。esptool预检及正式写验各执行hard-reset；未见ROM rst日志不能证明串口打开绝无复位扰动。

完整HTTP前后只变化version16→18、identity.revision16→18和serverEpochMs，businessDifferences=[]；online/eligible/seat1、房间、Tag、冻结席位及已确认身份保持。对应`verification.json`已记录，未新建房间、替玩家确认姓名或发送游戏动作。

用户实机反馈：**“手写没有什么问题了”**；同时明确旋钮仍有问题，具体是**“选中框在不同项目之间卡住或回跳”**。因此手写得到本轮用户确认，旋钮验收失败，整个输入修复仍继续。不能把上述43项主机PASS或正常启动当作旋钮现场通过。

下一步用efbe现有INPUT TRACE ON的90秒有界诊断，对照真实Gray采样、step、队列/消费时刻与app焦点；独立审查保留renderer/动画的实际呈现。没有反转旋钮方向，也不先用降速掩盖原因。至少24 FPS和原游戏画面频闪/错位验收仍单独保留。
