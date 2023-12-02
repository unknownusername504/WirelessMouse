idf_component_register(
    SRCS "kami_mouse_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES "driver" "usb" "xtensa" "newlib" "freertos" "hal" "esp_wifi" "esp_timer" "esp_system" "esp_hid" "esp_common" "bootloader" "bt" "console" "log" "esp_hw_support"
)