## MODIFIED Requirements

### Requirement: Touch slider handling precision
Manual touch interactions SHALL operate on the same hundredth-precision model as remote updates: finger samples map through the continuous track-to-temperature function without rounding to tenths, clamps enforce bounds/gaps using floats, and view-model caches retain the precise values. Only the rendered labels round to tenths. Inactive setpoints SHALL render using a desaturated color (50% of active saturation) at 40% opacity for both labels and tracks.

#### Scenario: User drag lands between tenths
- **WHEN** a user drags the cooling slider and releases where the math resolves to 23.37 °C
- **THEN** `g_view_model.cooling_setpoint_c` stores 23.37 °C, the track geometry holds that value, and the label renders 23.4 °C (rounded) without snapping the underlying state.

#### Scenario: Inactive setpoint styling
- **GIVEN** the cooling setpoint is active
- **WHEN** the UI renders the heating setpoint
- **THEN** the heating label and track use the heating color desaturated to 50% of its original saturation
- **AND** both the label and track render at 40% opacity
- **AND** when heating becomes active, it returns to full saturation at 100% opacity.

## REMOVED Requirements

### Requirement: Setpoint tick overlay
**Reason**: Visual clutter without sufficient benefit; simplifying the UI.
**Migration**: No migration needed; tick overlay was purely visual.
