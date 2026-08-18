# The WebUI is compiled into the app image so that a single OTA download
# updates the firmware and the frontend together
# (docs/adr/0016-embed-webui-assets-in-firmware.md). data/web/* used to live
# in a separate webfs LittleFS partition that OTA never touched, which let the
# frontend fall behind the backend after an update -- a failure mode that
# presented misleadingly (see docs/hardware/VALIDATION.md).
#
# tools/generate_web_assets.py gzips every asset into generated/*.cpp. It runs
# here so the file exists before CMake's code model is built, and again from
# tools/platformio_web_assets.py before every build so frontend edits land
# without needing a CMake reconfigure. See that script's docstring for why the
# payload is not attached with EMBED_FILES / target_add_binary_data.
idf_build_get_property(pf_web_python PYTHON)

execute_process(
    COMMAND
        "${pf_web_python}"
        "${CMAKE_CURRENT_LIST_DIR}/../../tools/generate_web_assets.py"
        --source-dir "${CMAKE_CURRENT_LIST_DIR}/../../data/web"
        --output-dir "${CMAKE_CURRENT_LIST_DIR}/generated"
        --quiet
    RESULT_VARIABLE pf_web_assets_result)
if(NOT pf_web_assets_result EQUAL 0)
    message(
        FATAL_ERROR
        "generate_web_assets.py failed (${pf_web_assets_result})")
endif()

idf_component_register(
    SRCS
        "health_server.cpp"
        "generated/web_assets_generated.cpp"
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "generated"
    REQUIRES
        esp_http_server
        esp_timer
        freertos
        pf_auth
        pf_config
        pf_network
        pf_ota
        pf_runtime
        pf_storage
        pf_weather
)
