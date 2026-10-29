idf_component_register(
    SRCS main.cpp display.cpp rtc.cpp
    INCLUDE_DIRS .
    REQUIRES vpet_engine esp_lcd esp_driver_gpio esp_driver_i2c esp_driver_spi esp_driver_ledc esp_timer
             joltwallet__littlefs)

# Stand-in clock until there is an on-device clock screen: the build PC's UTC
# time (used only when the RTC has never been set) and its UTC offset.
string(TIMESTAMP build_utc "%s" UTC)
string(TIMESTAMP local_h "%H")
string(TIMESTAMP local_m "%M")
string(TIMESTAMP utc_h "%H" UTC)
string(TIMESTAMP utc_m "%M" UTC)
# "1${x} - 100" strips leading zeros so "08" isn't read as octal.
math(EXPR offset_min "(1${local_h} - 100) * 60 + (1${local_m} - 100) - (1${utc_h} - 100) * 60 - (1${utc_m} - 100)")
if(offset_min GREATER 840)
    math(EXPR offset_min "${offset_min} - 1440")
elseif(offset_min LESS -720)
    math(EXPR offset_min "${offset_min} + 1440")
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE VPET_BUILD_UTC=${build_utc}LL VPET_UTC_OFFSET_MIN=${offset_min})

# Bake the demo pack into the "packs" flash partition.
littlefs_create_partition_image(packs ${CMAKE_CURRENT_LIST_DIR}/../../../packs/demo FLASH_IN_PROJECT)
