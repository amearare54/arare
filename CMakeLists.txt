# arare 独自のファーム拡張。zephyr/module.yml から読み込まれる
if(CONFIG_ARARE_BATTERY_LED)
  zephyr_library()
  zephyr_library_sources(src/battery_led.c)
endif()

if(CONFIG_ARARE_BATTERY_VOLTAGE_DIVIDER)
  zephyr_library_sources(src/battery_voltage_divider_arare.c)
endif()

if(CONFIG_ARARE_INPUT_PROCESSOR_GESTURE)
  zephyr_library_sources(src/input_processor_gesture.c)
endif()

# 診断用（diag-R-tb ブランチ限定）
if(CONFIG_ARARE_TB_PROBE)
  zephyr_library_sources(src/tb_probe.c)
endif()
