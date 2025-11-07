#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_DIR="$ROOT_DIR/assets/fonts"
OUT_DIR="$ROOT_DIR/main/assets/fonts"
mkdir -p "$OUT_DIR"

if command -v lv_font_conv >/dev/null 2>&1; then
  FONT_CMD=(lv_font_conv)
elif command -v npx >/dev/null 2>&1; then
  FONT_CMD=(npx --yes lv_font_conv)
else
  echo "Error: lv_font_conv not found. Install it globally or ensure npx is available." >&2
  exit 1
fi

generate_font() {
  local font_file="$1"
  local size="$2"
  local name="$3"
  local output="$4"
  local symbols="$5"

  echo "[lv_font_conv] $font_file size=$size -> $output"
  "${FONT_CMD[@]}" \
    --font "$SRC_DIR/$font_file" \
    --size "$size" \
    --bpp 4 \
    --format lvgl \
    --lv-include "lvgl.h" \
    --lv-font-name "$name" \
    --no-prefilter \
    --no-compress \
    --symbols "$symbols" \
    --output "$OUT_DIR/$output"
}

# Tabular numerals for the main setpoint values (include digits, decimal, degree, minus, space)
SETPOINT_SYMBOLS=$'0123456789.-° '
# Uppercase labels plus digits/degree/space/colon for the top bar and status
TOP_BAR_SYMBOLS=$' ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789°:.'

generate_font "Figtree-tnum-SemiBold.otf" 120 "Figtree_Tnum_SemiBold_120" "figtree_tnum_semibold_120.c" "$SETPOINT_SYMBOLS"
generate_font "Figtree-tnum-Medium.otf" 50 "Figtree_Tnum_Medium_50" "figtree_tnum_medium_50.c" "$SETPOINT_SYMBOLS"
generate_font "Figtree-Medium.otf" 39 "Figtree_Medium_39" "figtree_medium_39.c" "$TOP_BAR_SYMBOLS"
generate_font "Figtree-Medium.otf" 34 "Figtree_Medium_34" "figtree_medium_34.c" "$TOP_BAR_SYMBOLS"

echo "Fonts written to $OUT_DIR"
