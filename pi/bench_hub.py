# bench dashboard hub on benchpi: collects data and publishes it to mosquitto,
# stores the dashboard's habit/focus logs and runs its shortcut buttons
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import threading
import time
from datetime import datetime, timedelta
from zoneinfo import ZoneInfo

import icalendar
import paho.mqtt.client as mqtt
import recurring_ical_events
import requests
from PIL import Image

import status_report
import vault_feed

# ---------- settings ----------
HOME = os.path.expanduser("~/bench")
ENV_FILE = os.path.join(HOME, ".env")
LOG_FILE = os.path.join(HOME, "log.jsonl")
PULLED_LOG_FILE = os.path.join(HOME, "log.pulled.jsonl")
SHORTCUTS_FILE = os.path.join(HOME, "shortcuts.json")

VAULT_DIR = os.path.join(HOME, "vault")      # read-only copy of the vault's Dashboard/ folder
DRIVE_DIR = "gdrive:"                        # rclone remote, rooted at the Dashboard folder
OUTBOX_STATUS = os.path.join(HOME, "outbox", "status.md")
LAST_GOOD_FILE = os.path.join(HOME, "last_good.json")  # last good vault files, survives restarts

TZ = ZoneInfo("Australia/Perth")
LAT, LON = -31.98, 115.82   # uwa / crawley
STATS_EVERY = 60            # seconds
DASHBOARD_EVERY = 10 * 60
CALENDAR_EVERY = 15 * 60
WEATHER_EVERY = 30 * 60
PHOTO_EVERY = 5 * 60        # checks whether the photo slot changed
PHOTO_SLOT_HOURS = 1        # the photo takes turns: nasa, then each picture in images/links.md
ART_EVERY = 60              # checks whether the pixel-art slot changed
ART_SLOT_HOURS = 1          # pixel art cycles through images/*.txt
SHOW_HOLD = 3600            # a picture sent with bench/show stays this long before rotation resumes
USER_AGENT = "BenchDashboard/1.0 (personal workbench display; benchpi)"  # wikimedia wants one
STATUS_EVERY = 10 * 60
ISS_EVERY = 6 * 3600
ISS_ALERT_MIN = 2           # minutes before a visible pass
CALENDAR_ALERT_MIN = 10     # minutes before a calendar event

PHOTO_SIZE = (320, 170)
PHOTO_MAX_BYTES = 40000     # has to fit the dashboard's mqtt buffer

WEATHER_CODES = {
    0: "Clear", 1: "Mostly clear", 2: "Partly cloudy", 3: "Cloudy", 45: "Fog", 48: "Fog",
    51: "Drizzle", 53: "Drizzle", 55: "Drizzle", 61: "Light rain", 63: "Rain", 65: "Heavy rain",
    80: "Showers", 81: "Showers", 82: "Heavy showers", 95: "Thunderstorm", 96: "Thunderstorm", 99: "Thunderstorm",
}

# ---------- helpers ----------

def read_env():
    env = {}
    with open(ENV_FILE) as f:
        for line in f:
            if "=" in line:
                key, value = line.strip().split("=", 1)
                env[key] = value
    return env


ENV = read_env()


def log(msg):
    print(f"{datetime.now():%H:%M:%S} {msg}", flush=True)


def publish(client, topic, data, retain=True):
    payload = data if isinstance(data, (bytes, str)) else json.dumps(data)
    client.publish("bench/" + topic, payload, qos=1, retain=retain)


def notify(client, title, text, color="#50a0ff", anim="pulse", secs=6):
    publish(client, "notify", {"title": title, "text": text, "color": color, "anim": anim, "secs": secs}, retain=False)


def today_str():
    return datetime.now(TZ).strftime("%Y-%m-%d")


# ---------- collectors ----------

