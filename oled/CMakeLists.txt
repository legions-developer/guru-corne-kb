# The user-config workflow loads this directory through zephyr/module.yml.
if(CONFIG_SHIELD_NICE_OLED AND CONFIG_ZMK_DISPLAY AND CONFIG_USB_DEVICE_STACK AND
   CONFIG_ZMK_BATTERY_NRF_VDDH AND CONFIG_ZMK_BATTERY_REPORTING)
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/battery.c)
  target_link_libraries(zephyr_interface INTERFACE "-Wl,--wrap=draw_battery_status")
endif()

if(CONFIG_SHIELD_NICE_OLED AND CONFIG_ZMK_DISPLAY AND
   CONFIG_NICE_OLED_WIDGET_WPM AND CONFIG_NICE_OLED_WIDGET_WPM_NUMBER AND
   (NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL))
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/wpm.c)

  # screen.c calls this external function before rotating the OLED canvas.
  # Replace only that renderer; keep upstream WPM calculation and animation events.
  target_link_libraries(zephyr_interface INTERFACE "-Wl,--wrap=draw_wpm_status")
endif()

if(CONFIG_SHIELD_NICE_OLED AND CONFIG_ZMK_DISPLAY AND CONFIG_ZMK_SPLIT)
  if(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/bongo_relay.c)
  else()
    target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/bongo_peripheral.c)
    # Preserve the module's own battery/link widgets; replace only its looping art.
    target_link_libraries(zephyr_interface INTERFACE "-Wl,--wrap=draw_animation")
  endif()
endif()
