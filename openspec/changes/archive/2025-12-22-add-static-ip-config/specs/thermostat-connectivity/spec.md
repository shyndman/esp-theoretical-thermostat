## ADDED Requirements

### Requirement: Static IP Configuration for Wi-Fi STA
When `CONFIG_THEO_WIFI_STA_STATIC_IP_ENABLE` is enabled, the firmware SHALL configure the Wi-Fi STA netif with a static IPv4 address using `CONFIG_THEO_WIFI_STA_STATIC_IP`, `CONFIG_THEO_WIFI_STA_STATIC_NETMASK`, and `CONFIG_THEO_WIFI_STA_STATIC_GATEWAY`. Missing or invalid values SHALL cause Wi-Fi bring-up to fail rather than falling back to DHCP.

#### Scenario: Static IP Enabled with Valid Settings
- **GIVEN** `CONFIG_THEO_WIFI_STA_STATIC_IP_ENABLE=y`
- **AND** valid `CONFIG_THEO_WIFI_STA_STATIC_IP`, `CONFIG_THEO_WIFI_STA_STATIC_NETMASK`, and `CONFIG_THEO_WIFI_STA_STATIC_GATEWAY` values are configured
- **WHEN** the firmware starts Wi-Fi
- **THEN** DHCP is disabled for the STA netif
- **AND** the configured static IPv4 address, netmask, and gateway are applied before connecting
- **AND** Wi-Fi bring-up proceeds to the normal connected state.

#### Scenario: Static IP Enabled with Invalid Settings
- **GIVEN** `CONFIG_THEO_WIFI_STA_STATIC_IP_ENABLE=y`
- **AND** any of `CONFIG_THEO_WIFI_STA_STATIC_IP`, `CONFIG_THEO_WIFI_STA_STATIC_NETMASK`, or `CONFIG_THEO_WIFI_STA_STATIC_GATEWAY` is empty or invalid
- **WHEN** the firmware starts Wi-Fi
- **THEN** Wi-Fi bring-up fails and returns an error without falling back to DHCP.

### Requirement: DNS Override Reuse for Static IP
When static IP is enabled and `CONFIG_THEO_DNS_OVERRIDE_ADDR` is non-empty, the firmware SHALL apply it as the primary DNS server for the STA netif.

#### Scenario: Static IP with DNS Override
- **GIVEN** `CONFIG_THEO_WIFI_STA_STATIC_IP_ENABLE=y`
- **AND** `CONFIG_THEO_DNS_OVERRIDE_ADDR` is a valid IPv4 address
- **WHEN** the firmware configures the STA netif
- **THEN** the DNS override is applied as DNS[0] for the STA netif.
