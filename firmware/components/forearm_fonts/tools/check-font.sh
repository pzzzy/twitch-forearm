#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p tools/dump
lvgl=../../managed_components/lvgl__lvgl
cc -std=c99 -DLV_CONF_INCLUDE_SIMPLE -I../../main -I"$lvgl" -Iinclude \
  forearm_fonts.c forearm_emoji_24.c forearm_text_24.c tools/check-font.c \
  "$lvgl/src/font/lv_font.c" "$lvgl/src/font/lv_font_fmt_txt.c" \
  "$lvgl/src/font/lv_font_montserrat_24.c" "$lvgl/src/misc/lv_utils.c" \
  -o tools/dump/check-font
tools/dump/check-font
