## 1. Implementation
- [ ] 1.1 Update boot sequence to treat env sensor init failure as non-fatal (keep red splash error line; do not reboot).
- [ ] 1.2 Update environmental sensor service behavior on init failure:
  - [ ] Publish retained HA discovery config (if not already guaranteed on boot)
  - [ ] Publish retained per-entity `availability=offline` for all env entities
  - [ ] Ensure no sampling task starts and no telemetry publishes occur (all-or-nothing)
  - [ ] Ensure there is no periodic/automatic retry for boot-time init failure
- [ ] 1.3 Ensure device identity/topic strings used by other subsystems are available even when env sensors are missing.

## 2. Validation
- [ ] 2.1 `idf.py build`
- [ ] 2.2 Manual boot test with env sensors disconnected:
  - [ ] Splash shows env init error line in red
  - [ ] Device reaches main UI (no reboot loop)
  - [ ] Home Assistant shows env sensor entities as unavailable (retained `availability=offline`)
- [ ] 2.3 Manual boot test with sensors present:
  - [ ] Env entities publish online availability and state updates as before
