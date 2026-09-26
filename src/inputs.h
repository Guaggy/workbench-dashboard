#pragma once
#include <Arduino.h>

enum Button { B_A, B_B, B_C, B_D, B_PREV, B_NEXT, NUM_BTNS };

enum PressType { PRESS_NONE, PRESS_SHORT, PRESS_LONG };

struct InputEvent {
  Button button;
  PressType type;
};

void inputsBegin();

// call every loop, returns true and fills ev when a button was pressed
bool inputsPoll(InputEvent &ev);

// smoothed pot values 0..4095
int potColour();
int potBrightness();

// true once when a pot moved noticeably since the last call (counts as activity)
bool potsMoved();
