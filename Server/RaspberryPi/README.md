# Gridopoly Raspberry Pi Server

双向交易的冻结 wire schema、幂等与恢复规则见
[`Docs/firmware/trade-protocol.md`](../../Docs/firmware/trade-protocol.md)。交易使用独立按需
`TradeRequest/TradeResponse`，不会扩大常规 State/Authority/Roster 广播。

正式入口请阅读：

- [树莓派权威服务端](../../Docs/firmware/raspberry-pi-server.md)
- [玩家屏 Wi‑Fi/UDP 协议](../../Docs/firmware/wifi-udp-player-protocol.md)
- [头像组件流协议](../../Docs/firmware/avatar-component-protocol.md)

快速构建：

```bash
cmake -S ../.. -B ../../build-pi -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build ../../build-pi
ctest --test-dir ../../build-pi --output-on-failure
```

部署后的入口：

- 家庭局域网测试：`http://<raspberry-pi-ip>/`
- 玩家独立网络：`http://10.42.0.1/`
- 玩家二进制协议：UDP `10.42.0.1:4242`

网页右上角“设置”可在运行时调整机器人动作间隔。允许范围为
`100–10000 ms`，推荐测试值为 `800–2000 ms`；保存后立即生效，并持久化到
`authority.meta`，服务或树莓派重启后继续使用。对应接口为：

- `GET /api/settings`：读取当前值和允许范围。
- `POST /api/settings?botIntervalMs=<毫秒>`：验证并保存新值。

生产进程由 `gridopoly.service`、`gridopoly-ap.service` 和
`gridopoly-dnsmasq.service` 管理。不要再并行启动发行版默认的
`hostapd.service` 或 `dnsmasq.service`。

真实 PSK、AP 密码和家庭 Wi‑Fi 凭据不得写入本目录或提交到 Git。

头像编辑阶段通过 `/assets/avatar-components/v1/<kind>/<id>.gavc` 按结构层加载；颜色由圆屏
本地定点着色。服务端最终头像与旧整图预览接口使用同一组中性组件和同一整数合成算法。
部署前执行 `python Server/RaspberryPi/tools/verify-avatar-components.py`，安装器会将 30 个
GAVC 文件原子切换到 `/usr/local/share/gridopoly/avatar/components-v1`。
