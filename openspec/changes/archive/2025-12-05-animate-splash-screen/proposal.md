# Change: Add animated splash history stack

## Why
Boot currently shows a single centered label that updates text in place, which looks abrupt and offers no context on previous stages. Scott asked for a more polished experience that retains recent status lines, animates transitions, and tweaks the label sizing/position so the screen feels intentional. Without a spec, downstream agents would not know how many lines to keep, how long the animations last, or how to queue overlapping updates, so we are codifying those details here.

## What Changes
- Replace the lone label with an 8-line column (current + 7 previous) rendered via an LVGL flex container that is centered, lifted by 50 px, and padded 20 px on the left. The container keeps a fixed height so we never rely on scrolling.
- Ship a new 40 pt Figtree Medium font blob (six points larger than `Figtree_Medium_34`) and apply it to all splash rows.
- Add a history buffer plus a pending queue (capacity ≥4) so `thermostat_splash_set_status()` becomes push-only: incoming statuses enqueue, and the splash drains them sequentially.
- Define the animation: every dequeue triggers a 250 ± 25 ms upward slide (style translate) for all visible rows, the outgoing current fades from 100% → 65% opacity over 150 ± 25 ms (ease-out) exactly once, and after the slide completes the new text fades in at the head over 150 ± 25 ms while the rest of the stack stays put. New updates must wait for the active animation to finish before starting the next.

## Impact
- Affected specs: `thermostat-boot-experience`
- Affected code: `main/thermostat/ui_splash.c`, `thermostat/ui_splash.h`, `assets/fonts/fontgen.toml`, generated blobs under `main/assets/fonts/`, and `thermostat_fonts.h`.
- New behaviors to validate: queue ordering under rapid updates, translation/fade timing, and history persistence after 8+ boot stages.
