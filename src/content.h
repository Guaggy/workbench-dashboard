#pragma once

// built-in screensaver content, used until (and alongside) what the feed sends

static const char *BUILTIN_QUOTES[][2] = {
  {"Simplicity is prerequisite for reliability.", "Edsger W. Dijkstra"},
  {"The best way to predict the future is to invent it.", "Alan Kay"},
  {"Make it work, make it right, make it fast.", "Kent Beck"},
  {"Premature optimization is the root of all evil.", "Donald Knuth"},
  {"Talk is cheap. Show me the code.", "Linus Torvalds"},
  {"If debugging is the process of removing bugs, then programming must be the process of putting them in.", "Edsger W. Dijkstra"},
};

static const char *BUILTIN_FORTUNES[] = {
  "The bug you are looking for is in the file you are sure is fine.",
  "A clean workbench today saves a lost screw tomorrow.",
  "Measure twice, flash once.",
  "Your next build will work on the first try. Probably.",
  "Great things come to those who check the ground wire.",
  "The solder joint you skip today will haunt you in the demo.",
  "Today is a good day to finish something instead of starting something.",
};

static const char *BUILTIN_FACTS[] = {
  "Sunlight takes about 8 minutes and 20 seconds to reach Earth.",
  "The ISS circles Earth roughly every 92 minutes, about 16 times a day.",
  "A day on Venus is longer than its year.",
  "Some neutron stars spin more than 700 times per second.",
  "The Moon drifts about 3.8 cm further from Earth every year.",
  "This dashboard's ESP32-S3 has two cores running at 240 MHz.",
  "Olympus Mons on Mars is about two and a half times the height of Everest.",
  "Each WS2812B LED has its own tiny controller chip inside it.",
};

#define NUM_BUILTIN_QUOTES   (sizeof(BUILTIN_QUOTES) / sizeof(BUILTIN_QUOTES[0]))
#define NUM_BUILTIN_FORTUNES (sizeof(BUILTIN_FORTUNES) / sizeof(BUILTIN_FORTUNES[0]))
#define NUM_BUILTIN_FACTS    (sizeof(BUILTIN_FACTS) / sizeof(BUILTIN_FACTS[0]))
