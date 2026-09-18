# Keep the nice_oled shield defaults/driver, with a small shared status screen.
if(CONFIG_SHIELD_NICE_OLED AND CONFIG_ZMK_DISPLAY)
  target_sources(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/status_screen.c
    ${CMAKE_CURRENT_LIST_DIR}/status_art.c
    ${CMAKE_CURRENT_LIST_DIR}/status_relay.c
    ${ZEPHYR_NICE_OLED_MODULE_DIR}/boards/shields/nice_oled/src/fonts/pixel_operator_mono_12.c
    ${ZEPHYR_NICE_OLED_MODULE_DIR}/boards/shields/nice_oled/src/fonts/pixel_operator_mono_16.c)

  if(CONFIG_USB_DEVICE_STACK AND CONFIG_ZMK_BATTERY_NRF_VDDH AND
     CONFIG_ZMK_BATTERY_REPORTING)
    target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/battery.c)
  endif()
endif()
