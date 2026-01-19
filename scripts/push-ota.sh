#!/usr/bin/env bash
set -euo pipefail

usage() {
	echo "Usage: $0 [sdkconfig-path]" >&2
	echo "  sdkconfig-path defaults to ./sdkconfig" >&2
}

if [[ ${1:-} == "-h" || ${1:-} == "--help" ]]; then
	usage
	exit 0
fi

SDKCONFIG_PATH="${1:-sdkconfig}"
BIN_PATH="build/esp_theoretical_thermostat.bin"

if [[ ! -f "${SDKCONFIG_PATH}" ]]; then
	echo "push-ota: ${SDKCONFIG_PATH} not found" >&2
	exit 1
fi

if [[ ! -f "${BIN_PATH}" ]]; then
	echo "push-ota: ${BIN_PATH} not found; run idf.py build first" >&2
	exit 1
fi

STATIC_IP_ENABLE=$(grep -E "^CONFIG_THEO_WIFI_STA_STATIC_IP_ENABLE=" "${SDKCONFIG_PATH}" | cut -d= -f2 || true)
STATIC_IP=$(grep -E "^CONFIG_THEO_WIFI_STA_STATIC_IP=" "${SDKCONFIG_PATH}" | cut -d= -f2 | tr -d '"' || true)
OTA_PORT=$(grep -E "^CONFIG_THEO_OTA_PORT=" "${SDKCONFIG_PATH}" | cut -d= -f2 || true)

if [[ -z "${STATIC_IP_ENABLE}" || "${STATIC_IP_ENABLE}" != "y" ]]; then
	echo "push-ota: CONFIG_THEO_WIFI_STA_STATIC_IP_ENABLE must be set to y" >&2
	exit 1
fi

if [[ -z "${STATIC_IP}" ]]; then
	echo "push-ota: CONFIG_THEO_WIFI_STA_STATIC_IP is empty" >&2
	exit 1
fi

if [[ -z "${OTA_PORT}" ]]; then
	OTA_PORT=3232
fi

OTA_URL="http://${STATIC_IP}:${OTA_PORT}/ota"

echo "push-ota: uploading ${BIN_PATH} -> ${OTA_URL}" >&2
curl --fail --show-error --data-binary "@${BIN_PATH}" "${OTA_URL}"
