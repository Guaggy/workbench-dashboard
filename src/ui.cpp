#include "ui.h"
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "config.h"
#include "state.h"
#include "leds.h"
#include "net.h"
#include "content.h"

static TFT_eSPI tft;
static TFT_eSprite spr(&tft);       // the whole screen, redrawn every frame
static TFT_eSprite photoSpr(&tft);  // decoded photo of the day
static Preferences prefs;

static const int W = 320, H = 170;
static const int TOP = 20, BOTTOM = 134;  // content area, above the button labels

// the four buttons sit in a 2x2 grid bottom right, their labels are drawn right next to them:
// A top-left, B top-right, C bottom-left, D bottom-right (swap BTN_A..D pins in config.h to match)
static const int KEY_W = 62, KEY_H = 16;
static const int KEY_X = 320 - 10 - 2 * KEY_W, KEY_Y = 170 - 2 * KEY_H - 2;

// colours (rgb565)
static const uint16_t C_BG = TFT_BLACK;
static const uint16_t C_TEXT = TFT_WHITE;
static const uint16_t C_GREY = 0x7BEF;
static const uint16_t C_DARK = 0x2104;
static const uint16_t C_ACCENT = 0x551F;  // sky blue
static const uint16_t C_AMBER = 0xFD00;
static const uint16_t C_GREEN = 0x064A;
static const uint16_t C_RED = TFT_RED;

enum Page { P_HOME, P_LIGHT, P_FOCUS, P_TASKS, P_HABITS, P_CALENDAR, P_SPACE, P_STATS, P_GALLERY, NUM_PAGES };
static const char *PAGE_NAMES[NUM_PAGES] = {
  "Home", "Bench light", "Focus", "Tasks", "Habits", "Today", "Space", "Stats", "Gallery",
};

static Page page = P_HOME;
static int scroll = 0;
static bool galleryPhoto = false;
static int presetIndex = -1;  // light preset picked last, -1 = none

// home buttons until config.md provides some (first start, or no good config on the hub yet)
static const ButtonConfig DEFAULT_BUTTONS[4] = {
  {"Refresh", "shortcut", "Refresh"},
  {"Focus 25", "timer", "25"},
  {"No ads 5m", "shortcut", "No ads 5m"},
  {"Today", "shortcut", "Today"},
};

static const ButtonConfig *homeButtons(int &n) {
  if (app.feed.nButtons) { n = app.feed.nButtons; return app.feed.buttons; }
  n = 4;
  return DEFAULT_BUTTONS;
}

// page order from config.md, or the built-in order when it lists none; home is always first
static int pageOrder(Page *out) {
  int n = 0;
  out[n++] = P_HOME;
  if (app.feed.nPages == 0) {
    for (int p = 1; p < NUM_PAGES; p++) out[n++] = (Page)p;
    return n;
  }
  // the pi removes duplicates and puts home first, so home is skipped here
  for (int i = 0; i < app.feed.nPages && n < NUM_PAGES; i++)
    for (int p = 1; p < NUM_PAGES; p++)
      if (app.feed.pages[i] == PAGE_NAMES[p]) out[n++] = (Page)p;
  return n;
}

static bool pageShown(Page p) {
  Page order[NUM_PAGES];
  int n = pageOrder(order);
  for (int i = 0; i < n; i++) if (order[i] == p) return true;
  return false;
}

static Page stepPage(int direction) {
  Page order[NUM_PAGES];
  int n = pageOrder(order), at = 0;
  for (int i = 0; i < n; i++) if (order[i] == page) at = i;
  return order[(at + direction + n) % n];
}

enum ScreenState { AWAKE, SAVER, OFF };
static unsigned long lastActivity = 0;
static int backlight = -1;

struct Banner {
  bool active = false;
  bool sticky = false;
  String title, text;
  uint16_t colour = C_ACCENT;
  unsigned long until = 0;
};
static Banner banner;

// ---------- small helpers ----------

static uint16_t rgb565(CRGB c) { return ((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3); }

static bool localNow(struct tm &t) { return netTimeOk() && getLocalTime(&t, 0); }

static int minutesNow() {
  struct tm t;
  return localNow(t) ? t.tm_hour * 60 + t.tm_min : -1;
}

static String dateString(time_t when) {
  struct tm t;
  localtime_r(&when, &t);
  char buf[11];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &t);
  return buf;
}

// whole days from today to "YYYY-MM-DD", 9999 if unknown
static int daysUntil(const String &date) {
  struct tm now;
  if (date.length() < 10 || !localNow(now)) return 9999;
  struct tm d = {};
  d.tm_year = date.substring(0, 4).toInt() - 1900;
  d.tm_mon = date.substring(5, 7).toInt() - 1;
  d.tm_mday = date.substring(8, 10).toInt();
  d.tm_hour = 12;
  now.tm_hour = 12;
  now.tm_min = now.tm_sec = 0;
  return lround(difftime(mktime(&d), mktime(&now)) / 86400.0);
}

static String daysLabel(int days) {
  if (days == 9999) return "?";
  if (days < 0) return "late";
  if (days == 0) return "today";
  return String(days) + "d";
}

static uint16_t daysColour(int days) {
  if (days <= 2) return C_RED;
  if (days <= 7) return C_AMBER;
  return C_GREEN;
}

// cuts text with ".." so it fits in w pixels
static String fitText(String s, int w, uint8_t font) {
  if (spr.textWidth(s, font) <= w) return s;
  while (s.length() > 1 && spr.textWidth(s + "..", font) > w) s.remove(s.length() - 1);
  return s + "..";
}

