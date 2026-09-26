# updates the dashboard firmware over wifi, run from the project folder on the pc:
#   python tools/ota.py
# builds, puts firmware.bin on benchpi behind a one-time random funnel path, tells the
# dashboard to download it, waits for it to come back, then removes the file again
# (the firmware contains the wifi and mqtt passwords, so it never stays public)
import json
import os
import re
import secrets
import subprocess
import sys
import time

import paho.mqtt.client as mqtt

sys.path.insert(0, os.path.expanduser("~/.bench"))
from claude_notify import read_env  # noqa: E402  (claude's mqtt login)

# ---------- settings ----------
PIO = os.path.expanduser(r"~\.platformio\penv\Scripts\pio.exe")
FIRMWARE = r".pio\build\lilygo-t-display-s3\firmware.bin"
HOST = "pi@benchpi"
FUNNEL_HOST = "benchpi.tailec27dc.ts.net"
WAIT_SECONDS = 180


def ssh(command):
    return subprocess.run(["ssh", "-o", "BatchMode=yes", HOST, command], check=True,
                          capture_output=True, text=True).stdout


def firmware_version():
    with open("include/config.h") as f:
        return re.search(r'FW_VERSION\s+"([^"]+)"', f.read()).group(1)


def watch_heartbeat(env):
    # the dashboard sends a heartbeat on bench/state with its version once a minute
    seen = {}

    def on_message(client, userdata, msg):
        try:
            seen["ver"] = json.loads(msg.payload).get("ver")
        except ValueError:
            pass

    client = mqtt.Client()
    client.username_pw_set(env["MQTT_USER"], env["MQTT_PASSWORD"])
    client.tls_set()
    client.on_message = on_message
    client.connect(env["MQTT_HOST"], int(env["MQTT_PORT"]))
    client.subscribe("bench/state")
    client.loop_start()
    return client, seen


def main():
    version = firmware_version()
    print(f"building firmware {version}...")
    subprocess.run([PIO, "run"], check=True, capture_output=True)

    token = secrets.token_hex(16)
    remote_dir = f"/home/pi/bench/fw/{token}"
    url = f"https://{FUNNEL_HOST}/fw-{token}/firmware.bin"
    env = read_env()
    client, seen = watch_heartbeat(env)

    try:
        print("uploading to benchpi...")
        ssh(f"mkdir -p {remote_dir}")
        subprocess.run(["scp", "-q", FIRMWARE, f"{HOST}:{remote_dir}/firmware.bin"], check=True)
        ssh(f"sudo tailscale funnel --bg --https=443 --set-path /fw-{token} {remote_dir}")

        # the dashboard retries the download itself, and gets the command once more if it stays quiet
        seen.pop("ver", None)
        for attempt in (1, 2):
            print(f"telling the dashboard to update (attempt {attempt})...")
            client.publish("bench/cmd", json.dumps({"ota": url}), qos=1).wait_for_publish(timeout=10)
            deadline = time.time() + WAIT_SECONDS
            while time.time() < deadline and seen.get("ver") != version:
                time.sleep(2)
            if seen.get("ver") == version:
                break
        if seen.get("ver") == version:
            print(f"done, the dashboard is running {version}")
        else:
            print(f"no heartbeat with version {version} after {WAIT_SECONDS} s, check the screen / serial monitor")
    finally:
        # always take the firmware offline again (funnel stores the path with a trailing slash)
        ssh(f"rm -rf {remote_dir}; sudo tailscale funnel --https=443 --set-path /fw-{token}/ off")
        client.loop_stop()
        client.disconnect()
        if "fw-" in ssh("tailscale funnel status"):
            print("WARNING: a /fw- path is still in funnel, remove it with tailscale funnel --https=443 --set-path <path>/ off")
        else:
            print("firmware removed from benchpi")


if __name__ == "__main__":
    main()
