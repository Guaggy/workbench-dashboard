# reads the vault's Dashboard/ folder and turns it into the dashboard's mqtt messages
# every file is checked on its own: a broken file is skipped and its last good version is used
import json
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
NUMBER = re.compile(r"^[0-9]+$")  # str.isdigit() also accepts "²", which int() rejects


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
        if not COLOUR.match(colour) or not NUMBER.match(level) or int(level) > 255:
            raise ParseError(f"line {number}: preset needs 'name | #rrggbb | 0-255'")
        presets.append({"name": name, "color": colour, "brightness": int(level)})
    if len(presets) > 6:
        raise ParseError("max 6 light presets")

    buttons = []
    for number, parts in items(s.get("buttons", []), 3):
        label, action, arg = parts[:3]
        if action not in ACTIONS:
            raise ParseError(f"line {number}: unknown action '{action}' (use {', '.join(ACTIONS)})")
        if action == "timer" and not (NUMBER.match(arg) and 1 <= int(arg) <= 180):
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
        if not (HHMM.match(time_) and anim in ANIMS and COLOUR.match(colour) and NUMBER.match(secs)):
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
    except Exception as e:  # anything unexpected only skips this one file
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


def needs_rebuild(signature, built_signature, built_day, today):
    # rebuild when a file changed, and every new day (quote, facts, pixel art and events rotate daily)
    return signature != built_signature or built_day != today


def pixelart_rotation(images, today):
    # pictures to cycle through, newest first ("YYYY-MM-DD-name.txt"); future dates wait, undated ones come last
    dated_names = sorted((n for n in images if DATE.match(n[:10]) and n[:10] <= today), reverse=True)
    undated = sorted(n for n in images if not DATE.match(n[:10]))
    return [images[n] for n in dated_names + undated]


def save_last_good(path, last_good):
    # kept on disk so a hub restart with a broken file still has the last good version
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(last_good, f)
    os.replace(tmp, path)


def load_last_good(path):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


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
        "pixelarts": pixelart_rotation(data.get("images") or {}, today),
        "links": data.get("images/links.md") or [],
    }


if __name__ == "__main__":
    import json
    import sys
    folder_data, folder_errors = load_folder(sys.argv[1], {})
    print(json.dumps({"errors": folder_errors, "build": build(folder_data, date.today().isoformat())}, indent=1))