// word wraps text, returns the y below the last line
static int drawWrapped(const String &text, int x, int y, int w, uint8_t font, int maxLines, uint16_t colour) {
  spr.setTextColor(colour);
  int lineH = spr.fontHeight(font) + 2;
  String line, word;
  int lines = 0;
  for (int i = 0; i <= (int)text.length(); i++) {
    char c = i < (int)text.length() ? text[i] : ' ';
    if (c != ' ') { word += c; continue; }
    if (!word.length()) continue;
    String test = line.length() ? line + " " + word : word;
    if (spr.textWidth(test, font) > w && line.length()) {
      if (lines == maxLines - 1) { spr.drawString(fitText(line + " " + word, w, font), x, y, font); return y + lineH; }
      spr.drawString(line, x, y, font);
      y += lineH;
      lines++;
      line = word;
    } else {
      line = test;
    }
    word = "";
  }
  if (line.length()) { spr.drawString(line, x, y, font); y += lineH; }
  return y;
}

// ---------- night / deadline checks ----------

bool uiIsNight() {
  int now = minutesNow();
  if (now < 0) return false;
  int s = app.feed.nightStart, e = app.feed.nightEnd;
  return s > e ? (now >= s || now < e) : (now >= s && now < e);
}

bool uiDeadlineSoon() {
  for (int i = 0; i < app.feed.nDeadlines; i++) {
    int d = daysUntil(app.feed.deadlines[i].date);
    if (d >= 0 && d * 24 <= DEADLINE_WARN_HOURS) return true;
  }
  return false;
}

// ---------- habits (stored in flash so streaks survive reboots) ----------

static const char *DEFAULT_HABITS[4] = {"Typing", "Exercise", "Read", "Tidy bench"};

static int habitCount() { return app.feed.nHabits ? app.feed.nHabits : 4; }
static String habitName(int i) { return app.feed.nHabits ? app.feed.habits[i] : DEFAULT_HABITS[i]; }

// flash key from the habit's name, so reordering config.md keeps the streaks (nvs keys max 15 chars)
static String habitKey(const String &name) {
  uint32_t h = 2166136261u;  // fnv-1a
  for (size_t i = 0; i < name.length(); i++) { h ^= (uint8_t)name[i]; h *= 16777619u; }
  char key[12];
  snprintf(key, sizeof(key), "h%08lx", (unsigned long)h);
  return key;
}

// stored as "name|YYYY-MM-DD|streak"
static void habitRead(int i, String &lastDate, int &streak) {
  String v = prefs.getString(habitKey(habitName(i)).c_str(), "");
  int a = v.indexOf('|'), b = v.lastIndexOf('|');
  lastDate = "";
  streak = 0;
  if (a < 0 || b <= a || v.substring(0, a) != habitName(i)) return;
  lastDate = v.substring(a + 1, b);
  streak = v.substring(b + 1).toInt();
  // a streak only counts if it was kept up until yesterday
  String today = todayString(), yesterday = dateString(time(nullptr) - 86400);
  if (lastDate != today && lastDate != yesterday) streak = 0;
}

static void habitLog(int i) {
  String today = todayString();
  if (!today.length()) {
    uiBanner("No clock yet", "Waiting for wifi time", CRGB::Orange, 3, false);
    return;
  }
  String lastDate;
  int streak;
  habitRead(i, lastDate, streak);
  if (lastDate == today) {
    uiBanner(habitName(i), "Already done today", CRGB(80, 160, 255), 3, false);
    return;
  }
  streak++;
  prefs.putString(habitKey(habitName(i)).c_str(), habitName(i) + "|" + today + "|" + streak);

  JsonDocument doc;
  doc["type"] = "habit";
  doc["name"] = habitName(i);
  doc["date"] = today;
  doc["streak"] = streak;
  String out;
  serializeJson(doc, out);
  netPublish("log", out);

  uiBanner(habitName(i) + " done!", String(streak) + " day streak", CRGB(0, 200, 80), 3, false);
  ledsPlay(streak % 7 == 0 ? "rainbow" : "confetti", CRGB::Green, 3);
}

// ---------- focus timer ----------

static int focusMinutes = FOCUS_DEFAULT_MIN;
static bool focusRunning = false;
static unsigned long focusEndsAt = 0;
static long focusRemaining = FOCUS_DEFAULT_MIN * 60000L;

static long focusLeftMs() { return focusRunning ? (long)(focusEndsAt - millis()) : focusRemaining; }

void focusTick() {
  if (!focusRunning || (long)(focusEndsAt - millis()) > 0) return;
  focusRunning = false;
  focusRemaining = focusMinutes * 60000L;

  JsonDocument doc;
  doc["type"] = "focus";
  doc["project"] = app.feed.focus.length() ? app.feed.focus : "none";
  doc["minutes"] = focusMinutes;
  doc["date"] = todayString();
  String out;
  serializeJson(doc, out);
  netPublish("log", out);

  uiBanner("Focus session done", String(focusMinutes) + " min" + (app.feed.focus.length() ? " on " + app.feed.focus : ""),
           CRGB(0, 200, 80), 30, false);
  ledsPlay("confetti", CRGB::Green, 6);
}

// ---------- moon ----------

// days since the last new moon (reference new moon 2000-01-06 18:14 utc)
static double moonAge() {
  const double SYNODIC = 29.530588853;
  double age = fmod((time(nullptr) - 947182440.0) / 86400.0, SYNODIC);
  return age < 0 ? age + SYNODIC : age;
}

