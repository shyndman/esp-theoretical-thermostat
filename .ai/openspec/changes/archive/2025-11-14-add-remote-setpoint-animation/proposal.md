# Change: Remote remote-setpoint animation sequencing

## Why
Remote thermostat setpoint updates currently snap sliders to the final position the moment MQTT packets land. When the display is asleep this looks abrupt: the backlight wakes, the sliders teleport, and the panel immediately schedules sleep again. We need an explicit wake → delay → animation → hold → sleep choreography so remote activity feels intentional and gives users time to notice the change.

## What Changes
- Define a `thermostat-ui-interactions` capability that governs how the UI responds when inputs arrive outside of touch (starting with remote setpoint updates).
- Specify that remote setpoint updates run as paired sessions: the first session waits for the backlight, pauses for one second, animates both sliders together over 1.6s, holds for one second, then darkens the display if the wake was consumed; subsequent sessions that arrive mid-burst coalesce (latest wins) and start animating immediately once the prior session completes.
- Document how the MQTT dataplane, setpoint view, and backlight manager collaborate to honor this sequence so future contributors do not bypass it.

## Impact
- **Specs:** Adds the new `thermostat-ui-interactions` capability.
- **Code:** Touches remote MQTT dataplane delivery, `thermostat_apply_remote_setpoint` sequencing, and backlight manager helpers.
