cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(openliteradio)
target_include_directories(app PRIVATE src)
target_sources(app PRIVATE
  src/main.cpp
  src/calibration.cpp
  src/host.cpp
  src/indicators.cpp
  src/module_link.cpp
  src/output.cpp
  src/storage.cpp
  src/board/board.cpp
  src/core/channels.cpp
  src/core/crsf.cpp
  src/core/joystick.cpp
  src/core/led.cpp
  src/core/mode.cpp
  src/usb_device.c
  src/core/settings.cpp
)
target_sources_ifdef(CONFIG_UPDATER_ENTRY app PRIVATE src/updater_entry.c)