static const char *moonName(double age) {
  const char *names[] = {"New moon", "Waxing crescent", "First quarter", "Waxing gibbous",
                         "Full moon", "Waning gibbous", "Last quarter", "Waning crescent"};
  return names[(int)((age / 29.530588853) * 8 + 0.5) % 8];
}

static void drawMoon(int cx, int cy, int r) {
  double phase = moonAge() / 29.530588853;
  double k = cos(2 * PI * phase);
  spr.fillCircle(cx, cy, r, C_DARK);
  for (int dy = -r; dy <= r; dy++) {
    int xw = sqrt(r * r - dy * dy);
    int from, to;
    if (phase < 0.5) { from = xw * k; to = xw; }      // waxing, lit on the right
    else { from = -xw; to = -xw * k; }                // waning, lit on the left
    if (to > from) spr.drawFastHLine(cx + from, cy + dy, to - from, 0xEF5B);
  }
}

// ---------- pixel art and photo ----------

static void drawPixelArt(int top, int bottom) {
  PixelArt &a = app.art;
  int scale = max(1, min(W / a.w, (bottom - top) / a.h));
  int x0 = (W - a.w * scale) / 2, y0 = top + (bottom - top - a.h * scale) / 2;
  for (int y = 0; y < a.h; y++) {
    const String &row = a.rows[y];
    for (int x = 0; x < a.w && x < (int)row.length(); x++) {
      uint8_t c = row[x];
      if (c < 128 && a.palette[c]) spr.fillRect(x0 + x * scale, y0 + y * scale, scale, scale, a.palette[c]);
    }
  }
}

static bool jpgToSprite(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) photoSpr.drawPixel(x + i, y + j, bitmap[j * w + i]);
  return true;
}

static void decodePhotoIfNew() {
  if (!app.photoChanged || !app.photo) return;
  app.photoChanged = false;
  uint16_t w = 0, h = 0;
  TJpgDec.getJpgSize(&w, &h, app.photo, app.photoLen);
  photoSpr.fillSprite(C_BG);
  TJpgDec.drawJpg((W - (int)w) / 2, (H - (int)h) / 2, app.photo, app.photoLen);
}

static bool hasPhoto() { return app.photo && app.photoLen; }

// ---------- bars ----------

