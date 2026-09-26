# Dashboard folder: design

Date: 2026-09-26. Status: draft for Even's review.

## Goal

Replace the single `meta/dashboard.md` with a `Dashboard/` folder in the Atlas vault. Content is split into separate files that Atlas can maintain independently. A light config file lets Even (or Atlas, on request) change the Home buttons, habits, light presets, night hours and which pages are shown, without new firmware. A `status.md` in the same folder tells Atlas how the dashboard and Pi are doing.

Out of scope: layout control, new page types, cards/checklists/flashcards (possible later on top of this).

## Rules that don't change

- The PC is the only writer of the vault. The Pi only **reads** the folder (rclone, read-only Drive login) and **never** writes to it.
- The ESP32 only talks MQTT with the Pi. All parsing happens on the Pi.

## The folder (`C:\AI\Atlas\Dashboard\`)

All files use the current format: `##` headings, `- ` list items, fields split by ` | `.

| File | Contents | Written by |
|---|---|---|
| `README.md` | the format of every file below, for Atlas to read before editing. The Pi ignores it | once, then rarely |
| `config.md` | `## Pages`, `## Buttons`, `## Habits`, `## Light presets`, `## Night` | Even / Atlas on request |
| `today.md` | `updated: YYYY-MM-DD`, `## Message`, `## Focus`, `## Todos`, `## Events` | Atlas daily (`end-day`) |
| `dates.md` | `## Deadlines`, `## Countdowns`, `## Space events` | Atlas when tasks change |
| `quotes.md` | `- text \| author` | Atlas, grows over time |
| `facts.md`, `fortunes.md` | one per line | Atlas, grows over time |
| `images/*.txt` | one pixel-art picture per file (`title:`, `palette:`, rows in a code block), named `YYYY-MM-DD-name.txt` | Atlas, regularly |
| `images/links.md` | `- https://… \| title`, online pictures to show | Atlas, now and then |
| `status.md` | health and activity report (see below) | the **PC**, copied from the Pi during `end-day` / `atlas-sync` |

### `config.md` in detail

```
## Pages
- Home
- Bench light
- Focus
- Tasks
- Habits
- Today
- Space
- Stats
- Gallery

## Buttons
- Refresh | shortcut | Refresh
- Focus 50 | timer | 50
- Typing | habit | Typing
- Solder | light | Solder

## Habits
- Typing
- Exercise
- Read
- Tidy bench

## Light presets
- Solder | #d0e0ff | 255
- Evening | #ff9040 | 80
- Focus | #fff0e0 | 160

## Night
23:00-07:00
```

- **Pages:** the order they appear in, and a page left out is hidden. Home is always shown, even if left out. Names must match the fixed list.
- **Buttons:** the four Home buttons, A to D. `label | action | argument`, with four actions:
  - `shortcut <name>`: runs a Pi shortcut (built-ins: Refresh, No ads 5m, Ads on, Today; others come from `~/bench/shortcuts.json`)
  - `timer <minutes>`: sets the focus timer and starts it. The page doesn't change; the countdown shows in the top bar as it does today
  - `habit <name>`: logs that habit (must be in `## Habits`)
  - `light <preset>`: switches the strip to that preset (the pots take over again when turned)
- **Light presets:** `name | #rrggbb | brightness 0-255`. Button C on the Bench light page cycles through them.

## Pi (`bench-hub`)

- rclone's `gdrive:` remote is re-rooted at the `Dashboard` folder instead of `meta`, and it **syncs the whole folder** to `~/bench/vault/` every 10 minutes.
- Each file is parsed on its own. **A file with errors is skipped and the last good version of it is kept.** The errors (file, line, reason) go into the status report, and a short banner appears on the dashboard once.
- **Content rotation:**
  - Quote of the day: picked by date from `quotes.md`, so it's the same all day.
  - Facts and fortunes: 12 random ones each day, since the firmware holds 12.
  - Pixel art of the day: the newest `images/` file dated today or earlier.
  - Photos: the photo slot takes turns every 3 hours between the NASA photo and the pictures in `links.md` (cropped to 320×170 like the NASA one).
- Publishes the same topics as today (`feed`, `events`, `pixelart`, `space`, `photo`), plus the new config fields in `feed`: `pages`, `buttons`, `presets`.
- **Status outbox:** every 10 min the hub writes `~/bench/outbox/status.md`:
  - **Dashboard:** online/offline, firmware version, WiFi signal, last seen
  - **Pi:** temperature, disk free, Pi-hole on/off, `bench-hub` / Mosquitto / Funnel running
  - **Sync:** when the folder was last read, and every file error
  - **Today:** focus minutes and habits logged so far

## Dashboard firmware

- Pages: shown and ordered from `feed.pages`, with the current fixed order as the fallback.
- Home buttons: labels and actions from `feed.buttons`, with the current shortcuts as the fallback. The four actions run on the device, except `shortcut`, which publishes to the Pi as today.
- Light presets: kept in RAM. `light <preset>` or button C on the Bench light page sets colour and brightness. Moving a pot hands control back to the pots.
- Habits: from `config.md` as today.
- Everything else is unchanged.

## Atlas / PC

- `pull_logs.py` becomes `pull_bench.py`. It fetches the logs as today, **and copies `status.md` into `Dashboard/status.md`**. `--done` still clears the logs only.
- The skills are updated once more and re-uploaded by Even:
  - `end-day` writes `today.md` (for tomorrow), adds a pixel-art file to `images/`, tops up quotes, facts and fortunes, and reads `status.md` (and fixes any file errors listed there).
  - `atlas-sync` refreshes `dates.md` and `status.md`.
  - `atlas` updates `dates.md` / `today.md` when tasks change, and edits `config.md` when Even asks to change buttons, habits, presets or pages. After an edit it presses Refresh through `bench_pub.py`, so the change shows within a minute.
- Migration: the current `meta/dashboard.md` content moves into the new files, and `meta/dashboard.md` is deleted once the Pi reads the folder.

## Testing

- Parser: run it on the real folder on the PC, plus one deliberately broken copy of each file. Check that the errors are reported and the last good version is kept.
- Pi: after deploying, check every topic on the broker and the outbox `status.md`.
- Firmware: test the page order, hiding a page, all four button actions and the presets on the real hardware, then install over WiFi with `tools/ota.py`.
- End to end: edit `config.md` on the PC, press Refresh, and see the change on the screen.
