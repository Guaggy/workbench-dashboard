# Workbench dashboard

A LilyGO T-Display S3 mounted above my desk. It shows the focus timer, tasks, calendar, Claude Code alerts and home stats, and it has a 60-LED strip, two pots and six buttons. Firmware runs on the ESP32-S3; a small Raspberry Pi hub on the same network feeds it data over MQTT.

## Hardware

| Part | Pin |
|---|---|
| Buttons A, B, C, D | 44, 13, 43, 10 (to GND, internal pull-ups) |
| Onboard buttons: previous / next page | 0 / 14 |
| Pots: colour / brightness | 1 / 2 |
| WS2812B strip, 60 LEDs, 330 Ω in series, own 5 V supply | 16 |
| Display power (must be HIGH when not on USB) | 15 |

## Architecture

```
Dashboard (ESP32-S3)  <--MQTT-->  Raspberry Pi hub (Mosquitto + bench_hub.py)  <--MQTT-->  PC scripts / Claude Code hooks
```

- `src/` and `include/config.h`: firmware. All tunable settings live in `config.h`.
- `tools/ota.py`: firmware update over WiFi.

## Build and flash

Built with [PlatformIO](https://platformio.org/).

```
cp include/secrets.example.h include/secrets.h   # add your WiFi and MQTT details
pio run -t upload                                 # USB
python tools/ota.py                               # over WiFi, after the first flash
```

`secrets.h` is ignored by git, so credentials stay out of the repo.

## Hub

The hub runs as a systemd service on a Raspberry Pi Zero W. Its code (the hub service, a vault feed that reads a folder of Markdown files, and a status report) lives in a separate private repo with the rest of that Pi's setup.

## Notes

- WiFi signal at the bench was weak (around −85 to −92 dBm), so the firmware retries connections on its own.
