# Dashboard Folder Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `meta/dashboard.md` with a `Dashboard/` vault folder (split content files + a light `config.md`), have BenchPi read it with per-file validation and write a status report, and let the firmware apply configured pages, Home buttons and light presets.

**Architecture:** The PC stays the only vault writer. BenchPi syncs the folder read-only (rclone) into `~/bench/vault/`, parses each file with `pi/vault_feed.py` (last good version kept per file), publishes the same MQTT topics as today plus `pages`/`buttons`/`presets` in `bench/feed`, and writes `~/bench/outbox/status.md`. `end-day` / `atlas-sync` copy that status file into the vault with `pull_bench.py`. The ESP32 gets a small amount of new logic: page order/visibility, four button actions, and local light presets.

**Tech Stack:** Python 3.13 (stdlib `unittest`, paho-mqtt 2.x, requests, Pillow, rclone 1.60 on the Pi), C++ Arduino core 3.2.1 with TFT_eSPI/FastLED/ArduinoJson 7 (PlatformIO), Markdown in the Obsidian vault.

**Spec:** `docs/superpowers/specs/2026-09-26-dashboard-folder-design.md`

**Note:** the project is not a git repository, so the usual "commit" steps are "checkpoint" steps: confirm the tests/build pass before moving on.

## Global Constraints

- The PC is the only writer of the vault (`C:\AI\Atlas`). The Pi only reads the folder (rclone, read-only Drive login) and never writes to it.
- The ESP32 only talks MQTT with the Pi. All parsing happens on the Pi.
- File format everywhere: `##` headings, `- ` list items, fields split by ` | `.
- Page names (fixed list, exact spelling): Home, Bench light, Focus, Tasks, Habits, Today, Space, Stats, Gallery. Home is always shown.
- Button actions: `shortcut <name>`, `timer <minutes>` (1-180), `habit <name>` (must be in `## Habits`), `light <preset>` (must be in `## Light presets`). Max 4 buttons, max 4 habits.
- Light preset: `name | #rrggbb | brightness 0-255`.
- Timed events: `HH:MM | anim | #rrggbb | seconds | text`, animations: pulse, flash, rainbow, confetti, chase, sunrise. They apply only on the `updated:` date of `today.md`.
- Facts and fortunes: 12 random per day. Quote of the day picked by date. Pixel art: newest `images/YYYY-MM-DD-name.txt` dated today or earlier.
- A file with errors is skipped and its last good version is kept. Errors go into the status report, plus one banner on the dashboard per new set of errors.
- Pi Zero W: a manual `systemctl daemon-reload` reboots it through the watchdog; use `sudo safe-daemon-reload`. Keep the hub light (it shares the Pi with Pi-hole).
- Never read `~/bench/.env` with `. ~/bench/.env` in a shell (a value contains spaces). Python reads it.

## Review Focus

- A file is missing (e.g. no `quotes.md` yet): the others still publish, missing content is empty, nothing crashes. Test in Task 1.
- A file goes from good to broken: the last good version keeps being used and the error is reported. Test in Task 1.
- First run with an empty or half-filled folder and no last good versions: `build()` returns valid empty defaults and the firmware falls back to its built-in pages/buttons. Test in Task 1 (`build({})`).
- Files saved by Obsidian/Windows with `\r\n` line endings or a UTF-8 BOM: parsed exactly like plain files. Test in Task 1.
- A button pointing at a habit or preset that doesn't exist: reported as a file error on the Pi, never sent to the device. Test in Task 1.

---

## File structure

| Path | Responsibility |
|---|---|
| `C:\AI\Atlas\Dashboard\README.md`, `config.md`, `today.md`, `dates.md`, `quotes.md`, `facts.md`, `fortunes.md`, `images\2026-09-26-launch-night.txt`, `images\links.md` | the vault folder (Task 5) |
| `pi/vault_feed.py` (new, replaces `pi/dashboard_feed.py`) | parse + validate each file, keep last good versions, build the MQTT payloads |
| `pi/status_report.py` (new) | render `status.md` from a dict |
| `pi/tests/test_vault_feed.py`, `pi/tests/test_status_report.py` (new) | unittest suites |
| `pi/bench_hub.py` (modify) | folder sync, rotation, photo rotation, device tracking, status outbox |
| `C:\Users\evenm\.bench\pull_bench.py` (new, replaces `pull_logs.py`) | PC side: copy status into the vault, print/clear logs |
| `src/state.h`, `src/net.cpp`, `src/leds.h`, `src/leds.cpp`, `src/ui.cpp`, `src/main.cpp`, `include/config.h` (modify) | firmware: pages, buttons, presets |
| `skill-updates/{end-day,atlas-sync,atlas}/SKILL.md` + zips (modify) | Atlas skills for the folder |

---

### Task 1: Folder parser (`pi/vault_feed.py`)

**Files:**
- Create: `pi/vault_feed.py`
- Create: `pi/tests/test_vault_feed.py`
- Delete (in Task 7, after deploy): `pi/dashboard_feed.py`

**Interfaces:**
- Produces:
  - `class ParseError(ValueError)`
  - `load_folder(folder: str, last_good: dict) -> tuple[dict, list[str]]`: parsed data keyed by file name (`"config.md"`, `"today.md"`, `"dates.md"`, `"quotes.md"`, `"facts.md"`, `"fortunes.md"`, `"images/links.md"`) plus `"images"` (`{filename: pixelart}`), and a list of `"file: line N: reason"` errors. Mutates `last_good`.
  - `build(data: dict, today: str) -> dict` with keys `feed` (dict for `bench/feed`), `events` (list), `space_events` (list), `pixelart` (dict or None), `links` (list of `{"url","title"}`).
  - `feed` keys: `deadlines`, `countdowns`, `todos`, `message`, `focus`, `habits`, `pages`, `buttons` (`[{"label","action","arg"}]`), `presets` (`[{"name","color","brightness"}]`), `quote` (`{"text","author"}`), `facts`, `fortunes`, optional `night` (`{"start","end"}`).

- [ ] **Step 1: Write the failing tests**

`pi/tests/test_vault_feed.py`:

