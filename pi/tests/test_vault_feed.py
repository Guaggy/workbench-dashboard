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

    def test_non_ascii_digits_are_parse_errors(self):
        # "²".isdigit() is True but int("²") raises ValueError, which used to escape load_folder
        with self.assertRaisesRegex(vf.ParseError, "preset needs"):
            vf.parse_config("## Light presets\n- Red | #ff0000 | ²\n")
        with self.assertRaisesRegex(vf.ParseError, "timer needs minutes"):
            vf.parse_config("## Buttons\n- T | timer | ²\n")
        with self.assertRaisesRegex(vf.ParseError, "event needs"):
            vf.parse_today("updated: 2026-09-27\n## Events\n- 19:00 | chase | #00c850 | ² | x\n")

    def test_too_many_presets(self):
        # the firmware holds 6, a 7th would pass here and then be "unknown" on the device
        text = "## Light presets\n" + "".join(f"- P{i} | #ffffff | 100\n" for i in range(7))
        with self.assertRaisesRegex(vf.ParseError, "max 6 light presets"):
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

    def test_unexpected_error_only_skips_that_file(self):
        # a directory where a file should be (or any other surprise) must not stop the other files
        write(self.folder, "config.md", CONFIG)
        os.makedirs(os.path.join(self.folder, "quotes.md"))
        data, errors = vf.load_folder(self.folder, {})
        self.assertEqual(data["config.md"]["pages"], ["Home", "Focus", "Tasks"])
        self.assertEqual(len(errors), 1)
        self.assertTrue(errors[0].startswith("quotes.md: "))

    def test_broken_image_reported(self):
        write(self.folder, "images/2026-09-26-bad.txt", "no code block\n")
        data, errors = vf.load_folder(self.folder, {})
        self.assertEqual(data["images"], {})
        self.assertEqual(errors, ["images/2026-09-26-bad.txt: no rows in a ``` code block"])


class NeedsRebuild(unittest.TestCase):
    def test_first_run(self):
        self.assertTrue(vf.needs_rebuild("sig", None, None, "2026-09-27"))

    def test_nothing_changed_same_day(self):
        self.assertFalse(vf.needs_rebuild("sig", "sig", "2026-09-27", "2026-09-27"))

    def test_files_changed(self):
        self.assertTrue(vf.needs_rebuild("new", "old", "2026-09-27", "2026-09-27"))

    def test_new_day_rebuilds_even_when_files_are_unchanged(self):
        # otherwise yesterday's quote, pixel art and events would stay all day
        self.assertTrue(vf.needs_rebuild("sig", "sig", "2026-09-26", "2026-09-27"))


class Build(unittest.TestCase):
    def test_empty_data_gives_defaults(self):
        b = vf.build({}, "2026-09-27")
        self.assertEqual(b["feed"]["pages"], [])
        self.assertEqual(b["feed"]["buttons"], [])
        self.assertEqual(b["feed"]["quote"], {"text": "", "author": ""})
        self.assertEqual(b["events"], [])
        self.assertEqual(b["pixelarts"], [])
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

    def test_pixelart_rotation_newest_first_no_future(self):
        art = vf.parse_pixelart(ART)
        images = {"2026-09-20-old.txt": dict(art, title="old"), "2026-09-27-today.txt": dict(art, title="today"),
                  "2026-09-30-future.txt": dict(art, title="future"), "undated.txt": dict(art, title="undated")}
        titles = [a["title"] for a in vf.build({"images": images}, "2026-09-27")["pixelarts"]]
        self.assertEqual(titles, ["today", "old", "undated"])
        self.assertEqual(vf.build({}, "2026-09-27")["pixelarts"], [])


class LastGoodOnDisk(unittest.TestCase):
    def test_round_trip(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "last_good.json")
            vf.save_last_good(path, {"config.md": vf.parse_config(CONFIG)})
            self.assertEqual(vf.load_last_good(path)["config.md"]["pages"], ["Home", "Focus", "Tasks"])

    def test_missing_or_corrupt_file_gives_empty(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "last_good.json")
            self.assertEqual(vf.load_last_good(path), {})
            with open(path, "w") as f:
                f.write("{not json")
            self.assertEqual(vf.load_last_good(path), {})


if __name__ == "__main__":
    unittest.main()
