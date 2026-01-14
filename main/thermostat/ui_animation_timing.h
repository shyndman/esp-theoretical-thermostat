#pragma once

// Intro animation timings (milliseconds)
// 1) Splash status lines (appear in sequence)
#define THERMOSTAT_ANIM_SPLASH_LINE_SLIDE_MS 400  // Splash status line slide-in.
#define THERMOSTAT_ANIM_SPLASH_LINE_FADE_IN_MS 450  // Splash status line fade-in.
#define THERMOSTAT_ANIM_SPLASH_LINE_ENTER_MS \
  (THERMOSTAT_ANIM_SPLASH_LINE_SLIDE_MS + THERMOSTAT_ANIM_SPLASH_LINE_FADE_IN_MS)  // Splash line slide + fade-in.
#define THERMOSTAT_ANIM_SPLASH_FINAL_HOLD_MS 100  // Time waited after the final line settles before the line fade begins.
#define THERMOSTAT_ANIM_SPLASH_LINE_FADE_MS 350  // Splash line fade-out duration.
#define THERMOSTAT_ANIM_SPLASH_LINE_STAGGER_MS 30  // Time waited after each line starts fading before the next line starts.

// 2) Splash exit
#define THERMOSTAT_ANIM_SPLASH_FADE_OUT_MS 1200  // Splash screen fade-out duration.

// 3) Boot LED sequence (white crescendo + fade)
#define THERMOSTAT_ANIM_LED_WHITE_FADE_IN_MS 400  // Boot LED fade-in to white.
#define THERMOSTAT_ANIM_LED_WHITE_HOLD_MS 2000  // Time waited after white fade-in before LED fade-out.
#define THERMOSTAT_ANIM_LED_BLACK_FADE_OUT_MS 1000  // Boot LED fade-out to black.
#define THERMOSTAT_ANIM_LED_BLACK_FADE_COMPLETE_BUFFER_MS 100  // Time waited after LED fade-out before handoff.
#define THERMOSTAT_ANIM_LED_BLACK_FADE_COMPLETE_DELAY_MS \
  (THERMOSTAT_ANIM_LED_BLACK_FADE_OUT_MS + THERMOSTAT_ANIM_LED_BLACK_FADE_COMPLETE_BUFFER_MS)  // LED fade-out + buffer.

// 4) Entrance timing
#define THERMOSTAT_ANIM_ENTRANCE_OFFSET_MS 100  // Time between splash fade start and entrance animation start.
#define THERMOSTAT_ANIM_ENTRANCE_START_DELAY_MS \
  (THERMOSTAT_ANIM_SPLASH_FADE_OUT_MS - THERMOSTAT_ANIM_ENTRANCE_OFFSET_MS)  // Delay after splash fade start before entrance.

// 5) Top bar
#define THERMOSTAT_ANIM_TOP_BAR_STAGGER_MS 100  // Stagger between top bar element reveals.
#define THERMOSTAT_ANIM_TOP_BAR_FADE_MS 600  // Top bar element fade-in.
#define THERMOSTAT_ANIM_HVAC_PULSE_MS 1600  // HVAC status pulse cycle duration.

// 6) Setpoint tracks
#define THERMOSTAT_ANIM_TRACK_COOL_GROW_MS 450  // Cool track growth duration.
#define THERMOSTAT_ANIM_TRACK_HEAT_GROW_MS 300  // Heat track growth duration.
#define THERMOSTAT_ANIM_TRACK_HEAT_DELAY_MS 150  // Time waited after cool starts before heat begins.
#define THERMOSTAT_ANIM_TRACK_COOL_END_MS THERMOSTAT_ANIM_TRACK_COOL_GROW_MS  // Cool track end time.
#define THERMOSTAT_ANIM_TRACK_HEAT_END_MS \
  (THERMOSTAT_ANIM_TRACK_HEAT_DELAY_MS + THERMOSTAT_ANIM_TRACK_HEAT_GROW_MS)  // Heat track end time.
#define THERMOSTAT_ANIM_TRACK_END_MS \
  ((THERMOSTAT_ANIM_TRACK_COOL_END_MS > THERMOSTAT_ANIM_TRACK_HEAT_END_MS) \
       ? THERMOSTAT_ANIM_TRACK_COOL_END_MS                                 \
       : THERMOSTAT_ANIM_TRACK_HEAT_END_MS)  // Track animation end time.

// 7) Setpoint labels
#define THERMOSTAT_ANIM_LABEL_FADE_MS 250  // Setpoint label fade-in.
#define THERMOSTAT_ANIM_LABEL_FRACTION_DELAY_MS 100  // Time waited after label fade starts before fraction appears.

// 8) Action bar
#define THERMOSTAT_ANIM_ACTION_BAR_STAGGER_MS 100  // Stagger between action bar elements.
#define THERMOSTAT_ANIM_ACTION_BAR_FADE_MS 300  // Action bar fade-in.

// Interaction animation timings (milliseconds)
// 9) Interactions
#define THERMOSTAT_ANIM_SETPOINT_COLOR_MS 300  // Setpoint color tween.
