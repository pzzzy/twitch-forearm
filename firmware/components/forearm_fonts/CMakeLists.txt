idf_component_register(
    SRCS "forearm_fonts.c" "forearm_emoji_16.c" "forearm_emoji_20.c" "forearm_emoji_24.c"
         "forearm_text_16.c" "forearm_text_18.c" "forearm_text_20.c" "forearm_text_22.c" "forearm_text_24.c"
         "forearm_text_28.c" "forearm_text_30.c" "forearm_text_32.c" "forearm_text_36.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl__lvgl
)
