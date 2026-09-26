#pragma once
#include <Arduino.h>
#include <FastLED.h>

void netBegin();
void netLoop();

bool netWifiOk();
bool netMqttOk();
bool netTimeOk();

// publishes to TOPIC_PREFIX + topic, queued while offline
void netPublish(const String &topic, const String &payload);

// helpers shared with the rest of the code
CRGB parseColour(const String &hex, CRGB fallback = CRGB::White);
int parseMinutes(const String &hhmm);   // "HH:MM" -> minutes after midnight, -1 if invalid
String todayString();                   // "YYYY-MM-DD"
