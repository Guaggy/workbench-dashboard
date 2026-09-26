#include "inputs.h"
#include "config.h"

static const int PINS[NUM_BTNS] = {BTN_A, BTN_B, BTN_C, BTN_D, BTN_PREV, BTN_NEXT};

static const unsigned long DEBOUNCE_MS = 30;
static const unsigned long LONG_MS = 700;
static const int POT_MOVE_THRESHOLD = 100;

struct ButtonState {
  bool down = false;
  bool longFired = false;
  unsigned long changedAt = 0;
};

static ButtonState btn[NUM_BTNS];
static float potC = 0, potB = 0;
static int lastC = -1000, lastB = -1000;

void inputsBegin() {
  for (int i = 0; i < NUM_BTNS; i++) pinMode(PINS[i], INPUT_PULLUP);
  potC = analogRead(POT_COLOUR_PIN);
  potB = analogRead(POT_BRIGHTNESS_PIN);
  lastC = potC;
  lastB = potB;
}

bool inputsPoll(InputEvent &ev) {
  // smooth the noisy adc a bit
  potC += (analogRead(POT_COLOUR_PIN) - potC) * 0.2f;
  potB += (analogRead(POT_BRIGHTNESS_PIN) - potB) * 0.2f;

  unsigned long now = millis();
  for (int i = 0; i < NUM_BTNS; i++) {
    bool down = digitalRead(PINS[i]) == LOW;
    ButtonState &b = btn[i];

    if (down != b.down && now - b.changedAt > DEBOUNCE_MS) {
      b.down = down;
      b.changedAt = now;
      if (!down && !b.longFired) {
        ev = {(Button)i, PRESS_SHORT};
        return true;
      }
      b.longFired = false;
    }

    if (b.down && !b.longFired && now - b.changedAt > LONG_MS) {
      b.longFired = true;
      ev = {(Button)i, PRESS_LONG};
      return true;
    }
  }
  return false;
}

int potColour() { return (int)potC; }
int potBrightness() { return (int)potB; }

bool potsMoved() {
  if (abs((int)potC - lastC) > POT_MOVE_THRESHOLD || abs((int)potB - lastB) > POT_MOVE_THRESHOLD) {
    lastC = potC;
    lastB = potB;
    return true;
  }
  return false;
}
