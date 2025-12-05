## MODIFIED Requirements
### Requirement: Required Topics Matrix
The firmware SHALL subscribe/publish to the following Home Assistant topics (default base topic `homeassistant`, configurable). Each inbound topic MUST drive the UI behavior indicated. Invalid payloads SHALL react exactly as described per-topic (status label error text or widget recolor) while leaving cached state untouched unless clamps are specified. The outbound `temperature_command` publish SHALL occur whenever the UI commits new setpoints.

1. `sensor/pirateweather_temperature/state` (QoS0 float string, °C) → update the weather temperature label without touching the backlight.
2. `sensor/pirateweather_icon/state` (QoS0 enum string such as `sunny`, `cloudy`, etc.) → map to the LVGL weather icon set; unknown values hide the icon and log a warning.
3. `sensor/theoretical_thermostat_target_room_temperature/state` (QoS0 float string, °C) → refresh the active room temperature readout; invalid strings set the label to `ERR`.
4. `sensor/theoretical_thermostat_target_room_name/state` (QoS0 string) → swap the displayed room glyph (`Living Room`, `Bedroom`, `Office`, `Hallway`, fallback default) and tint it red when unknown.
5. `climate/theoretical_thermostat_climate_control/target_temp_low` (QoS0 float string) → reposition the heating slider remotely, waking/animating the UI per the remote-setpoint flow when values change.
6. `climate/theoretical_thermostat_climate_control/target_temp_high` (QoS0 float string) → same as low track for the cooling slider.
7. `binary_sensor/theoretical_thermostat_computed_fan/state` (QoS0 `on`/`off`) → toggle the action-bar fan animation, coloring it red on unknown payloads.
8. `binary_sensor/theoretical_thermostat_computed_heat/state` (QoS0 `on`/`off`) → toggle the HVAC status label to `HEATING` plus orange LED cues; invalid payloads show `ERROR`.
9. `binary_sensor/theoretical_thermostat_computed_a_c/state` (QoS0 `on`/`off`) → toggle the HVAC status label to `COOLING` plus blue LED cues; invalid payloads show `ERROR`.
10. `climate/theoretical_thermostat_climate_control/temperature_command` (QoS1 JSON `{ "target_temp_high": float, "target_temp_low": float }`, retain=false) → published whenever the user commits a setpoint change; the device does not wait for echoes.

#### Scenario: Passive Weather Update
- **GIVEN** a payload arrives on `homeassistant/sensor/pirateweather_temperature/state`
- **WHEN** the JSON number parses successfully
- **THEN** the weather temperature label updates to the float value verbatim without waking the backlight.

#### Scenario: Remote Setpoint Update While Idle
- **GIVEN** the backlight is currently off
- **AND** quiet hours permit a wake
- **AND** a payload arrives on `homeassistant/climate/theoretical_thermostat_climate_control/target_temp_low` or `/target_temp_high`
- **WHEN** the number parses and differs from the cached setpoint
- **THEN** the device wakes the backlight, repositions/animates the affected slider, and schedules it to turn off 5 seconds after the animation if this flow initiated the wake.
