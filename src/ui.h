#pragma once
#include <Arduino.h>
#include <FastLED.h>
#include "inputs.h"

void uiBegin();

// draws the current page, screensaver or nothing (screen off), call every FRAME_MS
void uiRender();

// handles a button press, the press only wakes the screen if it was dimmed
void uiHandleInput(const InputEvent &ev);

// any activity (pots, alerts) that should wake the screen
void uiWake();

// jumps to the gallery photo (a picture sent with bench/show)
void uiShowPhoto();

// banner on top of everything, sticky ones stay until a button is pressed
void uiBanner(const String &title, const String &text, CRGB colour, int secs, bool sticky);

bool uiIsNight();

// true when a deadline is within DEADLINE_WARN_HOURS
bool uiDeadlineSoon();

// focus timer, keeps running on every page
void focusTick();
