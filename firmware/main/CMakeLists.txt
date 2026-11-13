idf_component_register(
    SRCS "main.c" "board.c" "calendar.c" "library.c" "gallery.c" "network.c" "font_es_14.c"
    INCLUDE_DIRS "."
    REQUIRES esp_lcd esp_driver_gpio esp_driver_i2c esp_driver_spi esp_driver_sdspi
             esp_driver_ledc fatfs json esp_wifi esp_event esp_netif nvs_flash
             esp_http_server esp_timer lwip
    EMBED_FILES "../web/dist/index.html"
)

if(EXISTS "${COMPONENT_DIR}/profile-avatar.rgb565")
    target_add_binary_data(${COMPONENT_LIB} "profile-avatar.rgb565" BINARY)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE HAS_PROFILE_AVATAR)
endif()

set(local_config "${COMPONENT_DIR}/local_config.h")
if(NOT EXISTS "${local_config}")
    set(local_config "${COMPONENT_DIR}/local_config.default.h")
endif()
configure_file("${local_config}" "${CMAKE_CURRENT_BINARY_DIR}/device_profile.h" COPYONLY)

target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
