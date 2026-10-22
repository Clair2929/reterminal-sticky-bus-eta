cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sticky_bus_eta)

# 规避 xtensa-esp-elf gcc 14.2.0_20241119 在 -Og 下编译
# esp_lcd/rgb/esp_lcd_panel_rgb.c 时的 IRA 编译器崩溃（ICE）。
# 本项目未使用 RGB LCD，该选项只影响 esp_lcd 组件的寄存器分配。
if(TARGET __idf_esp_lcd)
    target_compile_options(__idf_esp_lcd PRIVATE
        -fno-ira-share-spill-slots -fno-ira-share-save-slots)
endif()
