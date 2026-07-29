idf_component_register(
    SRCS
        "app_main.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_hw_support
        esp_netif
        esp_system
        pf_config
        pf_runtime
        pf_storage
        pf_web
        spi_flash
)

# Build valid empty LittleFS images for explicit factory provisioning. They are
# intentionally not attached to normal firmware flashing, so an update cannot
# erase user images.
set(PAPERFRAME_EMPTY_FS_DIR "${CMAKE_CURRENT_BINARY_DIR}/empty_factory_fs")
file(MAKE_DIRECTORY "${PAPERFRAME_EMPTY_FS_DIR}")
littlefs_create_partition_image(webfs "${PAPERFRAME_EMPTY_FS_DIR}")
littlefs_create_partition_image(imagefs "${PAPERFRAME_EMPTY_FS_DIR}")
