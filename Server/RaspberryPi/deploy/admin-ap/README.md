# Dedicated management Wi-Fi

Offline boot was verified on 2026-10-20 with an unreachable home-network target and autoconnect still enabled: both APs and all five Gridopoly services started automatically, and all 16 tiles recovered by the 30-second sample. A real management Wi-Fi client successfully fetched the panel without an upstream route. See the [boot verification record](../../../../Docs/firmware/offline-boot-verification-2026-10-20.md) for evidence, cleanup and the remaining physical cold-power test.

Connect a phone or laptop to **`gridopoly-admin`**, then open **http://10.0.0.1**. Choose to stay connected if the device says this network has no Internet. The password is configured privately on the Pi; it is not included in this repository. This is a local management network, with manual browser access rather than a guaranteed captive-portal popup.

| Connection | Hardware / interface | Purpose |
| --- | --- | --- |
| `gridopoly-admin` | Onboard radio, virtual `gpadmin0` | Management page at `10.0.0.1`; up to 6 management clients |
| Saved home Wi-Fi | Same onboard radio, `wlan0` | Optional upstream connection; NetworkManager handles reconnects |
| `gridopoly` | ALFA USB radio, `gpusb0` | Existing tile/player network at `10.42.0.1` |

The management service starts at boot without waiting for home Wi-Fi. It does not change the USB AP, module credentials, game process, or game state.

## How it works

The onboard driver must support a managed station and an AP concurrently. On the current Pi they share one 2.4 GHz channel: the management AP follows the associated home network. Without that network, it uses its last channel, or channel 6 on first installation. A home channel change can briefly reconnect management clients. Home Wi-Fi must be restricted to 2.4 GHz channels 1–11 for this US configuration.

While the service runs it disables power saving on the shared onboard radio, including after NetworkManager reconnects the station. Leaving station power saving enabled caused repeated AP request timeouts during the real offline test. The saved home profile is not rewritten; stopping the management service restores the initial runtime power-save setting.

The home router also uses `10.0.0.1`. Assigning that address to an ordinary local interface would break the Pi's upstream routing. Instead, `gpadmin0` is attached to the `gpadmin` VRF (table 10430) **before** receiving its address. Management clients use their own connected route, while the Pi's normal route to the home router stays on `wlan0`.

The existing wildcard TCP listener serves the panel through `tcp_l3mdev_accept=1`; the prior setting is restored when this service stops. This applies to other wildcard TCP services, including SSH: the VRF separates routing, not application authentication. UDP settings are unchanged. A separate dnsmasq runs inside the VRF and leases `10.0.0.20–10.0.0.80`; `gridopoly.admin` resolves locally. No Internet forwarding or upstream DNS is configured.

## Install

Requirements: Linux with VRF support, Python 3, NetworkManager, `ip`, `iw`, `hostapd`, `hostapd_cli`, `dnsmasq`, and systemd. This deployment targets the existing Gridopoly Pi and its interface names, rather than arbitrary adapters.

1. Inspect `iw phy` interface combinations and confirm onboard AP + station support. Check that `gpadmin`, `gpadmin0`, and routing table 10430 are unused.
2. Keep the existing module network working. For a remote installation, retain another access path and prepare a bounded rollback before applying network changes.
3. Copy `admin-ap.example.json` to a private file outside the repository, set its passphrase, and restrict its permissions to `0600`. The management SSID must differ from `gridopoly`.
4. From this directory, run:

```sh
sudo sh install.sh /absolute/path/to/private-admin-ap.json
```

The installer validates the configuration, installs `/etc/gridopoly/admin-ap.json` privately, reloads NetworkManager configuration without restarting NetworkManager, and enables/starts `gridopoly-admin-ap.service`. It does not create or modify the home connection profile. The existing profile must have autoconnect enabled and use the 2.4 GHz band.

## Verify and recover

```sh
systemctl is-enabled gridopoly-admin-ap
systemctl is-active gridopoly-admin-ap gridopoly gridopoly-ap gridopoly-dnsmasq
sudo hostapd_cli -p /run/gridopoly-admin-ap/hostapd -i gpadmin0 status
sudo journalctl -u gridopoly-admin-ap -n 30 --no-pager
ip route get 10.0.0.1
ip route show table 10430
```

Then connect a real Wi-Fi client, verify a DHCP lease, open the page, and check that existing modules remain online. If the client also has Ethernet on `10.0.0.0/24`, its routing may select the home router instead: test from a phone on the management hotspot, or explicitly bind a diagnostic connection to the Wi-Fi interface.

To disable this feature while preserving the game and module hotspot:

```sh
sudo systemctl disable --now gridopoly-admin-ap
```

Stopping removes only this service's tagged virtual interfaces and exact unreachable route, and restores its saved TCP and radio power-save settings. The configuration and unit remain available for re-enabling. The supervisor retries failed child services and rebuilds missing management interfaces via systemd; it refuses to adopt/delete an interface owned by another service.

Linux regression checks (no radio/root needed):

```sh
python3 tests/deploy/test_admin_ap.py
```

Run that command from the repository root. For actual deployment results and remaining physical checks, see the [2026-10-19 record](../../../../Docs/firmware/admin-hotspot-2026-10-19.md).

Implementation references: [Linux VRF routing and socket behavior](https://docs.kernel.org/networking/vrf.html), [NetworkManager unmanaged-device matching](https://networkmanager.dev/docs/api/latest/NetworkManager.conf.html), [NetworkManager wireless band settings](https://www.networkmanager.dev/docs/api/latest/settings-802-11-wireless.html). Radio concurrency was checked against the installed Pi's own `iw phy` output.
