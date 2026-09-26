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
