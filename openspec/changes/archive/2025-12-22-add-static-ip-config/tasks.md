## 1. Implementation
- [x] Add Kconfig entries for static IP enablement and required IPv4 fields.
- [x] Update sdkconfig.defaults with provided defaults (enable static IP, IP/netmask/gateway).
- [x] Apply static IP settings in wifi_remote_manager before Wi-Fi start; fail fast on invalid config.
- [x] Reuse DNS override setting when static IP is enabled.

## 2. Validation
- [x] Run `openspec validate add-static-ip-config --strict`.
