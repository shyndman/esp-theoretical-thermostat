# thermostat-connectivity Delta (increase-setpoint-precision)

## MODIFIED Requirements
### Requirement: Required Topics Summary
Remote setpoint topics SHALL accept and propagate hundredth-precision floats, only clamping to the allowed min/max range before passing values to the UI and mirroring them in outbound `temperature_command` messages.

#### Scenario: Remote Setpoint Update While Idle
- **WHEN** a payload arrives on the high/low target topics with any float at hundredth precision
- **THEN** the MQTT parser accepts the value without rounding, clamps only to the min/max range, and hands the precise float to the UI controller for animation as described in the updated UI spec
- **AND** outgoing `temperature_command` payloads MAY include hundredth-precision floats because the upstream controller accepts them.

### Requirement: Payload Validation Rules
Numeric parsing SHALL retain the incoming precision (e.g., hundredths) when clamping succeeds; the implementation MUST NOT quantize to tenths except when displaying human-readable labels.

#### Scenario: Setpoint payload carries hundredths
- **GIVEN** `target_temp_high` arrives as `24.37`
- **WHEN** it passes range validation
- **THEN** the MQTT dataplane forwards exactly 24.37 °C to the remote controller and eventually publishes the same value back inside `temperature_command` after manual adjustments, subject only to clamping and ordering rules.
