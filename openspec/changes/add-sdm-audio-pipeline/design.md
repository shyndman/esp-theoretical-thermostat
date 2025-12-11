## Context
The ESP32-P4's built-in sigma-delta modulator provides a low-fidelity audio output option using only a single GPIO and an external RC low-pass filter. This is useful when external DAC hardware is unavailable or unreliable.

Key constraint: The SDM peripheral has **no DMA**. Each sample must be pushed via `sdm_channel_set_pulse_density()`, requiring a timer ISR to maintain the 16kHz sample rate.

## Goals / Non-Goals
- **Goals**: Add SDM as a third selectable audio pipeline; reuse existing 16kHz PCM assets; match blocking behavior of other drivers
- **Non-Goals**: High-fidelity audio; hardware volume control; dynamic sample rate

## Decisions

### Timer-driven ISR for sample output
- **Decision**: Use `gptimer` at 16kHz to call `sdm_channel_set_pulse_density()` from ISR
- **Alternatives considered**:
  - FreeRTOS task with tight loop: Less precise timing, blocks a core
  - Lower sample rate (8kHz): Would require asset pipeline changes
- **Rationale**: ISR provides most consistent timing; 16kHz overhead is acceptable for short chimes

### PCM → Density conversion
- **Decision**: `density = (pcm_sample * 90) / 32768`, clamping to [-90, 90]
- **Rationale**: ESP-IDF docs recommend [-90, 90] range for "better randomness" in the modulator output

### Blocking play()
- **Decision**: `play()` blocks via semaphore until ISR completes buffer
- **Rationale**: Matches MAX98357 and BSP codec behavior; callers expect synchronous completion

### Software gain
- **Decision**: Scale PCM samples by gain percentage before density conversion
- **Rationale**: Consistent with MAX98357 approach; no hardware volume knob on SDM

## Risks / Trade-offs
- **Low audio quality** → Expected; this is a fallback option, not primary
- **ISR CPU overhead** → Acceptable for short chimes; would not scale to continuous audio
- **Timer jitter under load** → May cause audible artifacts; mitigated by keeping ISR minimal

## Open Questions
- None; design discussed and agreed with Scott
