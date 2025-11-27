# Change: Introduce audio pipelines

## Why
The existing firmware can only drive the Waveshare BSP's built-in ES8311 codec. Scott is bringing up another ESP32-P4 board that lacks that BSP entirely but has a MAX98357 I2S amplifier on hand. We need a minimal way to pick between the current BSP codec path and a MAX98357 pipeline at build time so the same firmware image can target either piece of hardware without rewiring the policy or PCM assets.

## What Changes
- Add a tiny audio driver abstraction that keeps policy (quiet hours, clock sync, PCM selection) unchanged but lets us swap the low-level playback backend.
- Provide two backends: the existing BSP codec driver, and a new MAX98357 path that configures ESP32-P4 I2S TX and streams our 16 kHz mono PCM buffers to the amp.
- Extend Kconfig/CMake so integrators pick a pipeline (`bsp_codec` vs `i2s_max98357`) and only the relevant driver compiles.
- Document the MAX98357 wiring expectations (I2S pins, enable pin) and keep volume under the existing `CONFIG_THEO_BOOT_CHIME_VOLUME` knob.

## Impact
- **Specs:** Adds a capability that defines how audio pipelines are selected, what the MAX98357 path must implement, and how the BSP default behaves when selected.
- **Code:** Touches `thermostat/audio_boot.*` (to call the new abstraction), introduces driver source files (one for BSP, one for MAX98357), updates `Kconfig.projbuild`, `CMakeLists.txt`, and possibly documentation describing hardware wiring.
