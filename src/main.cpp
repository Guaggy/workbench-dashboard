#include <Arduino.h>
#include "config.h"
#include "state.h"
#include "inputs.h"
#include "leds.h"
#include "net.h"
#include "ui.h"

AppState app;

// fires the timed events atlas planned for today (from dashboard.md via the pi)
static void checkTimedEvents() {
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < 1000) return;
  lastCheck = millis();

  struct tm t;
  if (!app.nEvents || !netTimeOk() || !getLocalTime(&t, 0)) return;
  if (app.eventsDate != todayString()) return;
  int now = t.tm_hour * 60 + t.tm_min;

  for (int i = 0; i < app.nEvents; i++) {
    TimedEvent &e = app.events[i];
    if (e.fired || e.minute > now) continue;
    e.fired = true;
    ledsPlay(e.anim, e.colour, e.secs);
    if (e.text.length()) uiBanner("Atlas", e.text, e.colour, max(e.secs, 10), false);
  }
}

void setup() {
  Serial.begin(115200);
  uiBegin();
  inputsBegin();
  ledsBegin();
  netBegin();
}

void loop() {
  InputEvent ev;
  if (inputsPoll(ev)) uiHandleInput(ev);
  if (potsMoved()) {
    uiWake();
    ledsClearPreset();  // turning a pot takes the light back from a preset
  }

  netLoop();
  focusTick();
  checkTimedEvents();

  static bool deadlineSoon = false;
  static unsigned long lastDeadlineCheck = 0;
  if (millis() - lastDeadlineCheck > 10000) {
    lastDeadlineCheck = millis();
    deadlineSoon = uiDeadlineSoon();
  }
  ledsUpdate(potColour(), potBrightness(), uiIsNight(), deadlineSoon);

  static unsigned long lastFrame = 0;
  if (millis() - lastFrame >= FRAME_MS) {
    lastFrame = millis();
    uiRender();
  }
}
