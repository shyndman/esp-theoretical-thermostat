# Change: Remote remote-setpoint animation sequencing

## Why
Remote thermostat setpoint updates currently snap sliders to the final position the moment MQTT packets land. When the display is asleep this looks abrupt: the backlight wakes, the sliders teleport, and the panel immediately schedules sleep again. We need an explicit wake → delay → animation → hold → sleep choreography so remote activity feels intentional and gives users time to notice the change.

## What Changes
- Define a `thermostat-ui-interactions` capability that governs how the UI responds when inputs arrive outside of touch (starting with remote setpoint updates).
- Specify that remote setpoint updates must wait for the backlight to be fully lit, pause for one second, animate affected bars using an ease-in-out curve over 1.6s, then hold for another second before darkening the display if this flow initiated the wake.
- Document how the MQTT dataplane, setpoint view, and backlight manager collaborate to honor this sequence so future contributors do not bypass it.

## Impact
- **Specs:** Adds the new `thermostat-ui-interactions` capability.
- **Code:** Touches remote MQTT dataplane delivery, `thermostat_apply_remote_setpoint` sequencing, and backlight manager helpers.
