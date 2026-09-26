#pragma once

// ---------- pins ----------
#define LCD_POWER_PIN 15    // must be HIGH for the display when not on usb
#define LCD_BL_PIN    38

// external buttons in a 2x2 grid bottom right: A top-left, B top-right, C bottom-left, D bottom-right
// (mapped to the wiring 2026-09-26)
#define BTN_A     44
#define BTN_B     13
#define BTN_C     43
#define BTN_D     10
#define BTN_PREV  0         // onboard boot button, assumed top right (previous page)
#define BTN_NEXT  14        // onboard key button, assumed bottom right (next page)

#define POT_COLOUR_PIN     1
#define POT_BRIGHTNESS_PIN 2

// ---------- led strip ----------
#define LED_PIN        16
#define NUM_LEDS       60
#define MAX_MILLIAMPS  2000 // 3A supply minus ~0.4A for the board, with margin
#define ALERT_BRIGHTNESS       160
#define NIGHT_ALERT_BRIGHTNESS 40
#define DEADLINE_WARN_HOURS    48   // last leds glow amber when a deadline is this close
#define DEADLINE_WARN_LEDS     3

// ---------- screen ----------
#define BACKLIGHT_FULL   255
#define BACKLIGHT_DIM    40
#define IDLE_DIM_MS      (2UL * 60 * 1000)   // no input -> dim + screensaver
#define NIGHT_IDLE_MS    (30UL * 1000)       // at night -> screen off after this
#define SAVER_CARD_MS    12000               // time per screensaver card
#define SAVER_ANIM_MS    20000               // time per screensaver animation
#define FRAME_MS         50                  // ~20 fps
#define NIGHT_START_DEFAULT (23 * 60)        // minutes after midnight, overridden by the feed
#define NIGHT_END_DEFAULT   (7 * 60)

// ---------- focus timer ----------
#define FOCUS_DEFAULT_MIN 25
#define FOCUS_STEP_MIN    5

// ---------- time ----------
#define TIMEZONE   "AWST-8"                  // perth, no daylight saving
#define NTP_SERVER "pool.ntp.org"

// ---------- mqtt ----------
#define TOPIC_PREFIX     "bench/"
#define MQTT_BUFFER      49152               // must fit the jpeg photo (keep it under ~40 KB)
#define MQTT_RETRY_MS    5000
#define HEARTBEAT_MS     60000
#define FW_VERSION       "1.2.0"
