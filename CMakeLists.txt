cmake_minimum_required(VERSION 3.16)
project(ProcessManagerGUI LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 COMPONENTS Widgets REQUIRED)

# ---- Cac file LOI (nguyen ban, KHONG sua) duoc bien dich bang C ----
# Day la toan bo "thuat toan": doc /proc, tinh %CPU/%MEM, sort, search,
# kill, set priority, log. GUI Qt khong viet lai bat ky dong nao trong day.
add_library(core_c STATIC
    src/process_list.c
    src/proc_reader.c
    src/utils.c
    src/process_actions.c
    src/logger.c
)
set_target_properties(core_c PROPERTIES LINKER_LANGUAGE C)
target_compile_options(core_c PRIVATE -std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Wall -Wextra)

# Dòng này cũng đã được sửa thành "src" thay vì "../src"
target_include_directories(core_c PUBLIC src)

# ---- GUI Qt (C++) - CHI phan nay la code moi ----
add_executable(process_manager_gui
    main_gui.cpp
    MainWindow.h
    MainWindow.cpp
    core_bridge.h
)
target_link_libraries(process_manager_gui PRIVATE core_c Qt5::Widgets)