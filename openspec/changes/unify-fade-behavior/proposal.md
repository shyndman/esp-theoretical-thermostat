# Change Proposal: Unify Backlight Fade Behavior

The backlight manager currently uses a slow 1.6-second fade for waking up, but cuts immediately to black when sleeping (entering idle state or manual power-off). Additionally, transitions between day and night brightness are immediate if the new brightness is lower than the current one. 

This change unifies the backlight behavior to use a symmetric, snappier 500ms fade for all transitions: waking, sleeping, and day/night brightness shifts.

## User Review Required

> [!IMPORTANT]
> - All backlight transitions will now take 500ms.
> - Waking up will feel faster (down from 1.6s).
> - Sleeping will feel "softer" (up from 0s).

## Proposed Changes

### Core Backlight Logic

- **Unified Fading:** Update the internal fading engine to support both increasing and decreasing brightness deltas.
- **Duration Update:** Change the global fade duration from 1600ms to 500ms for both "on" and "off" transitions.
- **Fade-to-Off:** Modify the idle state transition to use the fading engine to reach 0% brightness before performing the final hardware shutdown.
- **Transition Smoothing:** Enable fading for day/night brightness adjustments, removing the previous restriction that only allowed fading "up."

### Hardware & Safety

- **Clean Shutdown:** Ensure `bsp_display_backlight_off()` is called upon completion of any fade that targets 0% brightness to ensure the hardware is fully powered down.
- **Interrupt Handling:** Touching the screen during a "fade-to-off" will immediately halt the downward fade and begin fading back up to the active target brightness from the current intermediate level.

## Verification Plan

### Automated Tests
- No automated tests available for backlight PWM transitions.

### Manual Verification
- **Idle Timeout:** Observe the backlight fading out over 500ms after the timeout period.
- **Touch Wake:** Verify the backlight fades in over 500ms when touched.
- **Rebound Effect:** Start a manual sleep (power button) and immediately touch the screen to verify it smoothly transitions back to full brightness.
- **Day/Night Shift:** (Simulated) Change system time to trigger a day/night shift and observe the smooth 500ms transition.

## Results
- Build successful.
- Unified 500ms fade verified on hardware for both wake and sleep.
- Symmetric fading (up/down) confirmed for all transitions, including manual sleep and day/night shifts.
- Rebound effect (interrupted fade-out) verified to be smooth.
- Quiet hours "off" transition suppression fixed and verified.
