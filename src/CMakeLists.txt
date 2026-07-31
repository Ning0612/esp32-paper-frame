idf_component_register(
    SRCS
        "app_main.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
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
        pf_runtime
        pf_sensor_task
        pf_storage
        pf_weather
        pf_weather_worker
        pf_web
        spi_flash
)

# Build a self-contained WebUI image and an empty imagefs image for explicit
# factory provisioning. Neither image is attached to normal firmware flashing,
# so a daily app update cannot erase web assets or user images.
set(PAPERFRAME_EMPTY_FS_DIR "${CMAKE_CURRENT_BINARY_DIR}/empty_factory_fs")
file(MAKE_DIRECTORY "${PAPERFRAME_EMPTY_FS_DIR}")
littlefs_create_partition_image(
    webfs
    "${CMAKE_CURRENT_LIST_DIR}/../data/web"
)
littlefs_create_partition_image(imagefs "${PAPERFRAME_EMPTY_FS_DIR}")
