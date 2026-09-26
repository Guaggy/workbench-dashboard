# Workbench dashboard

A LilyGO T-Display S3 above the workbench. It has a screen, a 60-LED strip, two pots and six buttons, and is connected to Atlas (the Obsidian vault) and Claude Code through a Raspberry Pi. The background and decisions are in the vault note `Projects/ESP32-S3 Dashboard.md`; this file is the hands-on guide.

## How it fits together

```
Atlas vault (PC)  --Drive backup, read-only-->  BenchPi (Pi Zero W)  --MQTT over Tailscale Funnel-->  dashboard (ESP32-S3)
Dashboard/*.md                                   bench-hub service                                     screen, LEDs, buttons
      ^                                          mosquitto                                                   |
      |                                          ~/bench/outbox/status.md, log.jsonl  <-- habit/focus logs --'
      '-- end-day / atlas-sync copy status + logs into the vault (pull_bench.py)

Claude Code (PC)  --hooks, bench_pub.py-->  the same MQTT broker
```

- **The PC is the only thing that writes to the vault.** The Pi only reads the `Dashboard/` folder, from the Google Drive backup, every 10 min.
- **The residence WiFi isolates devices,** so everything meets at Mosquitto on BenchPi. Tailscale Funnel publishes it on `benchpi.tailec27dc.ts.net:8443` (TLS, password login).

## Hardware

| Part | Pin |
|---|---|
| Buttons A (top-left), B (top-right), C (bottom-left), D (bottom-right) | 44, 13, 43, 10 (to GND, internal pull-ups) |
| Onboard buttons: previous / next page | 0 / 14 |
| Pots: colour (left) / brightness (right) | 1 / 2 |
| WS2812B strip, 60 LEDs, 330 Ω in series, own 5V 3A supply | 16 |
| Display power (must be HIGH when not on USB) | 15 |

## Repository

| Path | What |
|---|---|
| `src/`, `include/config.h` | firmware (PlatformIO). All settings are in `config.h` |
| `include/secrets.h` | WiFi and MQTT passwords, **not in git**. Copy `secrets.example.h` |
| `pi/` | the hub on BenchPi: `bench_hub.py` (service), `vault_feed.py` (reads the vault folder), `status_report.py`, `iss_passes.py`, `bench-hub.service`, tests in `pi/tests/` |
| `tools/ota.py` | firmware update over WiFi |
| `skill-updates/` | the Atlas skills (end-day, atlas-sync, atlas) as uploaded to claude.ai |
| `docs/superpowers/` | design and plan for the vault folder |

Outside the repo:
- **On the PC:** `~/.bench/`, with `claude_notify.py` (Claude Code hooks), `bench_pub.py` (send anything to the dashboard), `pull_bench.py` (status and logs into the vault) and `mqtt.env` (Claude's MQTT login). The hooks are in `~/.claude/settings.json`.
- **On the Pi:** `~/bench/`, with the deployed hub, `.env` (MQTT login, calendar links, NASA key), `vault/` (read-only copy of the folder), `outbox/status.md`, `log.jsonl`, `last_good.json`, and `shortcuts.json` for custom shortcut buttons. rclone's config is in `~/.config/rclone/rclone.conf`, and Mosquitto's in `/etc/mosquitto/conf.d/bench.conf`.

## Common jobs

| Job | How |
|---|---|
| Change content, buttons, pages, habits, presets | edit the vault's `Dashboard/` files (formats in `Dashboard/README.md`), then `python ~/.bench/bench_pub.py shortcut '{"name":"Refresh"}'` |
| Show a picture now | `python ~/.bench/bench_pub.py show '{"url":"https://…","title":"…"}'` |
| Message on the screen | `python ~/.bench/bench_pub.py notify '{"title":"…","text":"…","color":"#50a0ff","anim":"pulse","secs":10}'` |
| Update the firmware | bump `FW_VERSION` in `include/config.h`, then `python tools/ota.py` (USB also works: `pio run -t upload`) |
| Update the hub | `cd pi && python -m unittest discover -s tests`, then `scp bench_hub.py vault_feed.py status_report.py pi@benchpi:~/bench/` and `ssh pi@benchpi sudo systemctl restart bench-hub` |
| Hub log | `ssh pi@benchpi journalctl -u bench-hub -f` |
| Status report | `ssh pi@benchpi cat ~/bench/outbox/status.md`, or `python ~/.bench/pull_bench.py` (copies it into the vault) |

## Things to know

- **Weak WiFi at the bench (−85 to −92 dBm).** Updates retry by themselves. If connections drop, move the dashboard or turn the antenna end towards open space.
- **Pi Zero watchdog:** the 15 s hardware watchdog restarts the Pi during a long `systemctl daemon-reload`. Package installs are covered by an apt hook. For manual reloads, use `sudo safe-daemon-reload`.
- **SSH to the Pi is key-only** (the PC's `~/.ssh/id_ed25519`). The Pi-hole statistics need no password from the Pi itself.
- **The firmware file contains the passwords.** `ota.py` only publishes it on a random one-time path and removes it straight after.
