// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2024 SimeonOnSecurity <https://github.com/simeononsecurity>
//
// led_neopixel.h — NeoPixel/SK6812 status LED hardware primitive
// (M5Atom Lite/Voice, plain ESP32 DevKit w/ DEVKIT_LED)
//
// WHY THIS FILE EXISTS (de-monolithic refactor, mirrors flock-you-esp32):
// Previously the NeoPixel strip object, its pin/count configuration, and the
// setLED() hardware primitive all lived directly inside main.cpp, mixed in
// with app-level scoring/phase logic. Isolating the hardware primitive here
// means a bug or change in *how* the LED is physically driven can never be
// confused with a bug in *when*/*what color* main.cpp decides to show. That
// decision logic (updateLED()'s score→color mapping, the startup pulse, the
// alert flash cadence) stays in main.cpp — exactly like flock-you-esp32
// keeps ledFlash()/ledTick() in main.cpp while only the ledSet() hardware
// primitive lives in its led_neopixel.h.
//
// Public interface expected by main.cpp:
//   ledBegin(brightness)  — one-time strip init, called once from setup()
//   setLED(r, g, b)       — set the LED to an arbitrary RGB color
//
// Both are safe to call unconditionally on every board: on boards with
// USE_LED=0 (Atom Echo, T-Dongle C5, M5Basic/Core2, M5StickC Plus SE — all
// of which use a display or buzzer instead of a NeoPixel) they compile to
// no-ops, so main.cpp never needs `#if USE_LED` guards at any call site.

#pragma once

#include <cstdint>

#if USE_LED

#include <Adafruit_NeoPixel.h>


#ifdef DEVKIT_LED
  #define LED_PIN   2   // plain ESP32 DevKit test rig w/ external NeoPixel
#else
  #define LED_PIN   27  // M5Atom Lite/Voice onboard SK6812
#endif
#define LED_COUNT   1

static Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

static inline void ledBegin(uint8_t brightness = 80) {
  strip.begin();
  strip.setBrightness(brightness);
}

static inline void setLED(uint8_t r, uint8_t g, uint8_t b) {
  strip.setPixelColor(0, strip.Color(r, g, b));
  strip.show();
}

#else  // !USE_LED — no-op stubs so main.cpp never needs #if USE_LED guards

static inline void ledBegin(uint8_t brightness = 80) { (void)brightness; }
static inline void setLED(uint8_t, uint8_t, uint8_t) {}

#endif // USE_LED