def pi_stats():
    with open("/sys/class/thermal/thermal_zone0/temp") as f:
        temp = int(f.read()) / 1000
    with open("/proc/loadavg") as f:
        load = float(f.read().split()[0])
    mem = {}
    with open("/proc/meminfo") as f:
        for line in f:
            key, value = line.split(":")
            mem[key] = int(value.split()[0])
    used = 100 - mem["MemAvailable"] * 100 // mem["MemTotal"]
    with open("/proc/uptime") as f:
        secs = int(float(f.read().split()[0]))
    uptime = f"{secs // 86400}d {secs % 86400 // 3600}h" if secs >= 86400 else f"{secs // 3600}h {secs % 3600 // 60}m"
    return {"temp": round(temp, 1), "load": load, "mem": used, "uptime": uptime}


def pihole_stats():
    # the pi-hole api answers requests from the pi itself without a password
    summary = requests.get("http://127.0.0.1/api/stats/summary", timeout=10).json()["queries"]
    blocking = requests.get("http://127.0.0.1/api/dns/blocking", timeout=10).json().get("blocking", "?")
    return {
        "queries": summary["total"],
        "blocked": summary["blocked"],
        "percent": round(summary["percent_blocked"], 1),
        "status": "Blocking " + blocking,
    }


def weather():
    r = requests.get("https://api.open-meteo.com/v1/forecast", timeout=15, params={
        "latitude": LAT, "longitude": LON,
        "current": "temperature_2m,weather_code", "timezone": "Australia/Perth",
    }).json()["current"]
    return {"temp": round(r["temperature_2m"]), "text": WEATHER_CODES.get(r["weather_code"], "")}


def calendar_today():
    # today's events from all private ical links, recurring ones included
    start = datetime.now(TZ).replace(hour=0, minute=0, second=0, microsecond=0)
    events = []
    for url in ENV.get("CAL_URLS", "").split():
        cal = icalendar.Calendar.from_ical(requests.get(url, timeout=30).content)
        for e in recurring_ical_events.of(cal).between(start, start + timedelta(days=1)):
            begin, end = e.get("DTSTART").dt, e.get("DTEND").dt if e.get("DTEND") else None
            title = str(e.get("SUMMARY", "")).strip()
            if isinstance(begin, datetime):
                begin = begin.astimezone(TZ)
                end = end.astimezone(TZ) if isinstance(end, datetime) else begin
                events.append({"start": f"{begin:%H:%M}", "end": f"{end:%H:%M}", "title": title})
            else:
                events.append({"start": "All day", "end": "", "title": title})
    # all-day events first, then by start time
    return sorted(events, key=lambda e: ("" if e["start"] == "All day" else e["start"]))[:10]


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
    return d.get("title", ""), to_screen_jpeg(download(url))


def download(url):
    r = requests.get(url, timeout=60, headers={"User-Agent": USER_AGENT})
    r.raise_for_status()
    return r.content


def link_photo(link):
    return link.get("title", ""), to_screen_jpeg(download(link["url"]))


def iss_passes():
    script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "iss_passes.py")
    out = subprocess.run([sys.executable, script], capture_output=True, text=True, timeout=600, check=True)
    return json.loads(out.stdout)


def alert_before(hhmm, minutes):
    total = int(hhmm[:2]) * 60 + int(hhmm[3:5]) - minutes
    return f"{max(total, 0) // 60:02d}:{max(total, 0) % 60:02d}"


def iss_events(passes, today):
    # pulse the strip a couple of minutes before each of today's visible passes
    return [{
        "time": alert_before(p["time"][11:16], ISS_ALERT_MIN),
        "anim": "pulse", "color": "#ffffff", "secs": 20,
        "text": f"ISS overhead in {ISS_ALERT_MIN} min: {p['title']}",
    } for p in passes if p["time"].startswith(today)]


def calendar_events(calendar):
    # a short heads-up before each timed calendar event
    return [{
        "time": alert_before(e["start"], CALENDAR_ALERT_MIN),
        "anim": "pulse", "color": "#50a0ff", "secs": 8,
        "text": f"{e['title']} at {e['start']}",
    } for e in calendar if e["start"] != "All day"]


