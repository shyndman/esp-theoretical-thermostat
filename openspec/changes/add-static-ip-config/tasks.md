## 1. Implementation
- [ ] Add Kconfig entries for static IP enablement and required IPv4 fields.
- [ ] Update sdkconfig.defaults with provided defaults (enable static IP, IP/netmask/gateway).
- [ ] Apply static IP settings in wifi_remote_manager before Wi-Fi start; fail fast on invalid config.
- [ ] Reuse DNS override setting when static IP is enabled.

## 2. Validation
- [ ] Run `openspec validate add-static-ip-config --strict`.
