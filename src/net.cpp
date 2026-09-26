#include "net.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include "config.h"
#include "secrets.h"
#include "ca_cert.h"
#include "state.h"
#include "leds.h"
#include "ui.h"

static WiFiClientSecure tlsClient;
static PubSubClient mqtt(tlsClient);

static unsigned long lastMqttTry = 0;
static unsigned long lastHeartbeat = 0;
static String pendingOtaUrl;

// messages published while offline, sent on reconnect
static const int QUEUE_SIZE = 16;
static String queueTopic[QUEUE_SIZE], queuePayload[QUEUE_SIZE];
static int queueCount = 0;

// ---------- helpers ----------

CRGB parseColour(const String &hex, CRGB fallback) {
  if (hex.length() != 7 || hex[0] != '#') return fallback;
  long v = strtol(hex.c_str() + 1, nullptr, 16);
  return CRGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

int parseMinutes(const String &hhmm) {
  int colon = hhmm.indexOf(':');
  if (colon < 1) return -1;
  return hhmm.substring(0, colon).toInt() * 60 + hhmm.substring(colon + 1).toInt();
}

String todayString() {
  struct tm t;
  if (!getLocalTime(&t, 0)) return "";
  char buf[11];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &t);
  return buf;
}

static int nowMinutes() {
  struct tm t;
  if (!getLocalTime(&t, 0)) return -1;
  return t.tm_hour * 60 + t.tm_min;
}

bool netWifiOk() { return WiFi.status() == WL_CONNECTED; }
bool netMqttOk() { return mqtt.connected(); }
bool netTimeOk() { return time(nullptr) > 1700000000; }

// copies up to max strings from a json array
static int readStrings(JsonArrayConst arr, String *out, int max) {
  int n = 0;
  for (JsonVariantConst v : arr) {
    if (n >= max) break;
    out[n++] = v.as<String>();
  }
  return n;
}

static int readDated(JsonArrayConst arr, DatedItem *out, int max, const char *dateKey = "date") {
  int n = 0;
  for (JsonObjectConst o : arr) {
    if (n >= max) break;
    out[n].date = o[dateKey] | "";
    out[n].title = o["title"] | "";
    n++;
  }
  return n;
}

// ---------- topic handlers ----------

static void handleFeed(JsonDocument &doc) {
  Feed &f = app.feed;
  f.nDeadlines = readDated(doc["deadlines"], f.deadlines, 8);
  f.nTodos = readStrings(doc["todos"], f.todos, 6);
  f.nHabits = readStrings(doc["habits"], f.habits, 4);
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
  f.nCountdowns = readDated(doc["countdowns"], f.countdowns, 6);
  f.nFacts = readStrings(doc["facts"], f.facts, 12);
  f.nFortunes = readStrings(doc["fortunes"], f.fortunes, 12);
  f.focus = doc["focus"] | "";
  f.message = doc["message"] | "";
  f.quote = doc["quote"]["text"] | "";
  f.quoteAuthor = doc["quote"]["author"] | "";

  int ns = parseMinutes(doc["night"]["start"] | "");
  int ne = parseMinutes(doc["night"]["end"] | "");
  f.nightStart = ns >= 0 ? ns : NIGHT_START_DEFAULT;
  f.nightEnd = ne >= 0 ? ne : NIGHT_END_DEFAULT;
  f.valid = true;
}

static void handleEvents(JsonDocument &doc) {
  app.eventsDate = doc["date"] | "";
  bool isToday = app.eventsDate == todayString();
  int now = nowMinutes();

  app.nEvents = 0;
  for (JsonObjectConst o : doc["items"].as<JsonArrayConst>()) {
    if (app.nEvents >= 16) break;
    TimedEvent &e = app.events[app.nEvents];
    e.minute = parseMinutes(o["time"] | "");
    if (e.minute < 0) continue;
    e.anim = o["anim"] | "pulse";
    e.colour = parseColour(o["color"] | "", CRGB::White);
    e.secs = o["secs"] | 10;
    e.text = o["text"] | "";
    // don't replay events that already passed today
    e.fired = isToday && now >= 0 && e.minute < now;
    app.nEvents++;
  }
}

static void handleCalendar(JsonDocument &doc) {
  app.nCalendar = 0;
  for (JsonObjectConst o : doc["items"].as<JsonArrayConst>()) {
    if (app.nCalendar >= 10) break;
    CalEvent &c = app.calendar[app.nCalendar++];
    c.start = o["start"] | "";
    c.end = o["end"] | "";
    c.title = o["title"] | "";
  }
}

