#pragma once
#include <Arduino.h>
#include <FastLED.h>
#include "config.h"

// everything the pi sends lives here, filled by net.cpp and read by the ui

struct DatedItem {
  String date;   // "YYYY-MM-DD" (or "YYYY-MM-DD HH:MM" for iss passes)
  String title;
};

struct ButtonConfig { String label, action, arg; };
struct LightPreset { String name; CRGB colour; uint8_t brightness; };

struct Feed {
  bool valid = false;
  DatedItem deadlines[8]; int nDeadlines = 0;
  String todos[6];        int nTodos = 0;
  String habits[4];       int nHabits = 0;
  String pages[9];        int nPages = 0;     // empty = all pages in the built-in order
  ButtonConfig buttons[4]; int nButtons = 0;  // home buttons A-D
  LightPreset presets[6]; int nPresets = 0;
  DatedItem countdowns[6]; int nCountdowns = 0;
  String facts[12];       int nFacts = 0;
  String fortunes[12];    int nFortunes = 0;
  String focus;
  String message;
  String quote, quoteAuthor;
  int nightStart = NIGHT_START_DEFAULT;
  int nightEnd = NIGHT_END_DEFAULT;
};

struct CalEvent { String start, end, title; };

struct TimedEvent {
  int minute;          // minutes after midnight
  String anim;
  CRGB colour;
  int secs;
  String text;
  bool fired;
};

struct Stats {
  bool valid = false;
  unsigned long receivedAt = 0;
  float piTemp = 0, piLoad = 0;
  int piMem = 0;
  String piUptime;
  long queries = 0, blocked = 0;
  float blockedPct = 0;
  String piholeStatus;
};

struct PixelArt {
  bool valid = false;
  int w = 0, h = 0;
  String title;
  String rows[48];
  uint16_t palette[128];  // indexed by the row character
};

struct AppState {
  Feed feed;

  CalEvent calendar[10]; int nCalendar = 0;

  TimedEvent events[16]; int nEvents = 0;
  String eventsDate;

  Stats stats;

  DatedItem iss[4];        int nIss = 0;
  DatedItem spaceEvents[6]; int nSpaceEvents = 0;

  bool hasWeather = false;
  float weatherTemp = 0;
  String weatherText;

  PixelArt art;

  uint8_t *photo = nullptr;  // jpeg bytes in psram
  size_t photoLen = 0;
  String photoTitle;
  bool photoChanged = false;
};

extern AppState app;
