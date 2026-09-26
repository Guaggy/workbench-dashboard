# prints the visible iss passes over perth for the next DAYS days as json
# runs as a separate process so skyfield/numpy don't stay in the hub's memory
import json
import os
import time
from datetime import timedelta

from skyfield.api import Loader, wgs84

# ---------- settings ----------
LAT, LON = -31.98, 115.82          # uwa / crawley
MIN_ALTITUDE = 20                  # degrees, lower passes are hard to see
DAYS = 14                          # visible passes come in batches every week or two
TLE_URL = "https://celestrak.org/NORAD/elements/gp.php?CATNR=25544&FORMAT=tle"
CACHE = os.path.expanduser("~/bench/cache")

COMPASS = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]


def compass(degrees):
    return COMPASS[int((degrees + 22.5) // 45) % 8]


def main():
    os.makedirs(CACHE, exist_ok=True)
    loader = Loader(CACHE, verbose=False)
    tle_path = os.path.join(CACHE, "iss.tle")
    stale = not os.path.exists(tle_path) or time.time() - os.path.getmtime(tle_path) > 86400
    iss = loader.tle_file(TLE_URL, filename="iss.tle", reload=stale)[0]
    eph = loader("de421.bsp")  # sun position, 17 MB, downloaded once
    ts = loader.timescale()

    here = wgs84.latlon(LAT, LON)
    start = ts.now()
    end = ts.tt_jd(start.tt + DAYS)
    times, events = iss.find_events(here, start, end, altitude_degrees=10.0)

    passes = []
    current = {}
    for t, event in zip(times, events):
        if event == 0:
            current = {"rise": t}
        elif event == 1 and current:
            current["peak"] = t
        elif event == 2 and "peak" in current:
            current["set"] = t
            peak = current["peak"]
            alt, az, _ = (iss - here).at(peak).altaz()
            sun_alt = (eph["earth"] + here).at(peak).observe(eph["sun"]).apparent().altaz()[0]
            # visible when the sky is dark and the iss is still in sunlight
            visible = sun_alt.degrees < -6 and iss.at(peak).is_sunlit(eph)
            if visible and alt.degrees >= MIN_ALTITUDE:
                rise_az = (iss - here).at(current["rise"]).altaz()[1].degrees
                set_az = (iss - here).at(t).altaz()[1].degrees
                minutes = round((t - current["rise"]) * 24 * 60)
                local = current["rise"].utc_datetime() + timedelta(hours=8)  # awst
                passes.append({
                    "time": local.strftime("%Y-%m-%d %H:%M"),
                    # plain ascii, the dashboard fonts have no degree sign or arrows
                    "title": f"{alt.degrees:.0f} deg up, {minutes} min, {compass(rise_az)} to {compass(set_az)}",
                })
            current = {}

    print(json.dumps(passes))


if __name__ == "__main__":
    main()