static void handleStats(JsonDocument &doc) {
  Stats &s = app.stats;
  s.piTemp = doc["pi"]["temp"] | 0.0f;
  s.piLoad = doc["pi"]["load"] | 0.0f;
  s.piMem = doc["pi"]["mem"] | 0;
  s.piUptime = doc["pi"]["uptime"] | "";
  s.queries = doc["pihole"]["queries"] | 0L;
  s.blocked = doc["pihole"]["blocked"] | 0L;
  s.blockedPct = doc["pihole"]["percent"] | 0.0f;
  s.piholeStatus = doc["pihole"]["status"] | "";
  s.receivedAt = millis();
  s.valid = true;
}

static void handleSpace(JsonDocument &doc) {
  app.nIss = readDated(doc["iss"], app.iss, 4, "time");
  app.nSpaceEvents = readDated(doc["events"], app.spaceEvents, 6);
}

static void handleWeather(JsonDocument &doc) {
  app.weatherTemp = doc["temp"] | 0.0f;
  app.weatherText = doc["text"] | "";
  app.hasWeather = true;
}

static void handlePixelArt(JsonDocument &doc) {
  PixelArt &a = app.art;
  a.w = doc["w"] | 0;
  a.h = min((int)(doc["h"] | 0), 48);
  a.title = doc["title"] | "";
  for (int i = 0; i < 128; i++) a.palette[i] = 0;
  for (JsonPairConst p : doc["palette"].as<JsonObjectConst>()) {
    char key = p.key().c_str()[0];
    if (key > 0) {
      CRGB c = parseColour(p.value().as<String>(), CRGB::Black);
      a.palette[(int)key] = ((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3);
    }
  }
  int n = readStrings(doc["rows"], a.rows, 48);
  a.h = min(a.h, n);
  a.valid = a.w > 0 && a.h > 0;
}

static void handleClaude(JsonDocument &doc) {
  String state = doc["state"] | "done";
  String project = doc["project"] | "Claude Code";
  String text = doc["text"] | "";

  if (state == "waiting") {
    uiBanner("Claude needs you", project + (text.length() ? ": " + text : ""), CRGB(255, 140, 0), 0, true);
    ledsPlay("pulse", CRGB(255, 140, 0), 8);
  } else if (state == "error") {
    uiBanner("Claude hit an error", project + (text.length() ? ": " + text : ""), CRGB::Red, 30, false);
    ledsPlay("flash", CRGB::Red, 4);
  } else {
    uiBanner("Claude is done", project + (text.length() ? ": " + text : ""), CRGB(0, 200, 80), 20, false);
    ledsPlay("pulse", CRGB(0, 200, 80), 4);
  }
}

static void handleNotify(JsonDocument &doc) {
  CRGB colour = parseColour(doc["color"] | "", CRGB(80, 160, 255));
  String anim = doc["anim"] | "pulse";
  int secs = doc["secs"] | 10;
  uiBanner(doc["title"] | "Atlas", doc["text"] | "", colour, secs, doc["sticky"] | false);
  if (anim != "none") ledsPlay(anim, colour, min(secs, 10));
}

static void onMessage(char *topicRaw, byte *payload, unsigned int length) {
  String topic = String(topicRaw).substring(strlen(TOPIC_PREFIX));

  // the photo is raw jpeg, not json
  if (topic == "photo") {
    if (app.photo) free(app.photo);
    app.photo = (uint8_t *)ps_malloc(length);
    if (app.photo) {
      memcpy(app.photo, payload, length);
      app.photoLen = length;
      app.photoChanged = true;
    }
    return;
  }
  if (topic == "photo/title") {
    app.photoTitle = String((const char *)payload, length);
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, payload, length)) {
    Serial.printf("bad json on %s\n", topicRaw);
    return;
  }

  if (topic == "feed") handleFeed(doc);
  else if (topic == "events") handleEvents(doc);
  else if (topic == "calendar") handleCalendar(doc);
  else if (topic == "stats") handleStats(doc);
  else if (topic == "space") handleSpace(doc);
  else if (topic == "weather") handleWeather(doc);
  else if (topic == "pixelart") handlePixelArt(doc);
  else if (topic == "claude") handleClaude(doc);
  else if (topic == "notify") handleNotify(doc);
  else if (topic == "led/set") {
    ledsSetOverride(doc["mode"] | "auto", parseColour(doc["color"] | "", CRGB::White), doc["brightness"] | 200);
  } else if (topic == "led/anim") {
    ledsPlay(doc["anim"] | "pulse", parseColour(doc["color"] | "", CRGB::White), doc["secs"] | 5);
  } else if (topic == "cmd") {
    if (doc["reboot"] | false) ESP.restart();
    if (String(doc["show"] | "") == "photo") uiShowPhoto();
    pendingOtaUrl = doc["ota"] | "";
  }
}

// ---------- connection ----------

static const char *SUBSCRIPTIONS[] = {
  "feed", "events", "calendar", "stats", "space", "weather", "pixelart",
  "photo", "photo/title", "claude", "notify", "led/set", "led/anim", "cmd",
};

