# Avatar Setup旋钮连跳与顺序异常：调查和修复进度

## 用户症状与证据界限

2026-09-08用户报告新建对局Avatar Setup选中字段后，一格旋转偶发连续跳多个预设，顺序也不完全正确。该问题新增到正在进行的玩家24FPS/断线恢复工作，不替换原目标。

当前正常COM7仍为ded1375634d29cc454ce401dd7e1da56373e91055d2bd464a865bee3d3c31a8c。90秒只读观察opened1788911819560、closed1788911909585，无写入/reset，原始文件在本机GridopolyPlayerTools-3311/avatar-input-baseline-20260908-1957。刚打开时USB有积压旧日志，旧GRIDOPOLY_INPUT没有deviceMs、原始GPIO或recipe值；不能把同一host批次当作一次物理转动，也不能把用户简短“1”回复反推成完整5+5步验证通过。用户随后明确该问题是小概率。

## 已确认的源码缺陷

1. 原RotaryInputFilter仅拒绝35ms内反向事件，接受同向返回。因此原始+1/-1/+1净1，输出却是+1/+1净2。主任务用真实旧header单独MSVC编译并执行复现，before.exe输出raw_net=1 applied_net=2、exit1。旧header、reproducer和独立ESP32编译日志保存在本机Temp/gridopoly-rotary-review，未把人为输入冒充物理GPIO捕获。
2. 原ESP32_Knob 0.0.1使用3ms定时、A/B分别两次采样防抖。READY同时存在A/B变化标志时固定选A，下一轮会消费留下的B并发方向事件；没有验证联合Gray变化是否合法。两路同时跳变或采样漏过中间相位时不应猜测方向。
3. appHandleInput/Avatar applyIdentityDraftDelta没有多步加速：按delta对字段编号循环，队列每轮拆成±1。仅显示刷新变慢不会自己改变recipe，但会使积压输入集中呈现。
4. 玩家端审查发现previewDescriptor/front-buffer由后台改写，LVGL可能仍读旧front；旧头像页面删除之前还可能释放它的图像。另UI忽略frame.exact=false，把新选项文字与上一张头像并列且隐藏更新提示。这些显示问题由玩家任务修复，不能因此否认输入缺陷。

服务端只读核对30/30 GAVC文件SHA及kind/preset编号与仓库manifest一致，按numeric1..10寻址；20发色/8肤色着色RGB一致，重复相同recipe的RGB565与ETag一致。没有发现h1/h10/h2字典排序或服务端编号串位。仅Snow white UI色块0xEBEAEA应为0xEBEEEA，交玩家一并修正。证据在Temp/gridopoly-avatar-catalog-audit-20260909.json，已核对上述显示行为存在于ded137实际源码快照。

## 输入修复实现与验证

替换ESP_Knob回调适配为项目内RotaryQuadratureDecoder：同一次GPIO_IN_REG读取A/B，1ms ESPtimer采样，两次跨至少1ms的相位一致才接受；同一时刻的补发timer调用不能当作重复稳定观察。合法单比特Gray转移累计到00/11端点输出一步，保留原驱动半周期粒度及六点钟安装的单次方向反转。接点返回会抵消未完成相位，双位非法变化不猜方向，重同步后继续；快速真实反转不再被35ms单向过滤删除。

输入队列仍固定32项、同向合并后每轮消费±1，按钮边界不合并。新增64项有界原始相位诊断，记录deviceMs/rawAB/stableAB/emittedStep/invalidTransitions/traceDropped/queueEntries；回调不打印、不分配。主循环诊断开关与Avatar前后编号日志由玩家任务集成，输出不得反过来占用高频回调。

`tools/test-rotary-input.ps1`执行与设备共用的15项测试通过：两种端点、快速反转、接点返回、+/-/+净位移、非法双位/重同步、启动在半途、同刻采样、millis回绕、4000次快速合法双向变化、Avatar五字段正反序及环绕。逻辑自检另增加队列反转净位移与按钮顺序测试，待完整设备SelfTest。hardware_input.cpp已用实际ESP32S3 fresh构建的compile_commands重定向当前源和独立对象编译exit0，确认GPIO寄存器/esp_timer API适配；不等于最终固件链接、烧录或物理操作通过。

## 待验收

输入修复源码已交接玩家任务合入缓存/绘制修复。尚需最终fresh构建、完整SelfTest及24FPS实测、正常候选部署、真实单格/反转原始相位到recipe的对应验证，再做已授权断线窗口。用户尚有未提交头像/名字草稿，已请其完成确认，期间不重刷COM7；这不阻挡代码与构建继续。没有使用定时任务，也未修改线上房间、头像草稿或玩家资产。