```python
import os
import tempfile
import unittest

import vault_feed as vf

CONFIG = """# Config

## Pages
- Home
- Focus
- Tasks

## Buttons
- Refresh | shortcut | Refresh
- Focus 50 | timer | 50
- Typing | habit | Typing
- Solder | light | Solder

## Habits
- Typing
- Read

## Light presets
- Solder | #d0e0ff | 255
- Evening | #ff9040 | 80

## Night
23:00-07:00
"""

TODAY = """# Today
updated: 2026-09-27

## Message
Start the GENG draft.

## Focus
ESP32-S3 Dashboard

## Todos
- Draft part 1
- Typing

## Events
- 19:00 | chase | #00c850 | 8 | Typing time
"""

DATES = """## Deadlines
- 2026-10-01 | GENG4405 A2 Part 1

## Countdowns
- 2026-12-18 | Dune: Part Three

## Space events
- 2026-10-21 | Orionids peak
"""

ART = """title: Rocket
palette: r=#e03c28 w=#f0f0f0

```
.r.
rwr
```
"""


def write(folder, name, text, newline="\n", bom=False):
    path = os.path.join(folder, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8-sig" if bom else "utf-8", newline=newline) as f:
        f.write(text)


class ParseConfig(unittest.TestCase):
    def test_full_config(self):
        c = vf.parse_config(CONFIG)
        self.assertEqual(c["pages"], ["Home", "Focus", "Tasks"])
        self.assertEqual(c["buttons"][1], {"label": "Focus 50", "action": "timer", "arg": "50"})
        self.assertEqual(c["habits"], ["Typing", "Read"])
        self.assertEqual(c["presets"][0], {"name": "Solder", "color": "#d0e0ff", "brightness": 255})
        self.assertEqual(c["night"], {"start": "23:00", "end": "07:00"})

    def test_home_is_always_first(self):
        c = vf.parse_config("## Pages\n- Tasks\n- Focus\n")
        self.assertEqual(c["pages"], ["Home", "Tasks", "Focus"])

    def test_duplicate_page_is_listed_once(self):
        c = vf.parse_config("## Pages\n- Home\n- Tasks\n- Tasks\n")
        self.assertEqual(c["pages"], ["Home", "Tasks"])

    def test_unknown_page(self):
        with self.assertRaisesRegex(vf.ParseError, "line 2: unknown page 'Weather'"):
            vf.parse_config("## Pages\n- Weather\n")

    def test_unknown_action(self):
        with self.assertRaisesRegex(vf.ParseError, "unknown action 'dance'"):
            vf.parse_config("## Buttons\n- X | dance | now\n")

    def test_button_habit_must_exist(self):
        with self.assertRaisesRegex(vf.ParseError, "habit 'Gym' is not in ## Habits"):
            vf.parse_config("## Habits\n- Read\n\n## Buttons\n- Gym | habit | Gym\n")

    def test_button_preset_must_exist(self):
        with self.assertRaisesRegex(vf.ParseError, "preset 'Party' is not in ## Light presets"):
            vf.parse_config("## Buttons\n- Party | light | Party\n")

    def test_timer_range(self):
        with self.assertRaisesRegex(vf.ParseError, "timer needs minutes 1-180"):
            vf.parse_config("## Buttons\n- Long | timer | 500\n")

    def test_bad_preset_colour(self):
        with self.assertRaisesRegex(vf.ParseError, "line 2: preset needs"):
            vf.parse_config("## Light presets\n- Red | red | 100\n")

    def test_too_many_buttons(self):
        text = "## Buttons\n" + "".join(f"- B{i} | shortcut | Refresh\n" for i in range(5))
        with self.assertRaisesRegex(vf.ParseError, "max 4 buttons"):
            vf.parse_config(text)

    def test_bad_night(self):
        with self.assertRaisesRegex(vf.ParseError, "night needs HH:MM-HH:MM"):
            vf.parse_config("## Night\nlate\n")


class ParseOtherFiles(unittest.TestCase):
    def test_today(self):
        t = vf.parse_today(TODAY)
        self.assertEqual(t["updated"], "2026-09-27")
        self.assertEqual(t["message"], "Start the GENG draft.")
        self.assertEqual(t["todos"], ["Draft part 1", "Typing"])
        self.assertEqual(t["events"][0], {"time": "19:00", "anim": "chase", "color": "#00c850", "secs": 8, "text": "Typing time"})

    def test_today_needs_updated(self):
        with self.assertRaisesRegex(vf.ParseError, "missing 'updated: YYYY-MM-DD'"):
            vf.parse_today("## Message\nhi\n")

    def test_bad_event(self):
        with self.assertRaisesRegex(vf.ParseError, "event needs"):
            vf.parse_today("updated: 2026-09-27\n## Events\n- 7pm | chase | #00c850 | 8 | x\n")

    def test_dates(self):
        d = vf.parse_dates(DATES)
        self.assertEqual(d["deadlines"], [{"date": "2026-10-01", "title": "GENG4405 A2 Part 1"}])
        self.assertEqual(d["space_events"][0]["title"], "Orionids peak")

    def test_bad_date(self):
        with self.assertRaisesRegex(vf.ParseError, "date must be YYYY-MM-DD"):
            vf.parse_dates("## Deadlines\n- 1 Oct | GENG\n")

    def test_quotes_facts_links(self):
        self.assertEqual(vf.parse_quotes("- Well done. | Franklin\n- Anonymous words\n"),
                         [{"text": "Well done.", "author": "Franklin"}, {"text": "Anonymous words", "author": ""}])
        self.assertEqual(vf.parse_list("- A fact | with a bar\n- Another\n"), ["A fact | with a bar", "Another"])
        self.assertEqual(vf.parse_links("- https://x.org/a.jpg | A\n"), [{"url": "https://x.org/a.jpg", "title": "A"}])
        with self.assertRaisesRegex(vf.ParseError, "must start with https://"):
            vf.parse_links("- http://x.org/a.jpg\n")

    def test_pixelart(self):
        a = vf.parse_pixelart(ART)
        self.assertEqual((a["title"], a["w"], a["h"]), ("Rocket", 3, 2))
        self.assertEqual(a["palette"], {"r": "#e03c28", "w": "#f0f0f0"})
        with self.assertRaisesRegex(vf.ParseError, "different lengths"):
            vf.parse_pixelart("```\n..\n...\n```\n")


class LoadFolder(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.folder = self.dir.name

    def tearDown(self):
        self.dir.cleanup()

    def test_missing_files_are_fine(self):
        write(self.folder, "config.md", CONFIG)
        data, errors = vf.load_folder(self.folder, {})
        self.assertEqual(errors, [])
        self.assertIsNone(data["quotes.md"])
        self.assertEqual(data["images"], {})

    def test_broken_file_keeps_last_good(self):
        last_good = {}
        write(self.folder, "config.md", CONFIG)
        vf.load_folder(self.folder, last_good)
        write(self.folder, "config.md", "## Pages\n- Weather\n")
        data, errors = vf.load_folder(self.folder, last_good)
        self.assertEqual(data["config.md"]["pages"], ["Home", "Focus", "Tasks"])
        self.assertEqual(errors, ["config.md: line 2: unknown page 'Weather'"])

    def test_windows_newlines_and_bom(self):
        write(self.folder, "config.md", CONFIG, newline="\r\n", bom=True)
        write(self.folder, "images/2026-09-26-rocket.txt", ART, newline="\r\n", bom=True)
        data, errors = vf.load_folder(self.folder, {})
        self.assertEqual(errors, [])
        self.assertEqual(data["config.md"]["pages"], ["Home", "Focus", "Tasks"])
        self.assertEqual(data["images"]["2026-09-26-rocket.txt"]["w"], 3)

    def test_broken_image_reported(self):
        write(self.folder, "images/2026-09-26-bad.txt", "no code block\n")
        data, errors = vf.load_folder(self.folder, {})
        self.assertEqual(data["images"], {})
        self.assertEqual(errors, ["images/2026-09-26-bad.txt: no rows in a ``` code block"])


class Build(unittest.TestCase):
    def test_empty_data_gives_defaults(self):
        b = vf.build({}, "2026-09-27")
        self.assertEqual(b["feed"]["pages"], [])
        self.assertEqual(b["feed"]["buttons"], [])
        self.assertEqual(b["feed"]["quote"], {"text": "", "author": ""})
        self.assertEqual(b["events"], [])
        self.assertIsNone(b["pixelart"])
        self.assertNotIn("night", b["feed"])

    def test_events_only_on_their_day(self):
        data = {"today.md": vf.parse_today(TODAY)}
        self.assertEqual(len(vf.build(data, "2026-09-27")["events"]), 1)
        self.assertEqual(vf.build(data, "2026-09-28")["events"], [])

    def test_rotation_is_stable_within_a_day(self):
        data = {"quotes.md": [{"text": f"q{i}", "author": ""} for i in range(5)],
                "facts.md": [f"f{i}" for i in range(30)], "fortunes.md": ["only one"]}
        a, b = vf.build(data, "2026-09-27"), vf.build(data, "2026-09-27")
        self.assertEqual(a["feed"]["quote"], b["feed"]["quote"])
        self.assertEqual(a["feed"]["facts"], b["feed"]["facts"])
        self.assertEqual(len(a["feed"]["facts"]), 12)
        self.assertEqual(a["feed"]["fortunes"], ["only one"])
        self.assertNotEqual(a["feed"]["quote"], vf.build(data, "2026-09-28")["feed"]["quote"])

    def test_pixelart_newest_not_in_future(self):
        art = vf.parse_pixelart(ART)
        images = {"2026-09-20-old.txt": dict(art, title="old"), "2026-09-27-today.txt": dict(art, title="today"),
                  "2026-09-30-future.txt": dict(art, title="future"), "undated.txt": dict(art, title="undated")}
        self.assertEqual(vf.build({"images": images}, "2026-09-27")["pixelart"]["title"], "today")
        self.assertEqual(vf.build({"images": {"undated.txt": images["undated.txt"]}}, "2026-09-27")["pixelart"]["title"], "undated")


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run (from the project folder): `cd pi && python -m unittest discover -s tests -v`
Expected: FAIL/ERROR with `ModuleNotFoundError: No module named 'vault_feed'`

- [ ] **Step 3: Write the implementation**

`pi/vault_feed.py`:

