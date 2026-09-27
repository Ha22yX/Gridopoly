# 本轮硬件提交范围与复核

待总指挥审核/统一提交；本任务不操作共享Git索引。精确候选路径见 release-file-list.txt，共28条。

## 文件归属

三份共享文件 Docs/README.md、Docs/hardware/README.md、Docs/player-console/current-player-console-baseline.md，本轮只修改玩家电池底板简介段落，其他已有差异不自动属于本轮。清单表示路径，不授权将同一路径的未知历史差异一并提交；请审阅hunk。其余三份玩家底板硬件专题文档和独立PlayerConsolePower目录为本轮交付范围。

不纳入长期修改的 Gridopoly.eprj2。原生P1/P2的SVG、PNG仍有已删文字显示缓存，本地保留诊断用途，暂不建议提交；文档中状态不将它们视作视觉QA通过。功能框图和FPC观察方向图可随附，它们不表示实际封装布局或实物验证。

## 可重跑验证

从仓库根执行：

~~~powershell
node 'PCB Files/PlayerConsolePower/DesignDraft-2026-09-27/verify-schematic.cjs'
~~~

只需Node.js标准库，读取本目录快照，不联网、不连接EDA、不触碰硬件，也不写文件。当前输出：68符号、67网表器件、213连接脚、18空网络脚、J2另12脚、79项静态电气/身份检查通过。结果及输入SHA-256见 verification-result.json。

连接比较输入为当前native-netlist与预期清单；J2排除PCB，使用API采集的pin几何/NC快照，并以P2源码SHA-256绑定。verification-baseline.json是遥测修改前的历史连接意图，仅验证旧电源/USB网络保留，里面的J4/NTC及UART NC不代表当前设计。79项检查包括料号、供电和数据手册引脚约束，不是79个硬件测试，也没有清除40条EDA warning。

## 导出范围

export-scope-audit.json记录了两份原生源码各自唯一的SCH_PAGE头（P1/P2）和67器件网表范围。没有包含其他板子的原理图/PCB页面、bridge程序或工具日志。文本扫描未发现所列凭据/授权头/本机用户路径模式。原生源码保留EDA文档、client、库引用UUID、时间及公共商品链接；这些不是访问令牌。

原生页面JSONL保留符号/封装引用，不含可独立恢复的完整器件库定义；需要原工程库环境。不能把本目录作为完整可下单工程包。FPC、电池、ERC、固件、PCB和实测限制均保留。
