# Tasks
- [x] Implement LVGL splash helper that draws "Verbing ..." text and exposes APIs to update text + show error state.
- [x] Reorder `app_main` to initialize display/backlight, then splash, then speaker codec, followed by each connectivity service while updating the splash before every stage.
- [x] Extend `thermostat/audio_boot.*` with a speaker-prep call and a failure-tone playback path that uses `main/assets/audio/failure.c` while honoring quiet hours.
- [x] Wire failure handling so any boot-stage error updates the splash, triggers the failure tone, and halts startup cleanly; keep the boot chime at the end of a successful sequence.
- [ ] Validate via `openspec validate update-boot-sequence-feedback --strict` plus a dry `idf.py build` locally to ensure new helpers compile. (`openspec` passed; `idf.py build` blocked by offline component registry access.)
