# thermostat-ui-interactions Delta (increase-setpoint-precision)

## MODIFIED Requirements
### Requirement: Remote setpoint animation pacing
Remote animations SHALL continue using the 1600 ms ease-in-out profile, but the controller MUST maintain 0.01 °C precision for every intermediate sample, only rounding when rendering label text.

#### Scenario: Paired setpoints change remotely
- **WHEN** a session begins animating cooling from 24.00 °C to 25.55 °C and heating from 21.05 °C to 22.15 °C
- **THEN** both tracks move over 1600 ms using ease-in-out easing
- **AND** the animation samples every 0.01 °C (LVGL keys use ×100 integers) so track motion appears continuous despite the 0.1 °C label rounding
- **AND** label text continues to update per frame but rounds to tenths purely for display, leaving the underlying view-model values at hundredth precision throughout the animation.

#### Scenario: Intermediate frames between setpoints
- **GIVEN** a remote session drives cooling from 20.00 °C to 21.00 °C
- **WHEN** halfway through the animation the interpolated temperature is 20.47 °C
- **THEN** the slider geometry reflects 20.47 °C (track Y, label positions)
- **AND** the numeric label shows 20.5 °C because display text rounds to tenths while the internal value stays at 20.47 °C.

## ADDED Requirements
### Requirement: Touch slider handling precision
Manual touch interactions SHALL operate on the same hundredth-precision model as remote updates: finger samples map through the continuous track-to-temperature function without rounding to tenths, clamps enforce bounds/gaps using floats, and view-model caches retain the precise values. Only the rendered labels round to tenths.

#### Scenario: User drag lands between tenths
- **WHEN** a user drags the cooling slider and releases where the math resolves to 23.37 °C
- **THEN** `g_view_model.cooling_setpoint_c` stores 23.37 °C, the track geometry holds that value, and the label renders 23.4 °C (rounded) without snapping the underlying state.