# ---------- shortcut buttons ----------

def pihole_blocking(enabled, seconds=None):
    body = {"blocking": enabled, "timer": seconds}
    requests.post("http://127.0.0.1/api/dns/blocking", json=body, timeout=10).raise_for_status()


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


def run_shortcut(client, name):
    builtins = {
        "Refresh": lambda: (force_refresh(), "Fetching everything again"),
        "No ads 5m": lambda: (pihole_blocking(False, 300), "Ad blocking paused for 5 min"),
        "Ads on": lambda: (pihole_blocking(True), "Ad blocking is back on"),
        "Today": lambda: (None, today_summary()),
    }
    try:
        if name in builtins:
            _, text = builtins[name]()
        else:
            # anything else comes from shortcuts.json: {"Name": "shell command"}
            with open(SHORTCUTS_FILE) as f:
                command = json.load(f).get(name)
            if not command:
                notify(client, name, "No action set for this button", "#ff8c00")
                return
            result = subprocess.run(command, shell=True, cwd=HOME, capture_output=True, text=True, timeout=60)
            text = (result.stdout.strip() or "Done")[-80:] if result.returncode == 0 else "Failed"
        notify(client, name, text)
        log(f"shortcut '{name}': {text}")
    except Exception as e:
        notify(client, name, f"Failed: {e}"[:80], "#ff3030")
        log(f"shortcut '{name}' failed: {e}")


# ---------- mqtt ----------

last = {}  # when each job last ran


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
    elif topic == "show":
        try:
            link = json.loads(text)
        except ValueError:
            return
        if isinstance(link, dict) and str(link.get("url", "")).startswith("https://"):
            threading.Thread(target=show_now, args=(client, link), daemon=True).start()
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
    client.subscribe([("bench/log", 1), ("bench/shortcut", 1), ("bench/show", 1), ("bench/status", 1), ("bench/state", 1)])


def show_now(client, link):
    # downloads a picture sent with bench/show, puts it on the screen and pauses the rotation for a while
    try:
        title, jpeg = link_photo(link)
    except Exception as e:
        notify(client, "Picture", f"Couldn't load it: {e}"[:80], "#ff3030")
        log(f"show failed: {e}")
        return
    state["photo_hold_until"] = time.time() + SHOW_HOLD
    publish(client, "photo", jpeg)
    publish(client, "photo/title", title)
    publish(client, "cmd", {"show": "photo"}, retain=False)
    log(f"show: {title} ({len(jpeg) // 1024} KB)")


# ---------- main loop ----------

# latest data shared by the jobs
state = {"passes": [], "calendar": [], "vault": None, "vault_sig": "", "built_day": None, "rolled_to": None, "day": "",
         "last_read": None, "errors": [], "photo_key": "", "photo_hold_until": 0, "art_key": ""}
device = {"online": False, "ver": None, "rssi": None, "last_seen": None}
last_good = vault_feed.load_last_good(LAST_GOOD_FILE)  # last good version of every vault file


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
    try:
        funnel = subprocess.run(["tailscale", "funnel", "status"], capture_output=True, text=True, timeout=20).stdout
    except (OSError, subprocess.TimeoutExpired):
        funnel = ""
    return {"mosquitto": active[:1] == ["active"], "bench-hub": active[1:2] == ["active"],
            "funnel": ":8443" in funnel}


def run_every(interval, name, job):
    # runs job when due, one failure never stops the other jobs
    if time.time() - last.get(name, 0) < interval:
        return
    last[name] = time.time()
    try:
        job()
    except Exception as e:
        log(f"{name} failed: {e}")


