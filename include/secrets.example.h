#pragma once

// copy to secrets.h and fill in the passwords (secrets.h is not in git)

#define WIFI_SSID     "New_Trinity_Residents"
#define WIFI_PASSWORD "CHANGE_ME"

// mosquitto on benchpi, published through tailscale funnel (tls on 8443)
#define MQTT_HOST     "bench-hub.example.ts.net"
#define MQTT_PORT     8443
#define MQTT_USER     "dashboard"
#define MQTT_PASSWORD "CHANGE_ME"