static void connectMqtt() {
  String status = String(TOPIC_PREFIX) + "status";
  Serial.println("mqtt: connecting");
  if (!mqtt.connect("bench-dashboard", MQTT_USER, MQTT_PASSWORD, status.c_str(), 1, true, "offline")) {
    Serial.printf("mqtt: failed, state %d\n", mqtt.state());
    return;
  }
  Serial.println("mqtt: connected");
  mqtt.publish(status.c_str(), "online", true);
  for (const char *s : SUBSCRIPTIONS) mqtt.subscribe((String(TOPIC_PREFIX) + s).c_str(), 1);

  for (int i = 0; i < queueCount; i++) mqtt.publish(queueTopic[i].c_str(), queuePayload[i].c_str());
  queueCount = 0;
}

static void runOta(const String &url) {
  uiBanner("Updating", "Downloading new firmware...", CRGB(80, 160, 255), 60, true);
  uiRender();
  mqtt.disconnect();

  // the wifi here is weak, so a download can drop halfway: try a few times
  httpUpdate.rebootOnUpdate(true);
  for (int attempt = 1; attempt <= 3; attempt++) {
    WiFiClientSecure otaClient;
    otaClient.setCACert(CA_CERT);
    Serial.printf("ota: attempt %d\n", attempt);
    httpUpdate.update(otaClient, url);  // only returns when the update failed
    Serial.printf("ota: failed, %s\n", httpUpdate.getLastErrorString().c_str());
    delay(2000);
  }
  uiBanner("Update failed", httpUpdate.getLastErrorString(), CRGB::Red, 30, false);
}

void netPublish(const String &topic, const String &payload) {
  String full = String(TOPIC_PREFIX) + topic;
  if (mqtt.connected()) {
    mqtt.publish(full.c_str(), payload.c_str());
    return;
  }
  if (queueCount < QUEUE_SIZE) {
    queueTopic[queueCount] = full;
    queuePayload[queueCount] = payload;
    queueCount++;
  }
}

// logs why the access point dropped us (15 = wrong password, 201 = network not found)
static void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
    Serial.printf("wifi: disconnected, reason %d\n", info.wifi_sta_disconnected.reason);
}

// one scan when wifi won't connect, to see if the network is visible
static void scanForNetwork() {
  Serial.println("wifi: scanning...");
  int n = WiFi.scanNetworks();
  bool found = false;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) != WIFI_SSID) continue;
    found = true;
    Serial.printf("wifi: found '%s' ch %d rssi %d auth %d\n", WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i), WiFi.encryptionType(i));
  }
  if (!found) Serial.printf("wifi: '%s' not visible (%d networks seen)\n", WIFI_SSID, n);
  WiFi.scanDelete();
}

void netBegin() {
  WiFi.onEvent(onWifiEvent);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("bench-dashboard");
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  configTzTime(TIMEZONE, NTP_SERVER);

  tlsClient.setCACert(CA_CERT);
  tlsClient.setHandshakeTimeout(8);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(MQTT_BUFFER);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(10);
  mqtt.setCallback(onMessage);
}

void netLoop() {
  if (pendingOtaUrl.length()) {
    String url = pendingOtaUrl;
    pendingOtaUrl = "";
    runOta(url);
  }

  // log wifi and clock changes so problems show up on the serial monitor
  static wl_status_t lastWifi = WL_IDLE_STATUS;
  static bool timeLogged = false;
  if (WiFi.status() != lastWifi) {
    lastWifi = WiFi.status();
    if (lastWifi == WL_CONNECTED) Serial.printf("wifi: connected, ip %s, rssi %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    else Serial.printf("wifi: status %d\n", lastWifi);
  }
  static bool scanned = false;
  if (!scanned && !netWifiOk() && millis() > 20000) {
    scanned = true;
    scanForNetwork();
  }
  if (!timeLogged && netTimeOk()) {
    timeLogged = true;
    Serial.println("time: synced " + todayString());
  }

  // tls needs the real time to check the certificate
  if (!netWifiOk() || !netTimeOk()) return;

  // connecting blocks for a moment (tls handshake), so back off up to a minute
  static unsigned long retryDelay = MQTT_RETRY_MS;
  if (!mqtt.connected()) {
    if (millis() - lastMqttTry > retryDelay) {
      lastMqttTry = millis();
      connectMqtt();
      retryDelay = mqtt.connected() ? MQTT_RETRY_MS : min(retryDelay * 2, 60000UL);
    }
    return;
  }
  mqtt.loop();

  if (millis() - lastHeartbeat > HEARTBEAT_MS) {
    lastHeartbeat = millis();
    JsonDocument doc;
    doc["ver"] = FW_VERSION;
    doc["rssi"] = WiFi.RSSI();
    doc["uptime"] = millis() / 1000;
    doc["heap"] = ESP.getFreeHeap();
    String out;
    serializeJson(doc, out);
    netPublish("state", out);
  }
}
