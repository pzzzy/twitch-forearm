#!/bin/sh
set -eu
cd "$(dirname "$0")/../../../.."
font_tools=firmware/components/forearm_fonts/tools
font_python="$font_tools/.font-venv/bin/python"
if [ ! -x "$font_python" ]; then
  python3 -m venv "$font_tools/.font-venv"
  "$font_python" -m pip install -r "$font_tools/font-requirements.txt"
fi
mkdir -p "$font_tools/dump"
"$font_python" "$font_tools/instance-text-fonts.py"
for font_size in 16 18 20 22 24 28 30 32 36; do
"$font_tools/node_modules/.bin/lv_font_conv" \
  --font "$font_tools/dump/Montserrat-Regular.ttf" \
  --range '0x00A0-0x024F,0x0400-0x052F,0x2000-0x206F,0x20A0-0x20CF,0x2190-0x21FF' \
  --font "$font_tools/dump/NotoSans-Regular.ttf" --range '0x0370-0x03FF' \
  --size "$font_size" --bpp 4 --no-compress --format lvgl --lv-include lvgl.h \
  --lv-font-name "forearm_text_$font_size" \
  --output "firmware/components/forearm_fonts/forearm_text_$font_size.c"
python3 "$font_tools/prepend-font-license.py" text "firmware/components/forearm_fonts/forearm_text_$font_size.c"
done
