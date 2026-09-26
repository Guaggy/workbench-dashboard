#include "leds.h"
#include "config.h"

static CRGB leds[NUM_LEDS];

static bool lightOn = true;
static LightMode mode = LIGHT_WHITE;

static bool overrideActive = false;
static bool overrideOff = false;
static CRGB overrideColour;
static uint8_t overrideBrightness = 255;

static bool presetActive = false;
static CRGB presetColour;
static uint8_t presetBrightness = 255;

static String animName;
static CRGB animColour;
static unsigned long animStart = 0, animLength = 0;

static const CRGB WARM_WHITE = CRGB(255, 150, 60);
static const CRGB COOL_WHITE = CRGB(190, 215, 255);
static const CRGB AMBER = CRGB(255, 100, 0);

void ledsBegin() {
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_MILLIAMPS);
  FastLED.setBrightness(255);  // brightness is applied per layer below
  FastLED.clear(true);
}

CRGB ledsBaseColour(int potColour) {
  if (mode == LIGHT_COLOUR) return CHSV(map(potColour, 0, 4095, 0, 255), 255, 255);
  // white: pot goes from warm to cool
  return blend(WARM_WHITE, COOL_WHITE, map(potColour, 0, 4095, 0, 255));
}

static void drawLight(int potColour, int potBrightness) {
  uint8_t level = map(potBrightness, 0, 4095, 0, 255);

  if (overrideActive) {
    if (overrideOff) fill_solid(leds, NUM_LEDS, CRGB::Black);
    else {
      fill_solid(leds, NUM_LEDS, overrideColour);
      nscale8_video(leds, NUM_LEDS, overrideBrightness);
    }
    return;
  }

  if (presetActive) {
    fill_solid(leds, NUM_LEDS, presetColour);
    nscale8_video(leds, NUM_LEDS, presetBrightness);
    return;
  }

  if (!lightOn || level < 3) {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    return;
  }

  if (mode == LIGHT_RAINBOW) fill_rainbow(leds, NUM_LEDS, millis() / 30, 255 / NUM_LEDS);
  else fill_solid(leds, NUM_LEDS, ledsBaseColour(potColour));
  nscale8_video(leds, NUM_LEDS, level);
}

// returns false when the animation is over
static bool drawAnimation(uint8_t level) {
  unsigned long t = millis() - animStart;
  if (t > animLength) return false;
  float progress = (float)t / animLength;

  if (animName == "flash") {
    fill_solid(leds, NUM_LEDS, (t / 250) % 2 ? CRGB::Black : animColour);
  } else if (animName == "rainbow") {
    fill_rainbow(leds, NUM_LEDS, t / 5, 255 / NUM_LEDS);
  } else if (animName == "confetti") {
    fadeToBlackBy(leds, NUM_LEDS, 20);
    leds[random16(NUM_LEDS)] += CHSV(random8(), 200, level);
  } else if (animName == "chase") {
    fadeToBlackBy(leds, NUM_LEDS, 60);
    int pos = (t / 15) % NUM_LEDS;
    leds[pos] = animColour.scale8(level);
  } else if (animName == "sunrise") {
    // dark red -> orange -> warm white over the whole duration
    CRGB c = ColorFromPalette(HeatColors_p, progress * 240);
    fill_solid(leds, NUM_LEDS, c);
    nscale8_video(leds, NUM_LEDS, 40 + progress * 215);
  } else {
    // pulse, also the fallback for unknown names
    uint8_t wave = beatsin8(40, 30, 255, animStart);
    fill_solid(leds, NUM_LEDS, animColour);
    nscale8_video(leds, NUM_LEDS, wave);
  }

  // confetti and chase keep their own fading buffer, the rest get scaled here
  if (animName != "confetti" && animName != "chase") nscale8_video(leds, NUM_LEDS, level);
  return true;
}

void ledsUpdate(int potColour, int potBrightness, bool night, bool deadlineWarning) {
  static unsigned long lastShow = 0;
  if (millis() - lastShow < 16) return;  // ~60 fps is plenty
  lastShow = millis();

  if (animLength > 0) {
    if (drawAnimation(night ? NIGHT_ALERT_BRIGHTNESS : ALERT_BRIGHTNESS)) {
      FastLED.show();
      return;
    }
    animLength = 0;
  }

  drawLight(potColour, potBrightness);

  if (deadlineWarning && !night) {
    uint8_t glow = beatsin8(10, 20, 90);
    for (int i = NUM_LEDS - DEADLINE_WARN_LEDS; i < NUM_LEDS; i++) leds[i] = AMBER.scale8(glow);
  }
  FastLED.show();
}

void ledsToggleLight() { lightOn = !lightOn; }
void ledsNextMode() { mode = (LightMode)((mode + 1) % NUM_LIGHT_MODES); }
bool ledsLightOn() { return lightOn; }
LightMode ledsMode() { return mode; }

const char *ledsModeName() {
  switch (mode) {
    case LIGHT_WHITE: return "White";
    case LIGHT_COLOUR: return "Colour";
    default: return "Rainbow";
  }
}

void ledsSetOverride(const String &m, CRGB colour, int brightness) {
  if (m == "auto") {
    overrideActive = false;
    return;
  }
  overrideActive = true;
  overrideOff = (m == "off");
  overrideColour = colour;
  overrideBrightness = constrain(brightness, 0, 255);
}

bool ledsOverrideActive() { return overrideActive; }

void ledsSetPreset(CRGB colour, uint8_t brightness) {
  presetActive = true;
  presetColour = colour;
  presetBrightness = brightness;
}

void ledsClearPreset() { presetActive = false; }
bool ledsPresetActive() { return presetActive; }

void ledsPlay(const String &anim, CRGB colour, int secs) {
  animName = anim;
  animColour = colour;
  animStart = millis();
  animLength = (unsigned long)max(secs, 1) * 1000;
  if (anim == "confetti" || anim == "chase") fill_solid(leds, NUM_LEDS, CRGB::Black);
}

bool ledsAnimating() { return animLength > 0; }