```python
# reads the vault's Dashboard/ folder and turns it into the dashboard's mqtt messages
# every file is checked on its own: a broken file is skipped and its last good version is used
import os
import random
import re
from datetime import date

PAGE_NAMES = ["Home", "Bench light", "Focus", "Tasks", "Habits", "Today", "Space", "Stats", "Gallery"]
ACTIONS = ["habit", "light", "shortcut", "timer"]
ANIMS = {"pulse", "flash", "rainbow", "confetti", "chase", "sunrise"}
MAX_PICKS = 12  # facts and fortunes per day, what the firmware holds

HHMM = re.compile(r"^([01]\d|2[0-3]):[0-5]\d$")
DATE = re.compile(r"^\d{4}-\d{2}-\d{2}$")
COLOUR = re.compile(r"^#[0-9a-fA-F]{6}$")


class ParseError(ValueError):
    pass


# ---------- helpers ----------

def numbered(text):
    return list(enumerate(text.splitlines(), 1))


def split_sections(text):
    # {"heading": [(line_number, line), ...]}
    sections, current = {}, None
    for number, line in numbered(text):
        if line.startswith("## "):
            current = line[3:].strip().lower()
            sections[current] = []
        elif current is not None:
            sections[current].append((number, line))
    return sections


def items(lines, fields=1):
    # "- a | b" -> [(line_number, ["a", "b"])], with a minimum number of fields
    out = []
    for number, line in lines:
        if not line.startswith("- "):
            continue
        parts = [p.strip() for p in line[2:].split("|")]
        if len(parts) < fields:
            raise ParseError(f"line {number}: expected {fields} fields split by ' | '")
        out.append((number, parts))
    return out


def first_line(lines):
    for _, line in lines:
        if line.strip():
            return line.strip()
    return ""


def dated(lines):
    out = []
    for number, parts in items(lines, 2):
        if not DATE.match(parts[0]):
            raise ParseError(f"line {number}: date must be YYYY-MM-DD")
        out.append({"date": parts[0], "title": parts[1]})
    return out


# ---------- one parser per file ----------

def parse_config(text):
    s = split_sections(text)

    pages = []
    for number, parts in items(s.get("pages", [])):
        if parts[0] not in PAGE_NAMES:
            raise ParseError(f"line {number}: unknown page '{parts[0]}'")
        if parts[0] not in pages:
            pages.append(parts[0])
    if pages and pages[0] != "Home":
        pages = ["Home"] + [p for p in pages if p != "Home"]

    habits = [parts[0] for _, parts in items(s.get("habits", []))]
    if len(habits) > 4:
        raise ParseError("max 4 habits")

    presets = []
    for number, parts in items(s.get("light presets", []), 3):
        name, colour, level = parts[:3]
        if not COLOUR.match(colour) or not level.isdigit() or int(level) > 255:
            raise ParseError(f"line {number}: preset needs 'name | #rrggbb | 0-255'")
        presets.append({"name": name, "color": colour, "brightness": int(level)})

    buttons = []
    for number, parts in items(s.get("buttons", []), 3):
        label, action, arg = parts[:3]
        if action not in ACTIONS:
            raise ParseError(f"line {number}: unknown action '{action}' (use {', '.join(ACTIONS)})")
        if action == "timer" and not (arg.isdigit() and 1 <= int(arg) <= 180):
            raise ParseError(f"line {number}: timer needs minutes 1-180")
        if action == "habit" and arg not in habits:
            raise ParseError(f"line {number}: habit '{arg}' is not in ## Habits")
        if action == "light" and arg not in [p["name"] for p in presets]:
            raise ParseError(f"line {number}: preset '{arg}' is not in ## Light presets")
        buttons.append({"label": label, "action": action, "arg": arg})
    if len(buttons) > 4:
        raise ParseError("max 4 buttons")

    night = None
    line = first_line(s.get("night", []))
    if line:
        start, _, end = (part.strip() for part in line.partition("-"))
        if not (HHMM.match(start) and HHMM.match(end)):
            raise ParseError("night needs HH:MM-HH:MM")
        night = {"start": start, "end": end}

    return {"pages": pages, "buttons": buttons, "habits": habits, "presets": presets, "night": night}


def parse_today(text):
    match = re.search(r"^updated:\s*(\d{4}-\d{2}-\d{2})\s*$", text, re.M)
    if not match:
        raise ParseError("missing 'updated: YYYY-MM-DD' line")
    s = split_sections(text)
    events = []
    for number, parts in items(s.get("events", []), 5):
        time_, anim, colour, secs, message = parts[:5]
        if not (HHMM.match(time_) and anim in ANIMS and COLOUR.match(colour) and secs.isdigit()):
            raise ParseError(f"line {number}: event needs 'HH:MM | anim | #rrggbb | seconds | text'")
        events.append({"time": time_, "anim": anim, "color": colour, "secs": int(secs), "text": message})
    return {
        "updated": match.group(1),
        "message": first_line(s.get("message", [])),
        "focus": first_line(s.get("focus", [])),
        "todos": [parts[0] for _, parts in items(s.get("todos", []))],
        "events": events,
    }


def parse_dates(text):
    s = split_sections(text)
    return {"deadlines": dated(s.get("deadlines", [])), "countdowns": dated(s.get("countdowns", [])),
            "space_events": dated(s.get("space events", []))}


def parse_quotes(text):
    return [{"text": p[0], "author": p[1] if len(p) > 1 else ""} for _, p in items(numbered(text))]


def parse_list(text):
    # facts and fortunes: the whole line, a "|" inside is kept
    return [line[2:].strip() for _, line in numbered(text) if line.startswith("- ")]


def parse_links(text):
    out = []
    for number, parts in items(numbered(text)):
        if not parts[0].startswith("https://"):
            raise ParseError(f"line {number}: link must start with https://")
        out.append({"url": parts[0], "title": parts[1] if len(parts) > 1 else ""})
    return out


def parse_pixelart(text):
    title, palette, rows, in_block = "", {}, [], False
    for line in text.splitlines():
        if line.startswith("```"):
            in_block = not in_block
        elif in_block:
            if line.strip():
                rows.append(line.rstrip())
        elif line.startswith("title:"):
            title = line[6:].strip()
        elif line.startswith("palette:"):
            for pair in line[8:].split():
                key, _, colour = pair.partition("=")
                if len(key) == 1 and COLOUR.match(colour):
                    palette[key] = colour
    if not rows:
        raise ParseError("no rows in a ``` code block")
    if len({len(r) for r in rows}) > 1:
        raise ParseError("rows have different lengths")
    if len(rows) > 48 or len(rows[0]) > 64:
        raise ParseError("max 64x48 pixels")
    return {"title": title, "w": len(rows[0]), "h": len(rows), "palette": palette, "rows": rows}


PARSERS = {
    "config.md": parse_config,
    "today.md": parse_today,
    "dates.md": parse_dates,
    "quotes.md": parse_quotes,
    "facts.md": parse_list,
    "fortunes.md": parse_list,
    "images/links.md": parse_links,
}


# ---------- loading and building ----------

def read(path):
    # utf-8-sig drops the byte order mark windows editors sometimes add
    with open(path, encoding="utf-8-sig") as f:
        return f.read()


def load_file(path, key, parser, last_good, errors):
    try:
        last_good[key] = parser(read(path))
    except (ParseError, UnicodeDecodeError) as e:
        errors.append(f"{key}: {e}")
    return last_good.get(key)


def load_folder(folder, last_good):
    # parses every file; a missing file is empty, a broken one falls back to its last good version
    data, errors = {}, []
    for key, parser in PARSERS.items():
        path = os.path.join(folder, key)
        data[key] = load_file(path, key, parser, last_good, errors) if os.path.exists(path) else None

    data["images"] = {}
    image_dir = os.path.join(folder, "images")
    names = sorted(os.listdir(image_dir)) if os.path.isdir(image_dir) else []
    for name in names:
        if name.endswith(".txt"):
            art = load_file(os.path.join(image_dir, name), "images/" + name, parse_pixelart, last_good, errors)
            if art:
                data["images"][name] = art
    return data, errors


def pick_pixelart(images, today):
    # newest picture dated today or earlier ("YYYY-MM-DD-name.txt"); undated files count as the oldest
    dated_names = [n for n in images if DATE.match(n[:10]) and n[:10] <= today]
    undated = [n for n in images if not DATE.match(n[:10])]
    choice = max(dated_names) if dated_names else (max(undated) if undated else None)
    return images[choice] if choice else None


