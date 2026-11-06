idf_component_register(SRCS "main.c" "counter.c"
    INCLUDE_DIRS "."
    REQUIRES yaykit_board lvgl__lvgl yaykit_app yaykit_key yaykit_link yaykit_ui
    PRIV_REQUIRES nvs_flash console esp_timer)
# The shared font partition; the app's own full install writes it too. Found through yaykit_board, wherever the
# shared components are (this repository or the development kit).
idf_component_get_property(board_dir yaykit_board COMPONENT_DIR)
esptool_py_flash_to_partition(flash "fonts" "${board_dir}/fonts/cjk/yaykit_fonts.bin")
target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_VERSION="${PROJECT_VER}")
