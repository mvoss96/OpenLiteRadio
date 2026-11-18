cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(openliteradio)
target_include_directories(app PRIVATE src)
target_sources(app PRIVATE
  src/main.cpp
  src/board/board.cpp
  src/core/channels.cpp
  src/core/crsf.cpp
)
target_sources_ifdef(CONFIG_UPDATER_ENTRY app PRIVATE src/updater_entry.c)