def build(data, today):
    config = data.get("config.md") or {}
    day = data.get("today.md") or {}
    dates = data.get("dates.md") or {}
    quotes = data.get("quotes.md") or []
    facts = data.get("facts.md") or []
    fortunes = data.get("fortunes.md") or []

    rng = random.Random(today)  # the same picks all day
    feed = {
        "deadlines": dates.get("deadlines", []),
        "countdowns": dates.get("countdowns", []),
        "todos": day.get("todos", []),
        "message": day.get("message", ""),
        "focus": day.get("focus", ""),
        "habits": config.get("habits", []),
        "pages": config.get("pages", []),
        "buttons": config.get("buttons", []),
        "presets": config.get("presets", []),
        "quote": quotes[date.fromisoformat(today).toordinal() % len(quotes)] if quotes else {"text": "", "author": ""},
        "facts": rng.sample(facts, min(MAX_PICKS, len(facts))),
        "fortunes": rng.sample(fortunes, min(MAX_PICKS, len(fortunes))),
    }
    if config.get("night"):
        feed["night"] = config["night"]

    return {
        "feed": feed,
        # atlas plans events for one day only
        "events": day.get("events", []) if day.get("updated") == today else [],
        "space_events": dates.get("space_events", []),
        "pixelart": pick_pixelart(data.get("images") or {}, today),
        "links": data.get("images/links.md") or [],
    }


if __name__ == "__main__":
    import json
    import sys
    folder_data, folder_errors = load_folder(sys.argv[1], {})
    print(json.dumps({"errors": folder_errors, "build": build(folder_data, date.today().isoformat())}, indent=1))
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cd pi && python -m unittest discover -s tests -v`
Expected: all tests in `test_vault_feed.py` PASS (`OK`).

- [ ] **Step 5: Checkpoint**: tests green before moving on.

---

### Task 2: Status report (`pi/status_report.py`)

**Files:**
- Create: `pi/status_report.py`
- Create: `pi/tests/test_status_report.py`

**Interfaces:**
- Produces: `render(info: dict) -> str`. `info` keys:
  - `generated`: `"YYYY-MM-DD HH:MM"`
  - `dashboard`: `{online: bool, ver: str|None, rssi: int|None, last_seen: str|None}`
  - `pi`: `{temp: float, disk_free_gb: float, pihole: str, services: {name: bool}}`
  - `sync`: `{last_read: str|None, errors: [str]}`
  - `today`: `{focus_min: int, habits: [str]}`

- [ ] **Step 1: Write the failing test**

`pi/tests/test_status_report.py`:

```python
import unittest

import status_report

INFO = {
    "generated": "2026-09-27 21:10",
    "dashboard": {"online": True, "ver": "1.1.0", "rssi": -84, "last_seen": "2026-09-27 21:09"},
    "pi": {"temp": 45.2, "disk_free_gb": 9.8, "pihole": "Blocking enabled",
           "services": {"mosquitto": True, "bench-hub": True, "funnel": False}},
    "sync": {"last_read": "2026-09-27 21:05", "errors": []},
    "today": {"focus_min": 75, "habits": ["Typing", "Read"]},
}


class Render(unittest.TestCase):
    def test_healthy(self):
        text = status_report.render(INFO)
        self.assertIn("generated: 2026-09-27 21:10", text)
        self.assertIn("online, firmware 1.1.0, wifi -84 dBm", text)
        self.assertIn("mosquitto ok, bench-hub ok, funnel DOWN", text)
        self.assertIn("no file errors", text)
        self.assertIn("focus 75 min, habits: Typing, Read", text)

    def test_offline_with_errors(self):
        info = dict(INFO, dashboard={"online": False, "ver": None, "rssi": None, "last_seen": None},
                    sync={"last_read": None, "errors": ["config.md: line 2: unknown page 'Weather'"]},
                    today={"focus_min": 0, "habits": []})
        text = status_report.render(info)
        self.assertIn("OFFLINE, firmware ?, wifi ? dBm, last seen never", text)
        self.assertIn("  - config.md: line 2: unknown page 'Weather'", text)
        self.assertIn("last read never", text)
        self.assertIn("habits: none yet", text)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd pi && python -m unittest tests.test_status_report -v`
Expected: ERROR `No module named 'status_report'`

- [ ] **Step 3: Write the implementation**

`pi/status_report.py`:

```python
# renders the dashboard/pi status as markdown; end-day / atlas-sync copy it into the vault


def value(v):
    return "?" if v is None else v


def render(info):
    d, p, s, t = info["dashboard"], info["pi"], info["sync"], info["today"]
    lines = [
        "# Dashboard status",
        f"generated: {info['generated']}",
        "",
        "Written by BenchPi, copied here by end-day / atlas-sync. Don't edit it, it gets overwritten.",
        "",
        "## Dashboard",
        f"- {'online' if d['online'] else 'OFFLINE'}, firmware {value(d['ver'])}, wifi {value(d['rssi'])} dBm, "
        f"last seen {d['last_seen'] or 'never'}",
        "",
        "## Pi",
        f"- temp {p['temp']} C, disk free {p['disk_free_gb']} GB, Pi-hole: {p['pihole']}",
        "- services: " + ", ".join(f"{name} {'ok' if ok else 'DOWN'}" for name, ok in p["services"].items()),
        "",
        "## Sync",
        f"- Dashboard folder last read {s['last_read'] or 'never'}",
    ]
    if s["errors"]:
        lines.append("- **File errors** (the last good version is used until they're fixed):")
        lines += [f"  - {e}" for e in s["errors"]]
    else:
        lines.append("- no file errors")
    lines += ["", "## Today", f"- focus {t['focus_min']} min, habits: {', '.join(t['habits']) or 'none yet'}", ""]
    return "\n".join(lines)
