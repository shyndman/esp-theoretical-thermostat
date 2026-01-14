# Proposal: Tint Bias Lighting

## Summary
Modify bias lighting to be dimmer and tinted toward the active setpoint color (blue for cooling, orange for heating), making it feel more intentional and reducing visibility of case imperfections.

## Motivation
The current pure white bias lighting at 50% brightness:
1. Is bright enough to highlight flaws in the case finish
2. Feels disconnected from the thermostat's heating/cooling context

## Changes
- Reduce brightness from 50% to 30%
- Blend white 30% toward the active setpoint's UI color:
  - Cooling active: `#BED6F0` (white tinted toward `#2776cc`)
  - Heating active: `#F6D6C0` (white tinted toward `#e1752e`)

## Affected Specs
- `thermostat-led-notifications`: Modifies "Bias lighting when screen is active" requirement
