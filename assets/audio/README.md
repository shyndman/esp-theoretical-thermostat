# Scott Greeting Asset Workflow

1. Record a **1.2 s (+/- 50 ms)** mono clip at **16 kHz / 16-bit PCM**. Any longer file will be trimmed when we regenerate the assets, so align your waveform to roughly 1.20 s.
2. Export the clip as `assets/audio/scott_greeting.wav` (overwriting the placeholder in this directory). Keep the filename lowercase to match `soundgen.toml`.
3. Run `scripts/generate_sounds.py` from the repo root. The script enforces:
   - Sample rate exactly 16000 Hz
   - Single-channel audio
   - 16-bit depth
   If any property is wrong the script aborts, ensuring firmware builds can’t embed a bad asset.
4. Commit both `assets/audio/scott_greeting.wav` **and** the generated `main/assets/audio/scott_greeting.c`. Builds depend on the `.c` blob; the `.wav` is the editable source of truth.

Tips:

- Use the same recording chain (mic + room) for retakes so the energy matches the existing startup cues.
- Leave ~50 ms of silence at the head so the LED effect and audio stay in sync when triggered.
- If you need to audition the compiled PCM, `scripts/generate_sounds.py --preview` is not available—just play the WAV directly.
