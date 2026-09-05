# arare 独自のファーム拡張。zephyr/module.yml から読み込まれる
if(CONFIG_ARARE_BATTERY_LED)
  zephyr_library()
  zephyr_library_sources(src/battery_led.c)
endif()

if(CONFIG_ARARE_BATTERY_VOLTAGE_DIVIDER)
  zephyr_library_sources(src/battery_voltage_divider_arare.c)
endif()
