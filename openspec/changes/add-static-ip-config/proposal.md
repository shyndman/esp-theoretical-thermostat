# Change: Add Static IP Configuration for Wi-Fi STA

## Why
Some deployments require a fixed IP address for reliable access and routing. DHCP reservations are not always available or portable.

## What Changes
- Add Kconfig options for enabling and configuring a static IPv4 address for the Wi-Fi STA interface.
- Apply static IP configuration during Wi-Fi bring-up when enabled, making it mandatory (invalid/missing config fails Wi-Fi start).
- Reuse the existing DNS override setting for static DNS configuration.

## Impact
- Affected specs: thermostat-connectivity
- Affected code: main/connectivity/wifi_remote_manager.c, main/Kconfig.projbuild, sdkconfig.defaults
