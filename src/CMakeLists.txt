idf_component_register(
    SRCS
        "app_main.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        app_update
        esp_hw_support
        esp_netif
        esp_system
        esp_timer
        pf_auth
        pf_carousel
        pf_config
        pf_display
        pf_network
        pf_image
        pf_ota
        pf_runtime
        pf_sensors
        pf_storage
        pf_weather
        pf_web
        spi_flash
)

# An empty imagefs image for explicit factory provisioning. It is not attached
# to normal firmware flashing, so a routine app update cannot erase user
# images. The WebUI needs no image of its own: it is compiled into the app so
# that one OTA updates firmware and frontend together
# (docs/adr/0016-embed-webui-assets-in-firmware.md).
set(PAPERFRAME_EMPTY_FS_DIR "${CMAKE_CURRENT_BINARY_DIR}/empty_factory_fs")
file(MAKE_DIRECTORY "${PAPERFRAME_EMPTY_FS_DIR}")
littlefs_create_partition_image(imagefs "${PAPERFRAME_EMPTY_FS_DIR}")
