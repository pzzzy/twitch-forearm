option(FOREARM_NETWORK "Build standalone Twitch networking" ON)
set(FOREARM_NETWORK_SOURCE "network_stub.cpp")
if(FOREARM_NETWORK)
    set(FOREARM_NETWORK_SOURCE "twitch_client.cpp")
endif()
idf_component_register(
    SRCS "main.cpp" "board_display.cpp" "app_ui.cpp" "emote_cache.cpp" "startup_animation.cpp" ${FOREARM_NETWORK_SOURCE}
    INCLUDE_DIRS "." "../../core/include"
    REQUIRES esp_psram spi_flash esp_app_format forearm_panel forearm_fonts esp_lcd driver esp_timer
             lvgl__lvgl esp_wifi esp_netif nvs_flash esp_http_client esp-tls
             mbedtls json espressif__esp_websocket_client lwip
             esp_driver_usb_serial_jtag
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
if(FOREARM_NETWORK AND NOT CONFIG_LWIP_DNS_MAX_HOST_IP STREQUAL "1")
    message(FATAL_ERROR "Keep CONFIG_LWIP_DNS_MAX_HOST_IP=1: mitigates GHSA-ch63-jxm5-vmvf on ESP-IDF 5.3.6")
endif()