def main():
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="bench-hub")
    client.username_pw_set(ENV["MQTT_USER"], ENV["MQTT_PASSWORD"])
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect("127.0.0.1", 1883, keepalive=60)
    client.loop_start()

    def publish_space_and_events():
        today = today_str()
        vault = state["vault"] or {}
        publish(client, "space", {"iss": state["passes"][:4], "events": vault.get("space_events", [])})
        items = vault.get("events", []) + iss_events(state["passes"], today) + calendar_events(state["calendar"])
        publish(client, "events", {"date": today, "items": sorted(items, key=lambda e: e["time"])[:16]})
        state["day"] = today

    def stats_job():
        data = {"pi": pi_stats()}
        try:
            data["pihole"] = pihole_stats()
        except Exception as e:
            log(f"pihole failed: {e}")
        publish(client, "stats", data)

    def weather_job():
        publish(client, "weather", weather())

    def calendar_job():
        events = calendar_today()
        if events != state["calendar"] or state["day"] != today_str():
            state["calendar"] = events
            publish(client, "calendar", {"date": today_str(), "items": events})
            publish_space_and_events()
            log(f"calendar: {len(events)} events today")

    def photo_job():
        # the photo slot takes turns every hour: nasa, then each picture in images/links.md
        if time.time() < state["photo_hold_until"]:
            return
        links = (state["vault"] or {}).get("links", [])
        choices = [None] + links
        pick = choices[datetime.now(TZ).hour // PHOTO_SLOT_HOURS % len(choices)]
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

    def iss_job():
        # the heavy calculation runs every few hours in its own thread
        def work():
            state["passes"] = iss_passes()
            publish_space_and_events()
            log(f"iss: {len(state['passes'])} visible passes")
        threading.Thread(target=work, daemon=True).start()

    def dashboard_job():
        subprocess.run(["rclone", "sync", DRIVE_DIR, VAULT_DIR, "--exclude", "status.md"],
                       check=True, timeout=180, capture_output=True)
        signature, today = folder_signature(VAULT_DIR), today_str()
        state["last_read"] = f"{datetime.now(TZ):%Y-%m-%d %H:%M}"
        if not vault_feed.needs_rebuild(signature, state["vault_sig"], state["built_day"], today):
            return
        data, errors = vault_feed.load_folder(VAULT_DIR, last_good)
        state["vault"] = built = vault_feed.build(data, today)
        state["vault_sig"], state["built_day"] = signature, today
        vault_feed.save_last_good(LAST_GOOD_FILE, last_good)
        publish(client, "feed", built["feed"])
        state["art_key"] = ""  # the rotation below republishes the right picture
        publish_space_and_events()
        if errors and errors != state["errors"]:
            notify(client, "Dashboard files", f"{len(errors)} error(s), see status.md", "#ff8c00", secs=10)
        state["errors"] = errors
        log(f"Dashboard folder published ({len(errors)} errors)")

    def art_job():
        # cycles through the pixel art, newest first, one picture per slot
        arts = (state["vault"] or {}).get("pixelarts", [])
        if not arts:
            return
        index = datetime.now(TZ).hour // ART_SLOT_HOURS % len(arts)
        key = f"{today_str()}:{index}:{len(arts)}:{state['vault_sig']}"
        if key == state["art_key"]:
            return
        state["art_key"] = key
        publish(client, "pixelart", arts[index])

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

    while True:
        run_every(STATS_EVERY, "stats", stats_job)
        run_every(WEATHER_EVERY, "weather", weather_job)
        run_every(DASHBOARD_EVERY, "dashboard", dashboard_job)
        run_every(CALENDAR_EVERY, "calendar", calendar_job)
        run_every(PHOTO_EVERY, "photo", photo_job)
        run_every(ART_EVERY, "art", art_job)
        run_every(ISS_EVERY, "iss", iss_job)
        run_every(STATUS_EVERY, "status", status_job)
        # new day: rebuild once (quote, facts, pixel art, events rotate) and fetch the new calendar
        today = today_str()
        if state["built_day"] and state["built_day"] != today and state["rolled_to"] != today:
            state["rolled_to"] = today
            last["dashboard"] = last["calendar"] = 0
        time.sleep(5)


if __name__ == "__main__":
    main()