static void drawTopBar() {
  spr.fillRect(0, 0, W, 18, C_DARK);
  spr.setTextColor(C_TEXT);
  spr.drawString(PAGE_NAMES[page], 6, 1, 2);

  struct tm t;
  String clock = localNow(t) ? String(t.tm_hour < 10 ? "0" : "") + t.tm_hour + ":" + (t.tm_min < 10 ? "0" : "") + t.tm_min : "--:--";
  spr.drawRightString(clock, W - 14, 1, 2);  // leaves room for the onboard button marker

  if (focusRunning) {
    long s = focusLeftMs() / 1000;
    spr.setTextColor(C_GREEN);
    spr.drawCentreString(String(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + s % 60, W / 2 + 20, 1, 2);
  }

  // connection dot: green = broker, amber = wifi only, red = offline
  uint16_t dot = netMqttOk() ? C_GREEN : netWifiOk() ? C_AMBER : C_RED;
  spr.fillCircle(W - 60, 9, 3, dot);
}

static void drawSoftKeys(const char *a, const char *b, const char *c, const char *d) {
  const char *labels[4] = {a, b, c, d};
  for (int i = 0; i < 4; i++) {
    if (!labels[i] || !labels[i][0]) continue;
    int x = KEY_X + (i % 2) * KEY_W, y = KEY_Y + (i / 2) * (KEY_H + 1);
    spr.fillRoundRect(x + 1, y, KEY_W - 2, KEY_H, 4, C_DARK);
    spr.setTextColor(i == 0 ? C_ACCENT : C_TEXT);
    spr.drawCentreString(fitText(labels[i], KEY_W - 6, 2), x + KEY_W / 2, y, 2);
  }
}

// onboard buttons at the right edge: previous page at the top, next page at the bottom
static void drawNavHints() {
  spr.fillTriangle(W - 8, 11, W - 2, 11, W - 5, 5, C_GREY);
  spr.fillTriangle(W - 8, H - 11, W - 2, H - 11, W - 5, H - 5, C_GREY);
}

// where you are among the shown pages, bottom left next to the button labels
static void drawPageDots() {
  Page order[NUM_PAGES];
  int n = pageOrder(order);
  for (int i = 0; i < n; i++) {
    if (order[i] == page) spr.fillCircle(10 + i * 9, H - 6, 3, C_ACCENT);
    else spr.drawCircle(10 + i * 9, H - 6, 2, C_GREY);
  }
}

static void drawBar(int x, int y, int w, int h, float frac, uint16_t colour) {
  spr.drawRect(x, y, w, h, C_GREY);
  spr.fillRect(x + 1, y + 1, (w - 2) * constrain(frac, 0.0f, 1.0f), h - 2, colour);
}

// draws a list of lines, scrolled with the page's up/down keys
static void drawList(String *lines, uint16_t *colours, int n, int x, int y, int w) {
  int visible = (BOTTOM - y) / 17;
  scroll = constrain(scroll, 0, max(0, n - visible));
  for (int i = 0; i < visible && i + scroll < n; i++) {
    spr.setTextColor(colours[i + scroll]);
    spr.drawString(fitText(lines[i + scroll], w, 2), x, y + i * 17, 2);
  }
  if (n > visible) {
    // scroll indicator
    int barH = (BOTTOM - y) * visible / n;
    int barY = y + (BOTTOM - y - barH) * scroll / max(1, n - visible);
    spr.fillRect(W - 3, barY, 2, barH, C_GREY);
  }
}

// ---------- pages ----------

static void pageHome() {
  struct tm t;
  bool haveTime = localNow(t);
  char clock[6] = "--:--", day[12] = "", month[12] = "";
  if (haveTime) {
    strftime(clock, sizeof(clock), "%H:%M", &t);
    strftime(day, sizeof(day), "%a %d", &t);
    strftime(month, sizeof(month), "%B", &t);
  }
  spr.setTextColor(C_TEXT);
  spr.drawString(clock, 8, TOP + 4, 7);

  // right column: date, weather, moon
  spr.setTextColor(C_ACCENT);
  spr.drawString(day, 186, TOP + 2, 4);
  spr.setTextColor(C_GREY);
  String line = month;
  if (app.hasWeather) line += "  " + String(app.weatherTemp, 0) + "C " + app.weatherText;
  spr.drawString(fitText(line, 128, 2), 186, TOP + 28, 2);
  spr.drawString(moonName(moonAge()), 186, TOP + 44, 2);

  int y = TOP + 56;
  if (app.feed.nDeadlines) {
    DatedItem &d = app.feed.deadlines[0];
    int days = daysUntil(d.date);
    spr.setTextColor(daysColour(days));
    spr.drawString(daysLabel(days), 8, y, 2);
    spr.setTextColor(C_TEXT);
    spr.drawString(fitText(d.title, 250, 2), 56, y, 2);
    y += 18;
  }

  // next calendar event that hasn't ended
  int now = minutesNow();
  for (int i = 0; i < app.nCalendar; i++) {
    if (parseMinutes(app.calendar[i].end) > now || now < 0) {
      spr.setTextColor(C_ACCENT);
      spr.drawString(app.calendar[i].start, 8, y, 2);
      spr.setTextColor(C_TEXT);
      spr.drawString(fitText(app.calendar[i].title, 250, 2), 56, y, 2);
      y += 18;
      break;
    }
  }

  // the message fills the left column, next to the button labels
  if (app.feed.message.length()) drawWrapped(app.feed.message, 8, y, KEY_X - 16, 2, max(1, (H - 12 - y) / 18), C_AMBER);

  const char *keys[4] = {"-", "-", "-", "-"};
  int nButtons;
  const ButtonConfig *buttons = homeButtons(nButtons);
  for (int i = 0; i < nButtons; i++) keys[i] = buttons[i].label.c_str();
  drawSoftKeys(keys[0], keys[1], keys[2], keys[3]);
}

static void pageLight() {
  int pc = potColour(), pb = potBrightness();
  CRGB base = ledsBaseColour(pc);

  spr.setTextColor(C_TEXT);
  spr.drawString(String(ledsModeName()) + (ledsLightOn() ? "" : "  (off)"), 10, TOP + 2, 4);

  // who is in charge of the light right now
  spr.setTextColor(C_AMBER);
  if (ledsOverrideActive()) spr.drawString("Atlas is controlling the light", 10, TOP + 30, 2);
  else if (ledsPresetActive() && presetIndex >= 0 && presetIndex < app.feed.nPresets)
    spr.drawString(fitText("Preset " + app.feed.presets[presetIndex].name + ", turn a pot to exit", 210, 2), 10, TOP + 30, 2);

  // swatch shows the colour at the chosen brightness
  CRGB shown = base;
  shown.nscale8_video(ledsLightOn() ? map(pb, 0, 4095, 0, 255) : 0);
  spr.fillRoundRect(230, TOP + 4, 80, 60, 6, rgb565(shown));
  spr.drawRoundRect(230, TOP + 4, 80, 60, 6, C_GREY);

  spr.setTextColor(C_GREY);
  spr.drawString(ledsMode() == LIGHT_WHITE ? "Warm - cool" : "Hue", 10, TOP + 48, 2);
  drawBar(10, TOP + 66, 200, 12, pc / 4095.0f, rgb565(base));
  spr.drawString("Brightness", 10, TOP + 82, 2);
  drawBar(10, TOP + 100, 200, 12, pb / 4095.0f, C_TEXT);

  drawSoftKeys("On/Off", "Mode", app.feed.nPresets ? "Preset" : "Sunrise", ledsOverrideActive() ? "Auto" : "");
}

static void pageFocus() {
  long left = max(0L, focusLeftMs()) / 1000;
  char buf[8];
  snprintf(buf, sizeof(buf), "%02ld:%02ld", left / 60, left % 60);

  spr.setTextColor(C_GREY);
  spr.drawString(fitText(app.feed.focus.length() ? app.feed.focus : "Focus", 200, 2), 10, TOP + 4, 2);
  spr.drawRightString(focusRunning ? "Running" : focusRemaining < focusMinutes * 60000L ? "Paused" : "Ready", W - 10, TOP + 4, 2);

  spr.setTextColor(focusRunning ? C_GREEN : C_TEXT);
  spr.drawCentreString(buf, W / 2, TOP + 32, 7);

  float done = 1.0f - (float)max(0L, focusLeftMs()) / (focusMinutes * 60000.0f);
  drawBar(10, TOP + 100, W - 20, 14, done, C_GREEN);
  drawSoftKeys(focusRunning ? "Pause" : "Start", "+5", "-5", "Reset");
}

static void pageTasks() {
  String lines[16];
  uint16_t colours[16];
  int n = 0;
  for (int i = 0; i < app.feed.nDeadlines && n < 16; i++) {
    int days = daysUntil(app.feed.deadlines[i].date);
    lines[n] = daysLabel(days) + "   " + app.feed.deadlines[i].title;
    colours[n++] = daysColour(days);
  }
  for (int i = 0; i < app.feed.nTodos && n < 16; i++) {
    lines[n] = "- " + app.feed.todos[i];
    colours[n++] = C_TEXT;
  }
  if (!n) { lines[0] = app.feed.valid ? "Nothing due, nice." : "Waiting for the feed..."; colours[0] = C_GREY; n = 1; }
  drawList(lines, colours, n, 10, TOP + 4, W - 20);
  drawSoftKeys("Up", "Down", "", "");
}

static void pageHabits() {
  const char *keys[4] = {"", "", "", ""};
  String names[4];
  for (int i = 0; i < habitCount(); i++) {
    names[i] = habitName(i);
    keys[i] = names[i].c_str();
    String lastDate;
    int streak;
    habitRead(i, lastDate, streak);
    bool done = lastDate == todayString() && lastDate.length();

    int y = TOP + 4 + i * 28;  // four rows end above the button labels
    if (done) spr.fillCircle(20, y + 10, 9, C_GREEN);
    else spr.drawCircle(20, y + 10, 9, C_GREY);
    spr.setTextColor(done ? C_TEXT : C_GREY);
    spr.drawString(fitText(names[i], 180, 4), 40, y, 4);
    spr.setTextColor(streak ? C_AMBER : C_GREY);
    spr.drawRightString(streak ? String(streak) + " day streak" : "no streak", W - 10, y + 4, 2);
  }
  drawSoftKeys(keys[0], keys[1], keys[2], keys[3]);
}

static void pageCalendar() {
  String lines[10];
  uint16_t colours[10];
  int now = minutesNow();
  int n = 0;
  for (int i = 0; i < app.nCalendar; i++) {
    CalEvent &c = app.calendar[i];
    lines[n] = c.start + (c.end.length() ? "-" + c.end : "") + "  " + c.title;
    int s = parseMinutes(c.start), e = parseMinutes(c.end);
    colours[n++] = (now >= s && now < e) ? C_GREEN : (e >= 0 && now >= e) ? C_GREY : C_TEXT;
  }
  if (!n) { lines[0] = "Nothing on the calendar today"; colours[0] = C_GREY; n = 1; }
  drawList(lines, colours, n, 10, TOP + 4, W - 20);
  drawSoftKeys("Up", "Down", "", "");
}

static void pageSpace() {
  double age = moonAge();
  drawMoon(50, TOP + 44, 36);
  spr.setTextColor(C_TEXT);
  spr.drawCentreString(moonName(age), 50, TOP + 86, 2);
  spr.setTextColor(C_GREY);
  spr.drawCentreString(String((int)((1 - cos(2 * PI * age / 29.530588853)) * 50)) + "% lit", 50, TOP + 104, 2);

  String lines[16];
  uint16_t colours[16];
  int n = 0;
  if (app.nIss) {
    lines[n] = "ISS " + app.iss[0].date.substring(5) + "  " + app.iss[0].title;
    colours[n++] = C_ACCENT;
  }
  for (int i = 0; i < app.feed.nCountdowns && n < 16; i++) {
    int days = daysUntil(app.feed.countdowns[i].date);
    lines[n] = daysLabel(days) + "  " + app.feed.countdowns[i].title;
    colours[n++] = C_AMBER;
  }
  for (int i = 0; i < app.nSpaceEvents && n < 16; i++) {
    lines[n] = app.spaceEvents[i].date.substring(5) + "  " + app.spaceEvents[i].title;
    colours[n++] = C_TEXT;
  }
  if (!n) { lines[0] = "Waiting for space data..."; colours[0] = C_GREY; n = 1; }
  drawList(lines, colours, n, 104, TOP + 4, W - 110);
  drawSoftKeys("Up", "Down", "", "");
}

static void pageStats() {
  Stats &s = app.stats;
  spr.setTextColor(C_ACCENT);
  spr.drawString("BenchPi", 10, TOP + 2, 2);
  spr.drawString("Pi-hole", 170, TOP + 2, 2);
  spr.setTextColor(C_TEXT);
  if (s.valid) {
    spr.drawString("Temp  " + String(s.piTemp, 1) + " C", 10, TOP + 22, 2);
    spr.drawString("Load  " + String(s.piLoad, 2), 10, TOP + 40, 2);
    spr.drawString("RAM   " + String(s.piMem) + "%", 10, TOP + 58, 2);
    spr.drawString("Up    " + s.piUptime, 10, TOP + 76, 2);
    spr.drawString(s.piholeStatus.length() ? s.piholeStatus : "?", 170, TOP + 22, 2);
    spr.drawString("Queries " + String(s.queries), 170, TOP + 40, 2);
    spr.drawString("Blocked " + String(s.blocked), 170, TOP + 58, 2);
    spr.drawString(String(s.blockedPct, 1) + "% blocked", 170, TOP + 76, 2);
  } else {
    spr.setTextColor(C_GREY);
    spr.drawString("No stats yet", 10, TOP + 22, 2);
  }

  spr.setTextColor(C_GREY);
  String net = "WiFi " + String(netWifiOk() ? String(WiFi.RSSI()) + " dBm" : "off") + "   MQTT " + (netMqttOk() ? "ok" : "down");
  spr.drawString(net, 10, TOP + 94, 2);
  String info = "v" FW_VERSION "   heap " + String(ESP.getFreeHeap() / 1024) + " KB";
  if (s.valid) info += "   " + String((millis() - s.receivedAt) / 1000) + "s ago";
  spr.drawString(fitText(info, KEY_X - 16, 2), 10, TOP + 112, 2);  // left of the button labels
  drawSoftKeys("", "", "", "");
}

static void pageGallery() {
  if (galleryPhoto && hasPhoto()) {
    photoSpr.pushToSprite(&spr, 0, 0);
    if (app.photoTitle.length()) {
      spr.fillRect(0, BOTTOM - 18, W, 18, C_DARK);
      spr.setTextColor(C_TEXT);
      spr.drawString(fitText(app.photoTitle, W - 12, 2), 6, BOTTOM - 17, 2);
    }
  } else if (app.art.valid) {
    drawPixelArt(TOP, BOTTOM - 18);
    spr.setTextColor(C_GREY);
    spr.drawCentreString(app.art.title, W / 2, BOTTOM - 17, 2);
  } else {
    spr.setTextColor(C_GREY);
    spr.drawCentreString("No image yet", W / 2, 70, 2);
  }
  drawSoftKeys(galleryPhoto ? "Pixel art" : "Photo", "", "", "");
}

// ---------- screensaver ----------

enum SaverCard { S_CLOCK, S_QUOTE, S_FORTUNE, S_FACT, S_MESSAGE, S_PIXELART, S_PHOTO, S_COUNTDOWN, S_MOON, S_STARS, S_PLASMA, NUM_SAVER };

static SaverCard saverCard = S_CLOCK;
static unsigned long saverStart = 0;
static String saverText, saverSub;

static bool saverAvailable(SaverCard c) {
  switch (c) {
    case S_MESSAGE: return app.feed.message.length() > 0;
    case S_PIXELART: return app.art.valid;
    case S_PHOTO: return hasPhoto();
    case S_COUNTDOWN: return app.feed.nCountdowns > 0;
    default: return true;
  }
}

static bool saverIsAnimation(SaverCard c) { return c == S_STARS || c == S_PLASMA; }

static void saverNext() {
  SaverCard next;
  do next = (SaverCard)random(NUM_SAVER);
  while (next == saverCard || !saverAvailable(next));
  saverCard = next;
  saverStart = millis();

  // pick the text now so it doesn't change every frame
  if (saverCard == S_QUOTE) {
    if (app.feed.quote.length() && random(2)) { saverText = app.feed.quote; saverSub = app.feed.quoteAuthor; }
    else { int i = random(NUM_BUILTIN_QUOTES); saverText = BUILTIN_QUOTES[i][0]; saverSub = BUILTIN_QUOTES[i][1]; }
  } else if (saverCard == S_FORTUNE) {
    int total = app.feed.nFortunes + NUM_BUILTIN_FORTUNES, i = random(total);
    saverText = i < app.feed.nFortunes ? app.feed.fortunes[i] : BUILTIN_FORTUNES[i - app.feed.nFortunes];
  } else if (saverCard == S_FACT) {
    int total = app.feed.nFacts + NUM_BUILTIN_FACTS, i = random(total);
    saverText = i < app.feed.nFacts ? app.feed.facts[i] : BUILTIN_FACTS[i - app.feed.nFacts];
  } else if (saverCard == S_COUNTDOWN) {
    DatedItem &c = app.feed.countdowns[random(app.feed.nCountdowns)];
    saverText = c.title;
    saverSub = daysLabel(daysUntil(c.date));
  }
}

// starfield
static const int NUM_STARS = 90;
static float starX[NUM_STARS], starY[NUM_STARS], starZ[NUM_STARS];

static void drawStars() {
  for (int i = 0; i < NUM_STARS; i++) {
    starZ[i] -= 0.02f;
    if (starZ[i] <= 0.05f) { starX[i] = random(-1000, 1000) / 1000.0f; starY[i] = random(-1000, 1000) / 1000.0f; starZ[i] = 1; }
    int x = W / 2 + starX[i] / starZ[i] * 160, y = H / 2 + starY[i] / starZ[i] * 85;
    if (x < 0 || x >= W || y < 0 || y >= H) { starZ[i] = 0; continue; }
    uint8_t b = 255 * (1 - starZ[i]);
    int size = starZ[i] < 0.3f ? 2 : 1;
    spr.fillRect(x, y, size, size, spr.color565(b, b, b));
  }
}

static void drawPlasma() {
  uint8_t t = millis() / 20;
  for (int by = 0; by < H / 5 + 1; by++)
    for (int bx = 0; bx < W / 5; bx++) {
      uint8_t v = sin8(bx * 6 + t) / 3 + sin8(by * 9 - t) / 3 + sin8((bx + by) * 4 + t * 2) / 3;
      CRGB c = CHSV(v + t, 230, 160);
      spr.fillRect(bx * 5, by * 5, 5, 5, rgb565(c));
    }
}

static void drawCentredText(const String &text, const String &sub, uint16_t colour) {
  // bigger font for short texts
  uint8_t font = text.length() < 60 ? 4 : 2;
  int lineH = spr.fontHeight(font) + 2;
  int lines = min(5, (int)(spr.textWidth(text, font) / (W - 40)) + 1);
  int y = (H - lines * lineH - (sub.length() ? 24 : 0)) / 2;
  y = drawWrapped(text, 20, y, W - 40, font, 5, colour);
  if (sub.length()) {
    spr.setTextColor(C_GREY);
    spr.drawRightString("- " + sub, W - 20, y + 6, 2);
  }
}

static void drawSaver() {
  unsigned long length = saverIsAnimation(saverCard) ? SAVER_ANIM_MS : SAVER_CARD_MS;
  if (millis() - saverStart > length || !saverAvailable(saverCard)) saverNext();

  switch (saverCard) {
    case S_CLOCK: {
      struct tm t;
      char clock[6] = "--:--", date[24] = "";
      if (localNow(t)) { strftime(clock, sizeof(clock), "%H:%M", &t); strftime(date, sizeof(date), "%A %d %B", &t); }
      spr.setTextColor(C_TEXT);
      spr.drawCentreString(clock, W / 2, 40, 7);
      spr.setTextColor(C_GREY);
      spr.drawCentreString(date, W / 2, 100, 4);
      break;
    }
    case S_QUOTE: drawCentredText("\"" + saverText + "\"", saverSub, C_TEXT); break;
    case S_FORTUNE: {
      spr.setTextColor(C_AMBER);
      spr.drawCentreString("Fortune cookie", W / 2, 8, 2);
      drawCentredText(saverText, "", C_TEXT);
      break;
    }
    case S_FACT: {
      spr.setTextColor(C_ACCENT);
      spr.drawCentreString("Did you know?", W / 2, 8, 2);
      drawCentredText(saverText, "", C_TEXT);
      break;
    }
    case S_MESSAGE: drawCentredText(app.feed.message, "Atlas", C_AMBER); break;
    case S_PIXELART: drawPixelArt(0, H); break;
    case S_PHOTO: photoSpr.pushToSprite(&spr, 0, 0); break;
    case S_COUNTDOWN: {
      spr.setTextColor(C_AMBER);
      spr.drawCentreString(saverSub, W / 2, 40, 7);
      spr.setTextColor(C_TEXT);
      spr.drawCentreString(fitText(saverText, W - 20, 4), W / 2, 110, 4);
      break;
    }
    case S_MOON: {
      double age = moonAge();
      drawMoon(W / 2, 70, 55);
      spr.setTextColor(C_GREY);
      spr.drawCentreString(moonName(age), W / 2, 140, 2);
      break;
    }
    case S_STARS: drawStars(); break;
    case S_PLASMA: drawPlasma(); break;
    default: break;
  }
}

// ---------- banner ----------

void uiBanner(const String &title, const String &text, CRGB colour, int secs, bool sticky) {
  banner.active = true;
  banner.sticky = sticky;
  banner.title = title;
  banner.text = text;
  banner.colour = rgb565(colour);
  banner.until = millis() + (unsigned long)max(secs, 1) * 1000;
  // alerts wake the screen, but not at night
  if (!uiIsNight()) lastActivity = millis();
}

static void drawBanner() {
  if (!banner.sticky && millis() > banner.until) banner.active = false;
  if (!banner.active) return;
  spr.fillRoundRect(12, 34, W - 24, 100, 8, C_DARK);
  spr.drawRoundRect(12, 34, W - 24, 100, 8, banner.colour);
  spr.fillRoundRect(12, 34, W - 24, 6, 3, banner.colour);
  spr.setTextColor(banner.colour);
  spr.drawString(fitText(banner.title, W - 48, 4), 24, 48, 4);
  drawWrapped(banner.text, 24, 80, W - 48, 2, 2, C_TEXT);
  if (banner.sticky) {
    spr.setTextColor(C_GREY);
    spr.drawRightString("any button", W - 22, 114, 2);
  }
}

// ---------- screen ----------

static void setBacklight(int level) {
  if (level == backlight) return;
  backlight = level;
  ledcWrite(LCD_BL_PIN, level);
}

static ScreenState screenState() {
  unsigned long idle = millis() - lastActivity;
  if (uiIsNight() && idle > NIGHT_IDLE_MS) return OFF;
  if (idle > IDLE_DIM_MS) return SAVER;
  return AWAKE;
}

void uiWake() { lastActivity = millis(); }

void uiShowPhoto() {
  page = P_GALLERY;
  galleryPhoto = true;
  if (!uiIsNight()) lastActivity = millis();
}

void uiBegin() {
  pinMode(LCD_POWER_PIN, OUTPUT);
  digitalWrite(LCD_POWER_PIN, HIGH);
  pinMode(LCD_BL_PIN, OUTPUT);  // tft.init writes to it before ledcAttach

  tft.init();
  tft.setRotation(1);  // landscape, 320x170
  tft.fillScreen(C_BG);
  ledcAttach(LCD_BL_PIN, 5000, 8);
  setBacklight(BACKLIGHT_FULL);

  if (!spr.createSprite(W, H) || !photoSpr.createSprite(W, H)) {
    Serial.println("sprite allocation failed, is psram enabled?");
    tft.drawString("No PSRAM for the screen buffer", 10, 70, 2);
  }
  TJpgDec.setCallback(jpgToSprite);

  prefs.begin("dashboard", false);
  for (int i = 0; i < NUM_STARS; i++) starZ[i] = 0;
  lastActivity = millis();
}

void uiRender() {
  decodePhotoIfNew();
  ScreenState state = screenState();

  if (state == OFF) {
    setBacklight(0);
    return;
  }
  setBacklight(state == AWAKE ? BACKLIGHT_FULL : BACKLIGHT_DIM);

  spr.fillSprite(C_BG);
  if (!pageShown(page)) page = P_HOME;  // config.md hid the current page
  if (state == SAVER) {
    drawSaver();
  } else {
    switch (page) {
      case P_HOME: pageHome(); break;
      case P_LIGHT: pageLight(); break;
      case P_FOCUS: pageFocus(); break;
      case P_TASKS: pageTasks(); break;
      case P_HABITS: pageHabits(); break;
      case P_CALENDAR: pageCalendar(); break;
      case P_SPACE: pageSpace(); break;
      case P_STATS: pageStats(); break;
      case P_GALLERY: pageGallery(); break;
      default: break;
    }
    if (!(page == P_GALLERY && galleryPhoto)) drawTopBar();
    drawNavHints();
    drawPageDots();
  }
  drawBanner();
  spr.pushSprite(0, 0);
}

// ---------- input ----------

// light presets from config.md, button c on the light page cycles through them
static bool applyPreset(const String &name) {
  for (int i = 0; i < app.feed.nPresets; i++) {
    LightPreset &p = app.feed.presets[i];
    if (p.name != name) continue;
    presetIndex = i;
    ledsSetPreset(p.colour, p.brightness);
    uiBanner("Light: " + p.name, "Turn a pot to take over again", p.colour, 2, false);
    return true;
  }
  return false;
}

// home buttons from config.md: shortcut, timer, habit or light
static void runButton(const ButtonConfig &b, int index) {
  if (b.action == "shortcut") {
    JsonDocument doc;
    doc["id"] = index;
    doc["name"] = b.arg;
    String out;
    serializeJson(doc, out);
    netPublish("shortcut", out);
    uiBanner("Sent", b.label, CRGB(80, 160, 255), 2, false);
  } else if (b.action == "timer") {
    if (focusRunning || focusRemaining < focusMinutes * 60000L) {
      uiBanner("Focus in progress", "Finish or reset it on the Focus page", CRGB::Orange, 3, false);
      return;
    }
    focusMinutes = constrain((int)b.arg.toInt(), 1, 180);
    focusRemaining = focusMinutes * 60000L;
    focusEndsAt = millis() + focusRemaining;
    focusRunning = true;
    uiBanner("Focus started", String(focusMinutes) + " min", CRGB(0, 200, 80), 2, false);
  } else if (b.action == "habit") {
    for (int h = 0; h < habitCount(); h++)
      if (habitName(h) == b.arg) { habitLog(h); return; }
    uiBanner(b.label, "Unknown habit " + b.arg, CRGB::Orange, 3, false);
  } else if (b.action == "light") {
    if (!applyPreset(b.arg)) uiBanner(b.label, "Unknown preset " + b.arg, CRGB::Orange, 3, false);
  }
}

static void pageAction(Button b, PressType type) {
  int i = b;  // 0..3 for A..D
  switch (page) {
    case P_HOME:
      {
        int nButtons;
        const ButtonConfig *buttons = homeButtons(nButtons);
        if (i < nButtons) runButton(buttons[i], i);
      }
      break;
    case P_LIGHT:
      if (b == B_A) ledsToggleLight();
      if (b == B_B) ledsNextMode();
      if (b == B_C) {
        if (app.feed.nPresets) applyPreset(app.feed.presets[(presetIndex + 1) % app.feed.nPresets].name);
        else ledsPlay("sunrise", CRGB::White, 30);
      }
      if (b == B_D) ledsSetOverride("auto", CRGB::White, 0);
      break;
    case P_FOCUS:
      if (b == B_A) {
        if (focusRunning) { focusRemaining = focusLeftMs(); focusRunning = false; }
        else { focusEndsAt = millis() + focusRemaining; focusRunning = true; }
      }
      if ((b == B_B || b == B_C) && !focusRunning) {
        focusMinutes = constrain(focusMinutes + (b == B_B ? FOCUS_STEP_MIN : -FOCUS_STEP_MIN), FOCUS_STEP_MIN, 180);
        focusRemaining = focusMinutes * 60000L;
      }
      if (b == B_D) { focusRunning = false; focusRemaining = focusMinutes * 60000L; }
      break;
    case P_HABITS:
      if (i < habitCount()) habitLog(i);
      break;
    case P_TASKS:
    case P_CALENDAR:
    case P_SPACE:
      if (b == B_A) scroll--;
      if (b == B_B) scroll++;
      break;
    case P_GALLERY:
      if (b == B_A) galleryPhoto = !galleryPhoto;
      break;
    default:
      break;
  }
}

void uiHandleInput(const InputEvent &ev) {
  bool wasAwake = screenState() == AWAKE;
  lastActivity = millis();

  // first press only dismisses a banner or wakes the screen
  if (banner.active) { banner.active = false; return; }
  if (!wasAwake) return;

  if (ev.button == B_NEXT) {
    page = ev.type == PRESS_LONG ? P_HOME : stepPage(1);
    scroll = 0;
  } else if (ev.button == B_PREV) {
    if (ev.type == PRESS_LONG) ledsToggleLight();   // quick light switch from any page
    else { page = stepPage(-1); scroll = 0; }
  } else if (ev.type == PRESS_SHORT) {
    pageAction(ev.button, ev.type);
  }
}
