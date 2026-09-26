# parses meta/dashboard.md from the atlas vault into the dashboard's mqtt messages
import re


def split_sections(text):
    sections, current = {}, None
    for line in text.splitlines():
        if line.startswith("## "):
            current = line[3:].strip().lower()
            sections[current] = []
        elif current is not None:
            sections[current].append(line)
    return sections


def items(lines):
    # "- a | b | c" -> [["a", "b", "c"], ...]
    return [[part.strip() for part in line[2:].split("|")] for line in lines if line.startswith("- ")]


def first_line(lines):
    for line in lines:
        if line.strip():
            return line.strip()
    return ""


def dated(lines):
    return [{"date": i[0], "title": i[1]} for i in items(lines) if len(i) >= 2]


def parse(text):
    sections = split_sections(text)
    get = lambda name: sections.get(name, [])

    match = re.search(r"^updated:\s*(\d{4}-\d{2}-\d{2})", text, re.M)
    updated = match.group(1) if match else ""

    quote = first_line(get("quote")).split("|")
    night = first_line(get("night")).split("-")

    feed = {
        "deadlines": dated(get("deadlines")),
        "todos": [i[0] for i in items(get("todos"))],
        "habits": [i[0] for i in items(get("habits"))][:4],
        "shortcuts": [i[0] for i in items(get("shortcuts"))][:4],
        "countdowns": dated(get("countdowns")),
        "facts": [i[0] for i in items(get("facts"))],
        "fortunes": [i[0] for i in items(get("fortunes"))],
        "focus": first_line(get("focus")),
        "message": first_line(get("message")),
        "quote": {"text": quote[0].strip(), "author": quote[1].strip() if len(quote) > 1 else ""},
    }
    if len(night) == 2:
        feed["night"] = {"start": night[0].strip(), "end": night[1].strip()}

    events = []
    for i in items(get("events")):
        if len(i) >= 5:
            events.append({"time": i[0], "anim": i[1], "color": i[2], "secs": int(i[3]), "text": i[4]})

    return {
        "updated": updated,
        "feed": feed,
        "events": events,
        "space_events": dated(get("space events")),
        "pixelart": parse_pixelart(get("pixel art")),
    }


def parse_pixelart(lines):
    title, palette, rows, in_block = "", {}, [], False
    for line in lines:
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
                if len(key) == 1 and colour.startswith("#"):
                    palette[key] = colour
    if not rows:
        return None
    return {"title": title, "w": max(len(r) for r in rows), "h": len(rows), "palette": palette, "rows": rows}


if __name__ == "__main__":
    import json
    import sys
    print(json.dumps(parse(open(sys.argv[1], encoding="utf-8").read()), indent=1))