```

- [ ] **Step 4: Run all Pi tests**

Run: `cd pi && python -m unittest discover -s tests -v`
Expected: all PASS.

- [ ] **Step 5: Checkpoint**

---

### Task 3: Hub integration (`pi/bench_hub.py`)

**Files:**
- Modify: `pi/bench_hub.py` (settings block, `nasa_photo`, `today_summary`, `run_shortcut`, `force_refresh`, `on_message`, `on_connect`, `state`, `main`)

**Interfaces:**
- Consumes: `vault_feed.load_folder`, `vault_feed.build` (Task 1), `status_report.render` (Task 2).
- Produces (on the Pi): `~/bench/vault/` (synced folder), `~/bench/outbox/status.md`; MQTT `bench/feed` gains `pages`, `buttons`, `presets` (no more `shortcuts`).

- [ ] **Step 1: Settings and imports.** Replace the `import dashboard_feed` line with `import status_report` and `import vault_feed`, add `import hashlib` and `import shutil` to the stdlib imports, and replace the `DASHBOARD_FILE` / `DRIVE_FILE` lines and `PHOTO_EVERY` with:

```python
VAULT_DIR = os.path.join(HOME, "vault")      # read-only copy of the vault's Dashboard/ folder
DRIVE_DIR = "gdrive:"                        # rclone remote, rooted at the Dashboard folder
OUTBOX_STATUS = os.path.join(HOME, "outbox", "status.md")
```

```python
PHOTO_EVERY = 10 * 60       # the photo slot changes every 3 hours, checked every 10 min
STATUS_EVERY = 10 * 60
```

- [ ] **Step 2: Shared photo pipeline.** Replace `nasa_photo()` with these three functions:

```python
def to_screen_jpeg(data):
    # crops any picture to the screen's shape (cover), scales it and keeps it under the size limit
    img = Image.open(io.BytesIO(data))
    img.draft("RGB", (PHOTO_SIZE[0] * 2, PHOTO_SIZE[1] * 2))  # decode big jpegs at reduced size
    img = img.convert("RGB")
    target = PHOTO_SIZE[0] / PHOTO_SIZE[1]
    w, h = img.size
    if w / h > target:
        crop = int(h * target)
        img = img.crop(((w - crop) // 2, 0, (w + crop) // 2, h))
    else:
        crop = int(w / target)
        img = img.crop((0, (h - crop) // 2, w, (h + crop) // 2))
    img = img.resize(PHOTO_SIZE, Image.LANCZOS)
    for quality in (85, 75, 65, 55, 45):
        out = io.BytesIO()
        img.save(out, "JPEG", quality=quality)  # baseline jpeg, what the dashboard decoder supports
        if out.tell() <= PHOTO_MAX_BYTES:
            break
    return out.getvalue()


def nasa_photo():
    # nasa's astronomy picture of the day: (title, jpeg) or None
    d = requests.get("https://api.nasa.gov/planetary/apod", timeout=30,
                     params={"api_key": ENV.get("NASA_KEY", "DEMO_KEY"), "thumbs": "true"}).json()
    url = d.get("url") if d.get("media_type") == "image" else d.get("thumbnail_url")
    if not url:
        return None
    return d.get("title", ""), to_screen_jpeg(requests.get(url, timeout=60).content)


def link_photo(link):
    return link["title"], to_screen_jpeg(requests.get(link["url"], timeout=60).content)
```

- [ ] **Step 3: Today's totals.** Replace `today_summary()` with:

```python
def today_totals():
    # today's focus minutes and habits from the logs that haven't been pulled into the vault yet
    focus, habits = 0, []
    for path in (PULLED_LOG_FILE, LOG_FILE):
        if not os.path.exists(path):
            continue
        with open(path) as f:
            for line in f:
                try:
                    entry = json.loads(line)
                except ValueError:
                    continue
                if entry.get("date") != today_str():
                    continue
                if entry.get("type") == "focus":
                    focus += entry.get("minutes", 0)
                elif entry.get("type") == "habit":
                    habits.append(entry.get("name", "?"))
    return focus, habits


def today_summary():
    focus, habits = today_totals()
    return f"Focus {focus} min. Habits: {', '.join(habits) if habits else 'none yet'}"
```

- [ ] **Step 4: Device tracking and refresh.** Replace `force_refresh`, `on_message` and `on_connect`, and the `state = {...}` line, with:

```python
def force_refresh():
    for name in ("dashboard", "calendar", "weather", "stats"):
        last[name] = 0
    state["vault_sig"] = ""  # rebuild from the folder even if nothing changed


def on_message(client, userdata, msg):
    topic = msg.topic.split("/", 1)[1]
    text = msg.payload.decode(errors="replace")
    if topic == "log":
        with open(LOG_FILE, "a") as f:
            f.write(text.strip() + "\n")
        log(f"logged {text}")
    elif topic == "shortcut":
        name = json.loads(text).get("name", "")
        threading.Thread(target=run_shortcut, args=(client, name), daemon=True).start()
    elif topic == "status":
        device["online"] = text == "online"
    elif topic == "state":
        try:
            info = json.loads(text)
        except ValueError:
            return
        device.update(ver=info.get("ver"), rssi=info.get("rssi"), last_seen=f"{datetime.now(TZ):%Y-%m-%d %H:%M}")


def on_connect(client, userdata, flags, reason, properties):
    log(f"mqtt connected ({reason})")
    client.subscribe([("bench/log", 1), ("bench/shortcut", 1), ("bench/status", 1), ("bench/state", 1)])


# latest data shared by the jobs
state = {"passes": [], "calendar": [], "vault": None, "vault_sig": "", "day": "", "last_read": None,
         "errors": [], "photo_key": ""}
device = {"online": False, "ver": None, "rssi": None, "last_seen": None}
last_good = {}  # last good version of every vault file
```

- [ ] **Step 5: Folder signature and status helpers.** Add above `run_every`:

```python
def folder_signature(folder):
    # changes whenever any file in the folder changes
    digest = hashlib.sha256()
    for root, _, files in sorted(os.walk(folder)):
        for name in sorted(files):
            path = os.path.join(root, name)
            digest.update(os.path.relpath(path, folder).encode())
            with open(path, "rb") as f:
                digest.update(f.read())
    return digest.hexdigest()


def service_status():
    active = subprocess.run(["systemctl", "is-active", "mosquitto", "bench-hub"],
                            capture_output=True, text=True).stdout.split()
    funnel = subprocess.run(["tailscale", "funnel", "status"], capture_output=True, text=True).stdout
    return {"mosquitto": active[:1] == ["active"], "bench-hub": active[1:2] == ["active"],
            "funnel": ":8443" in funnel}
```

- [ ] **Step 6: Jobs in `main()`.** Inside `main()`, replace `publish_space_and_events`, `photo_job` and `dashboard_job` with the versions below, and add `status_job`:

```python
    def publish_space_and_events():
        today = today_str()
        vault = state["vault"] or {}
        publish(client, "space", {"iss": state["passes"][:4], "events": vault.get("space_events", [])})
        items = vault.get("events", []) + iss_events(state["passes"], today) + calendar_events(state["calendar"])
        publish(client, "events", {"date": today, "items": sorted(items, key=lambda e: e["time"])[:16]})
        state["day"] = today

    def photo_job():
        # the photo slot takes turns every 3 hours: nasa, then each picture in images/links.md
        links = (state["vault"] or {}).get("links", [])
        choices = [None] + links
        pick = choices[datetime.now(TZ).hour // 3 % len(choices)]
        key = f"{today_str()}:{pick['url'] if pick else 'nasa'}"
        if key == state["photo_key"]:
            return
        result = link_photo(pick) if pick else nasa_photo()
        state["photo_key"] = key
        if not result:
            return
        title, jpeg = result
        publish(client, "photo", jpeg)
        publish(client, "photo/title", title)
        log(f"photo: {title} ({len(jpeg) // 1024} KB)")

    def dashboard_job():
        subprocess.run(["rclone", "sync", DRIVE_DIR, VAULT_DIR, "--exclude", "status.md"],
                       check=True, timeout=180, capture_output=True)
        signature, today = folder_signature(VAULT_DIR), today_str()
        state["last_read"] = f"{datetime.now(TZ):%Y-%m-%d %H:%M}"
        if signature == state["vault_sig"] and state["vault"] and state["day"] == today:
            return
        data, errors = vault_feed.load_folder(VAULT_DIR, last_good)
        state["vault"] = built = vault_feed.build(data, today)
        state["vault_sig"] = signature
        publish(client, "feed", built["feed"])
        if built["pixelart"]:
            publish(client, "pixelart", built["pixelart"])
        publish_space_and_events()
        if errors and errors != state["errors"]:
            notify(client, "Dashboard files", f"{len(errors)} error(s), see status.md", "#ff8c00", secs=10)
        state["errors"] = errors
        log(f"Dashboard folder published ({len(errors)} errors)")

    def status_job():
        focus, habits = today_totals()
        stats = pi_stats()
        try:
            pihole = pihole_stats()["status"]
        except Exception:
            pihole = "unreachable"
        report = status_report.render({
            "generated": f"{datetime.now(TZ):%Y-%m-%d %H:%M}",
            "dashboard": dict(device),
            "pi": {"temp": stats["temp"], "disk_free_gb": round(shutil.disk_usage("/").free / 1e9, 1),
                   "pihole": pihole, "services": service_status()},
            "sync": {"last_read": state["last_read"], "errors": state["errors"]},
            "today": {"focus_min": focus, "habits": habits},
        })
        os.makedirs(os.path.dirname(OUTBOX_STATUS), exist_ok=True)
        with open(OUTBOX_STATUS, "w") as f:
            f.write(report)
```

and replace the `while True:` loop body with:

```python
    while True:
        run_every(STATS_EVERY, "stats", stats_job)
        run_every(WEATHER_EVERY, "weather", weather_job)
        run_every(DASHBOARD_EVERY, "dashboard", dashboard_job)
        run_every(CALENDAR_EVERY, "calendar", calendar_job)
        run_every(PHOTO_EVERY, "photo", photo_job)
        run_every(ISS_EVERY, "iss", iss_job)
        run_every(STATUS_EVERY, "status", status_job)
        # new day: quote, facts, pixel art and events all change
        if state["day"] and state["day"] != today_str():
            last["dashboard"] = last["calendar"] = 0
            state["vault_sig"] = ""
        time.sleep(5)
```

- [ ] **Step 7: Check locally**

Run: `cd pi && python -m py_compile bench_hub.py && python -m unittest discover -s tests`
Expected: no output from `py_compile`, tests `OK`.

- [ ] **Step 8: Checkpoint** (deployed in Task 7).

---

### Task 4: Firmware: pages, Home buttons, light presets

**Files:**
- Modify: `src/state.h` (Feed struct), `src/net.cpp` (`handleFeed`), `src/leds.h` + `src/leds.cpp` (preset), `src/ui.cpp` (page order, Home buttons, Light page), `src/main.cpp` (pots clear preset), `include/config.h` (version)

**Interfaces:**
- Consumes: `bench/feed` fields `pages: [str]`, `buttons: [{label, action, arg}]`, `presets: [{name, color, brightness}]` (Task 1).
- Produces: `void ledsSetPreset(CRGB colour, uint8_t brightness)`, `void ledsClearPreset()`, `bool ledsPresetActive()`.

- [ ] **Step 1: `src/state.h`.** Above `struct Feed`, add:

```cpp
struct ButtonConfig { String label, action, arg; };
struct LightPreset { String name; CRGB colour; uint8_t brightness; };
```

In `struct Feed`, replace `String shortcuts[4];    int nShortcuts = 0;` with:

```cpp
  String pages[9];        int nPages = 0;     // empty = all pages in the built-in order
  ButtonConfig buttons[4]; int nButtons = 0;  // home buttons A-D
  LightPreset presets[6]; int nPresets = 0;
```

- [ ] **Step 2: `src/net.cpp`.** In `handleFeed`, replace `f.nShortcuts = readStrings(doc["shortcuts"], f.shortcuts, 4);` with:

```cpp
  f.nPages = readStrings(doc["pages"], f.pages, 9);
  f.nButtons = 0;
  for (JsonObjectConst b : doc["buttons"].as<JsonArrayConst>()) {
    if (f.nButtons >= 4) break;
    f.buttons[f.nButtons++] = {b["label"] | "", b["action"] | "", b["arg"] | ""};
  }
  f.nPresets = 0;
  for (JsonObjectConst p : doc["presets"].as<JsonArrayConst>()) {
    if (f.nPresets >= 6) break;
    f.presets[f.nPresets++] = {p["name"] | "", parseColour(p["color"] | "", CRGB::White), (uint8_t)(p["brightness"] | 200)};
  }
```

- [ ] **Step 3: `src/leds.h`.** Add after `bool ledsOverrideActive();`:

```cpp
// light preset from config.md, cleared as soon as a pot moves
void ledsSetPreset(CRGB colour, uint8_t brightness);
void ledsClearPreset();
bool ledsPresetActive();
```

- [ ] **Step 4: `src/leds.cpp`.** Add next to the override variables:

```cpp
static bool presetActive = false;
static CRGB presetColour;
static uint8_t presetBrightness = 255;
```

In `drawLight`, directly after the `if (overrideActive) { ... return; }` block, add:

```cpp
  if (presetActive) {
    fill_solid(leds, NUM_LEDS, presetColour);
    nscale8_video(leds, NUM_LEDS, presetBrightness);
    return;
  }
```

At the end of the file add:

```cpp
void ledsSetPreset(CRGB colour, uint8_t brightness) {
  presetActive = true;
  presetColour = colour;
  presetBrightness = brightness;
}

void ledsClearPreset() { presetActive = false; }
bool ledsPresetActive() { return presetActive; }
```

- [ ] **Step 5: `src/main.cpp`.** Replace `if (potsMoved()) uiWake();` with:

```cpp
  if (potsMoved()) {
    uiWake();
    ledsClearPreset();  // turning a pot takes the light back from a preset
  }
```

- [ ] **Step 6: `src/ui.cpp`: page order.** After `static bool galleryPhoto = false;` add:

```cpp
// page order from config.md, or the built-in order when it lists none; home is always first
static int pageOrder(Page *out) {
  int n = 0;
  out[n++] = P_HOME;
  if (app.feed.nPages == 0) {
    for (int p = 1; p < NUM_PAGES; p++) out[n++] = (Page)p;
    return n;
  }
  // the pi removes duplicates and puts home first, so home is skipped here
  for (int i = 0; i < app.feed.nPages && n < NUM_PAGES; i++)
    for (int p = 1; p < NUM_PAGES; p++)
      if (app.feed.pages[i] == PAGE_NAMES[p]) out[n++] = (Page)p;
  return n;
}

static bool pageShown(Page p) {
  Page order[NUM_PAGES];
  int n = pageOrder(order);
  for (int i = 0; i < n; i++) if (order[i] == p) return true;
  return false;
}

static Page stepPage(int direction) {
  Page order[NUM_PAGES];
  int n = pageOrder(order), at = 0;
  for (int i = 0; i < n; i++) if (order[i] == page) at = i;
  return order[(at + direction + n) % n];
}
```

In `uiRender()`, right after `spr.fillSprite(C_BG);` add `if (!pageShown(page)) page = P_HOME;  // config.md hid the current page`.

In `uiHandleInput`, replace the page stepping so next/previous use the configured order:

```cpp
  if (ev.button == B_NEXT) {
    page = ev.type == PRESS_LONG ? P_HOME : stepPage(1);
    scroll = 0;
  } else if (ev.button == B_PREV) {
    if (ev.type == PRESS_LONG) ledsToggleLight();   // quick light switch from any page
    else { page = stepPage(-1); scroll = 0; }
  } else if (ev.type == PRESS_SHORT) {
```

- [ ] **Step 7: `src/ui.cpp`: Home buttons.** In `pageHome()`, replace the shortcut soft-key lines with:

```cpp
  const char *keys[4] = {"-", "-", "-", "-"};
  for (int i = 0; i < app.feed.nButtons; i++) keys[i] = app.feed.buttons[i].label.c_str();
  drawSoftKeys(keys[0], keys[1], keys[2], keys[3]);
```

Above `static void pageAction(...)` add:

```cpp
// light presets from config.md, button c on the light page cycles through them
static int presetIndex = -1;

static bool applyPreset(const String &name) {
  for (int i = 0; i < app.feed.nPresets; i++) {
    LightPreset &p = app.feed.presets[i];
    if (p.name != name) continue;
    presetIndex = i;
    ledsSetPreset(p.colour, p.brightness);
    uiBanner("Light: " + p.name, "Turn a pot to take over again", p.colour, 2, false);
    return true;
  }
  return false;
}

// home buttons from config.md: shortcut, timer, habit or light
static void runButton(const ButtonConfig &b, int index) {
  if (b.action == "shortcut") {
    JsonDocument doc;
    doc["id"] = index;
    doc["name"] = b.arg;
    String out;
    serializeJson(doc, out);
    netPublish("shortcut", out);
    uiBanner("Sent", b.label, CRGB(80, 160, 255), 2, false);
  } else if (b.action == "timer") {
    focusMinutes = constrain((int)b.arg.toInt(), 1, 180);
    focusRemaining = focusMinutes * 60000L;
    focusEndsAt = millis() + focusRemaining;
    focusRunning = true;
    uiBanner("Focus started", String(focusMinutes) + " min", CRGB(0, 200, 80), 2, false);
  } else if (b.action == "habit") {
    for (int h = 0; h < habitCount(); h++)
      if (habitName(h) == b.arg) { habitLog(h); return; }
    uiBanner(b.label, "Unknown habit " + b.arg, CRGB::Orange, 3, false);
  } else if (b.action == "light") {
    if (!applyPreset(b.arg)) uiBanner(b.label, "Unknown preset " + b.arg, CRGB::Orange, 3, false);
  }
}
```

In `pageAction`, replace the whole `case P_HOME:` block with:

```cpp
    case P_HOME:
      if (i < app.feed.nButtons) runButton(app.feed.buttons[i], i);
      break;
```

- [ ] **Step 8: `src/ui.cpp`: Light page presets.** In `pageAction`'s `case P_LIGHT:`, replace `if (b == B_C) ledsPlay("sunrise", CRGB::White, 30);` with:

```cpp
      if (b == B_C) {
        if (app.feed.nPresets) applyPreset(app.feed.presets[(presetIndex + 1) % app.feed.nPresets].name);
        else ledsPlay("sunrise", CRGB::White, 30);
      }
```

In `pageLight()`, replace the override notice and soft keys at the end with:

```cpp
  if (ledsOverrideActive()) {
    spr.setTextColor(C_AMBER);
    spr.drawString("Atlas is controlling the light", 10, TOP + 116, 2);
  } else if (ledsPresetActive() && presetIndex >= 0) {
    spr.setTextColor(C_AMBER);
    spr.drawString("Preset: " + app.feed.presets[presetIndex].name + " (turn a pot to exit)", 10, TOP + 116, 2);
  }
  drawSoftKeys("On/Off", "Mode", app.feed.nPresets ? "Preset" : "Sunrise", ledsOverrideActive() ? "Auto" : "");
```

- [ ] **Step 9: Version.** In `include/config.h` set `#define FW_VERSION       "1.1.0"`.

- [ ] **Step 10: Build**

Run: `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run 2>&1 | Select-String "^src/.*(error|warning)|SUCCESS|FAILED"`
Expected: `SUCCESS`, no lines from `src/`.

- [ ] **Step 11: Checkpoint** (flashed in Task 7).

---

### Task 5: The vault folder

**Files:**
- Create: `C:\AI\Atlas\Dashboard\README.md`, `config.md`, `today.md`, `dates.md`, `quotes.md`, `facts.md`, `fortunes.md`, `images\2026-09-26-launch-night.txt`, `images\links.md`
- Delete (in Task 7, after the Pi reads the folder): `C:\AI\Atlas\meta\dashboard.md`

**Interfaces:**
- Consumes: the formats from Task 1. Content is migrated from `meta/dashboard.md` (read it first; keep its deadlines, todos, message, quote, focus, events, facts, fortunes, space events, countdown and pixel art, with `updated:` set to the day this runs).

- [ ] **Step 1: `README.md`.** This is the format reference Atlas reads before editing:

````markdown
# Dashboard folder

Feeds the workbench dashboard (see [[Projects/ESP32-S3 Dashboard]]). BenchPi reads this folder read-only from the Drive backup every 10 minutes. After editing from the PC, press Refresh: `python C:\Users\evenm\.bench\bench_pub.py shortcut '{"name":"Refresh"}'`.

Format everywhere: `##` headings, `- ` list items, fields split by ` | `. A file with a mistake is skipped (its last good version stays on the dashboard) and the error shows up in `status.md`.

| File | What | Format |
|---|---|---|
| `config.md` | pages, Home buttons, habits, light presets, night hours | see below |
| `today.md` | `updated: YYYY-MM-DD`, `## Message`, `## Focus`, `## Todos`, `## Events` | events: `- HH:MM \| anim \| #rrggbb \| seconds \| text`, only fire on the `updated:` date. Animations: pulse, flash, rainbow, confetti, chase, sunrise |
| `dates.md` | `## Deadlines`, `## Countdowns`, `## Space events` | `- YYYY-MM-DD \| title` |
| `quotes.md` | real quotes with the correct author | `- text \| author`. One per day, picked by date |
| `facts.md`, `fortunes.md` | true facts / made-up fortunes | `- text`. 12 random ones per day |
| `images/YYYY-MM-DD-name.txt` | pixel art | `title:`, `palette: a=#rrggbb b=…`, rows in a code block, `.` = transparent, same length, max 64×48. The newest dated today or earlier is shown |
| `images/links.md` | online pictures | `- https://… \| title`. Takes turns with the NASA photo every 3 hours |
| `status.md` | health report | written by the PC during end-day / atlas-sync. Don't edit |

## config.md

- `## Pages`: `- Name` in the order to show. Names: Home, Bench light, Focus, Tasks, Habits, Today, Space, Stats, Gallery. Leave one out to hide it. Home is always shown.
- `## Buttons`: the four Home buttons A-D, `- Label | action | argument`:
  - `shortcut <name>`: Pi shortcut. Built-in: Refresh, No ads 5m, Ads on, Today; others are shell commands in `~/bench/shortcuts.json` on the Pi
  - `timer <minutes>`: start the focus timer (1-180)
  - `habit <name>`: log a habit from `## Habits`
  - `light <preset>`: a preset from `## Light presets`
- `## Habits`: max 4.
- `## Light presets`: `- Name | #rrggbb | brightness 0-255`. Button C on the Bench light page cycles them.
- `## Night`: `HH:MM-HH:MM`, screen off and dimmer alerts.
````

- [ ] **Step 2: `config.md`:**

```markdown
# Dashboard config

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
- No ads 5m | shortcut | No ads 5m
- Today | shortcut | Today

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

- [ ] **Step 3: `today.md`, `dates.md`, `quotes.md`, `facts.md`, `fortunes.md`.** Move the matching sections from `meta/dashboard.md`:
  - **`today.md`:** `# Today`, `updated: <today>`, then `## Message`, `## Focus`, `## Todos` and `## Events`.
  - **`dates.md`:** `## Deadlines`, `## Countdowns`, `## Space events`.
  - **`quotes.md`:** `# Quotes`, then `- Well done is better than well said. | Benjamin Franklin` plus 4 more real quotes with correct authors (e.g. Dijkstra "Simplicity is prerequisite for reliability.", Alan Kay "The best way to predict the future is to invent it.", Kent Beck "Make it work, make it right, make it fast.", Knuth "Premature optimization is the root of all evil.").
  - **`facts.md`, `fortunes.md`:** the existing items plus the built-in firmware ones from `src/content.h`, so the pool is larger than 12.

- [ ] **Step 4: `images/2026-09-26-launch-night.txt`.** Move the `## Pixel art` block from `meta/dashboard.md` (its `title:`, `palette:` and code block) unchanged. **`images/links.md`:** `# Picture links` followed by an empty list (no `- ` lines yet).

- [ ] **Step 5: Validate against the parser**

Run: `cd pi && python vault_feed.py "C:\AI\Atlas\Dashboard"`
Expected: `"errors": []`, `feed.pages` has 9 names, `feed.buttons` 4 items, `pixelart.title` is `"Launch night"`, `events` has today's events.

- [ ] **Step 6: Checkpoint**

---

### Task 6: PC side and skills

**Files:**
- Create: `C:\Users\evenm\.bench\pull_bench.py`
- Delete: `C:\Users\evenm\.bench\pull_logs.py` (after the new one is tested)
- Modify: `skill-updates/end-day/SKILL.md`, `skill-updates/atlas-sync/SKILL.md`, `skill-updates/atlas/SKILL.md`, then re-zip

**Interfaces:**
- Consumes: `~/bench/outbox/status.md` on the Pi (Task 3), `~/bench/log.jsonl` / `log.pulled.jsonl` (existing).
- Produces: `python C:\Users\evenm\.bench\pull_bench.py` → copies status to `C:\AI\Atlas\Dashboard\status.md`, prints pending log entries; `--done` clears them.

- [ ] **Step 1: `pull_bench.py`:**

```python
# fetches the workbench dashboard's status and logs from benchpi for end-day / atlas-sync
# usage: python pull_bench.py          -> copies status.md into the vault, prints the pending log entries
#        python pull_bench.py --done   -> clears the log entries once they're written to the vault
import subprocess
import sys

HOST = "pi@benchpi"
VAULT_STATUS = r"C:\AI\Atlas\Dashboard\status.md"

# move new entries into a "pulled" file first, so nothing logged meanwhile gets lost
PULL = ("cd ~/bench && if [ -s log.jsonl ]; then cat log.jsonl >> log.pulled.jsonl && rm log.jsonl; fi; "
        "cat log.pulled.jsonl 2>/dev/null; true")
DONE = "rm -f ~/bench/log.pulled.jsonl"
SSH = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=15", HOST]


def run(command):
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    if result.returncode != 0:
        sys.exit(f"could not reach {HOST}: {result.stderr.strip()}")
    return result.stdout.strip()


def main():
    if "--done" in sys.argv:
        run(SSH + [DONE])
        print("cleared")
        return
    run(["scp", "-q", "-o", "BatchMode=yes", f"{HOST}:bench/outbox/status.md", VAULT_STATUS])
    print(f"status copied to {VAULT_STATUS}")
    print(run(SSH + [PULL]) or "no new entries")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Test it** (after Task 7 has deployed the hub, since it needs the outbox file):

Run: `python ~/.bench/pull_bench.py`
Expected: `status copied to C:\AI\Atlas\Dashboard\status.md`, then log entries or `no new entries`. Open the vault file and check it matches the Pi's report. Then delete `pull_logs.py`.

- [ ] **Step 3: Skills.** First copy the live versions again (Even uploaded the last ones): `cp ~/.claude/skills/synced/*/{end-day,atlas-sync,atlas}/SKILL.md` into `skill-updates/<name>/`. Then:

**end-day:**
- In step 2, replace `python C:\Users\evenm\.bench\pull_logs.py` with `python C:\Users\evenm\.bench\pull_bench.py` (it also copies `Dashboard/status.md` into the vault).
- Replace the whole `## 4. Workbench dashboard` section with:

```markdown
## 4. Workbench dashboard

The dashboard above the bench is fed from the vault's `Dashboard/` folder. **Read `Dashboard/README.md` first** for the file formats.

- **Status:** read `Dashboard/status.md` (just copied by `pull_bench.py`). If it lists file errors, fix those files now. Mention in the sign-off if the dashboard is offline or a service is down.
- **Logs:** add the pulled entries to today's diary entry as one or two lines (e.g. `habits: Exercise ✓, Read ✓` and `focus: 50 min ESP32-S3 Dashboard`). Once the diary write is confirmed, run `python C:\Users\evenm\.bench\pull_bench.py --done`. If the write failed, don't clear.
- **`today.md` for tomorrow.** Set `updated:` to **tomorrow's** date.
  - **Message:** the most useful nudge.
  - **Focus:** the main project.
  - **Todos:** 3-5 concrete ones.
  - **Events:** 2-4 timed nudges, e.g. before a class, the typing reminder, and the 22:45 wind-down. Calendar events already get an automatic heads-up, so don't duplicate them.
- **`dates.md`:** deadlines in the next ~30 days (max 8) from `meta/tasks.md` and project notes; countdowns and space events worth tracking.
- **A new pixel-art picture:** `images/<tomorrow>-<name>.txt`, 32×16, themed on tomorrow. The format is in README.
- **Now and then:** add a quote (real, correct author), facts (true) and fortunes to their files. Optionally add an online picture to `images/links.md`.
- **`config.md`:** only change it when Even asks.
```

- In `## 6. Sign-off`, replace `(diary, dashboard logs, \`dashboard.md\` for tomorrow, any vault changes)` with `(diary, dashboard logs, \`Dashboard/\` for tomorrow, any vault changes)`.

**atlas-sync:**
- Replace item 7 of Gather with:

```markdown
7. **Workbench dashboard**: run `python C:\Users\evenm\.bench\pull_bench.py` via `device_bash`. It copies `Dashboard/status.md` into the vault and prints habit/focus log entries. File the entries into today's diary entry silently, then run it with `--done`. File errors listed in `status.md` get fixed silently, following `Dashboard/README.md`. A dashboard that is offline or a service that's down becomes one question.
```

- Replace the `- **Workbench dashboard:** if deadlines, todos or focus changed…` write-back line with:

```markdown
- **Workbench dashboard:** if deadlines, todos or focus changed, update `Dashboard/dates.md` / `Dashboard/today.md` in their exact format (see `Dashboard/README.md`). Leave events, pictures and `config.md` alone; `end-day` owns those.
```

**atlas:** replace step 4 with:

```markdown
4. When a deadline, a to-do for today or the current focus project changes, also update `Dashboard/dates.md` / `Dashboard/today.md` (they feed the workbench dashboard; formats in `Dashboard/README.md`). When Even asks to change the dashboard's buttons, habits, light presets, night hours or pages, edit `Dashboard/config.md`. After editing anything in `Dashboard/` from the PC, press Refresh: `python C:\Users\evenm\.bench\bench_pub.py shortcut '{"name":"Refresh"}'`. Atlas can also message the dashboard directly: `python C:\Users\evenm\.bench\bench_pub.py notify '{"title":"Atlas","text":"...","color":"#50a0ff","anim":"pulse","secs":10}'`, or take over the light with `led/set` (`{"mode":"solid"|"off"|"auto","color":"#rrggbb","brightness":0-255}`).
```

- [ ] **Step 4: Zip for upload**

Run (PowerShell): `$root = "C:\Users\evenm\Desktop\Mekke\ESP32_S3_Dashboard\skill-updates"; foreach ($s in "end-day","atlas-sync","atlas") { Compress-Archive -Path "$root\$s" -DestinationPath "$root\$s.zip" -Force }`
Expected: three zips. Check with `grep -n "dashboard.md\|pull_logs" skill-updates/*/SKILL.md`, which should print nothing.

- [ ] **Step 5: Checkpoint.** Even uploads the three zips on claude.ai.

---

### Task 7: Deploy, migrate, verify end to end

**Files:** Pi: `~/bench/*.py`, `~/.config/rclone/rclone.conf`; vault: delete `meta/dashboard.md`; vault note `Projects/ESP32-S3 Dashboard.md`.

- [ ] **Step 1: Find the Drive ID of the new `Dashboard` folder** (Drive for desktop uploads it within minutes). Use the Google Drive connector: search `title = 'config.md'` and take its `parentId`, and confirm that the parent's title is `Dashboard`.

- [ ] **Step 2: Re-root rclone and test the sync**

```bash
ssh pi@benchpi "sed -i 's/^root_folder_id = .*/root_folder_id = <DASHBOARD_FOLDER_ID>/' ~/.config/rclone/rclone.conf && rclone sync gdrive: ~/bench/vault --exclude status.md && ls -R ~/bench/vault"
```

Expected: the folder's files, including `images/`.

- [ ] **Step 3: Deploy the hub**

```bash
cd pi && scp -q bench_hub.py vault_feed.py status_report.py pi@benchpi:~/bench/ && ssh pi@benchpi "rm -f ~/bench/dashboard_feed.py ~/bench/dashboard.md && sudo systemctl restart bench-hub"
```

Wait about 1 minute, then check: `ssh pi@benchpi "journalctl -u bench-hub --since -3min -o cat --no-pager | tail; cat ~/bench/outbox/status.md"`
Expected: `Dashboard folder published (0 errors)`, and a status report with the dashboard online and no file errors. All jobs run once at start-up, with the status job last, so the outbox file exists within about a minute.

- [ ] **Step 4: Check the feed on the broker**

Run (PC):

```bash
python -c "import os,sys,json,time; sys.path.insert(0, os.path.expanduser('~/.bench')); from claude_notify import read_env; import paho.mqtt.client as m; e=read_env(); g={}; c=m.Client(); c.username_pw_set(e['MQTT_USER'], e['MQTT_PASSWORD']); c.tls_set(); c.on_message=lambda a,b,msg: g.setdefault('f', json.loads(msg.payload)); c.connect(e['MQTT_HOST'], int(e['MQTT_PORT'])); c.subscribe('bench/feed'); c.loop_start(); time.sleep(4); f=g['f']; print(f['pages'], [b['label'] for b in f['buttons']], [p['name'] for p in f['presets']], len(f['facts']))"
```

Expected: 9 pages, `['Refresh', 'Focus 50', 'No ads 5m', 'Today']`, `['Solder', 'Evening', 'Focus']`, and a number ≤ 12.

- [ ] **Step 5: Firmware over WiFi**

Run: `python tools/ota.py`
Expected: `done, the dashboard is running 1.1.0`.

- [ ] **Step 6: Error handling end to end.** Edit `C:\AI\Atlas\Dashboard\config.md`, add `- Weather` under `## Pages`, wait for the Drive upload (~1 min), press Refresh: `python ~/.bench/bench_pub.py shortcut '{"name":"Refresh"}'`.
Expected: an orange "Dashboard files: 1 error(s)" banner on the dashboard, the pages unchanged (last good version), and `ssh pi@benchpi cat ~/bench/outbox/status.md` (after the next status run) listing `config.md: line …: unknown page 'Weather'`. Remove the line again and press Refresh.

- [ ] **Step 7: Config change end to end.** In `config.md`, remove `- Stats` from `## Pages`, then refresh as above.
Expected: the Stats page no longer appears when stepping through pages. Put it back.

- [ ] **Step 8: `pull_bench.py`** (Task 6 Step 2), then delete `C:\Users\evenm\.bench\pull_logs.py`.

- [ ] **Step 9: Delete `C:\AI\Atlas\meta\dashboard.md`.** It's replaced by the folder (approved in the spec). Check nothing else in the vault links to it: `grep -rl "meta/dashboard.md" /c/AI/Atlas`, and update any hits to point at `Dashboard/README.md`.

- [ ] **Step 10: Vault note.** In `Projects/ESP32-S3 Dashboard.md`:
  - replace the `## \`meta/dashboard.md\` format` section with a short pointer to `Dashboard/README.md`
  - update the architecture bullet about `meta/dashboard.md` to `Dashboard/` (rclone remote rooted at the Dashboard folder)
  - add a dated status line: firmware 1.1.0, config-driven pages/buttons/presets, status outbox
  - add the open item "skills re-uploaded?"

- [ ] **Step 11: Hardware check by Even.** Step through the pages, press all four Home buttons, cycle the presets with button C on the Bench light page, and turn a pot to leave a preset.
