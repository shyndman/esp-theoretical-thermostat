## 1. ISP Configuration
- [ ] 1.1 Add an ISP configuration helper that opens `/dev/video*` controls and enables demosaic, gamma, color, and AWB modules with safe defaults.
- [ ] 1.2 Surface tunable coefficients/gains via Kconfig or menuconfig options.
- [ ] 1.3 Log applied ISP settings during pipeline start for diagnostics.

## 2. Camera Pipeline Integration
- [ ] 2.1 Invoke the ISP helper after V4L2 capture initialization but before encoder start.
- [ ] 2.2 Fail gracefully (with WARN) if any ISP control write is unsupported so streaming continues.

## 3. Validation
- [ ] 3.1 Capture before/after footage (ffplay or Frigate) to confirm color output.
- [ ] 3.2 Update manual test notes with the verification steps.
