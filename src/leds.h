#pragma once
#include <FastLED.h>

enum LightMode { LIGHT_WHITE, LIGHT_COLOUR, LIGHT_RAINBOW, NUM_LIGHT_MODES };

void ledsBegin();

// call every loop with the smoothed pots (0..4095)
void ledsUpdate(int potColour, int potBrightness, bool night, bool deadlineWarning);

// bench light controls
void ledsToggleLight();
void ledsNextMode();
bool ledsLightOn();
LightMode ledsMode();
const char *ledsModeName();
CRGB ledsBaseColour(int potColour);

// atlas override: "off", "solid" or "auto" (auto hands control back to the pots)
void ledsSetOverride(const String &mode, CRGB colour, int brightness);
bool ledsOverrideActive();

// light preset from config.md, cleared as soon as a pot moves
void ledsSetPreset(CRGB colour, uint8_t brightness);
void ledsClearPreset();
bool ledsPresetActive();

// one-shot animation on top of the light: pulse, flash, rainbow, confetti, chase, sunrise
void ledsPlay(const String &anim, CRGB colour, int secs);
bool ledsAnimating();
