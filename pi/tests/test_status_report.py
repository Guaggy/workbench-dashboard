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
