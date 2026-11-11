#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p tools/dump
lvgl=../../managed_components/lvgl__lvgl
cc -std=c99 -DLV_CONF_INCLUDE_SIMPLE -I../../main -I"$lvgl" -Iinclude \
  forearm_fonts.c forearm_emoji_*.c forearm_text_*.c tools/check-font.c \
  "$lvgl/src/font/lv_font.c" "$lvgl/src/font/lv_font_fmt_txt.c" \
  "$lvgl/src/font/lv_font_montserrat_24.c" "$lvgl/src/misc/lv_utils.c" \
  "$lvgl/src/font/lv_font_montserrat_20.c" "$lvgl/src/font/lv_font_montserrat_22.c" \
  "$lvgl/src/font/lv_font_montserrat_28.c" "$lvgl/src/font/lv_font_montserrat_30.c" \
  "$lvgl/src/font/lv_font_montserrat_16.c" "$lvgl/src/font/lv_font_montserrat_18.c" \
  "$lvgl/src/font/lv_font_montserrat_32.c" "$lvgl/src/font/lv_font_montserrat_36.c" \
  -o tools/dump/check-font
tools/dump/check-font
