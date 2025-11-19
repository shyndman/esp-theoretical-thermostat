# Proposal: Remove audio hardware access when audio is disabled

## Why
Field testing showed that setting `CONFIG_THEO_AUDIO_ENABLE = n` still brings up the ES8311 codec during boot. That surprises technicians and wastes power, and it violates the updated expectation that disabling application audio should behave as if no speaker path exists at all. We need the spec and firmware to agree on this stricter definition so the device never pokes the codec/I2C bus when the feature is off.

## What Changes
- Scope the speaker-prepare requirement so it only applies when application audio is enabled.
- Update the audio-flag requirement to state explicitly that disabling audio prevents any codec initialization or I2C traffic.
- Short-circuit all runtime entry points (`thermostat_audio_boot_prepare`, `thermostat_audio_boot_try_play`, `thermostat_audio_boot_play_failure`) plus the boot invocation so no hardware calls execute when the flag is off.

## Impact
- Boot behavior becomes deterministic for SKUs that ship without speakers—no codec bring-up, no I2C access.
- Audio-enabled builds are unaffected; initialization, quiet-hours policy, and playback continue to run exactly once per boot when enabled.
- No additional assets or configuration knobs are required.
