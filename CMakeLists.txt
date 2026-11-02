cmake_minimum_required(VERSION 3.16)
project(ProcessManagerGUI LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 COMPONENTS Widgets REQUIRED)

# Tu nhan dien bo cuc: src/ ngang hang (nhu nhom dang dung) hoac ../src khi dat trong thu muc con gui_qt/
set(CORE_DIR "/home/quantrung/Downloads/gui_qt_addon/src")

# ---- Cac file LOI (nguyen ban, KHONG sua) duoc bien dich bang C ----
add_library(core_c STATIC
    ${CORE_DIR}/process_list.c
    ${CORE_DIR}/proc_reader.c
    ${CORE_DIR}/utils.c
    ${CORE_DIR}/process_actions.c
    ${CORE_DIR}/logger.c
)
set_target_properties(core_c PROPERTIES LINKER_LANGUAGE C)
target_compile_options(core_c PRIVATE -std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Wall -Wextra)
target_include_directories(core_c PUBLIC ${CORE_DIR})

# ---- Phan C++ thuan, khong can Qt: giam sat mang + dong cay tien trinh ----
add_library(pm_logic STATIC NetMonitor.cpp NetMonitor.h ProcessTree.cpp ProcessTree.h core_bridge.h)
target_link_libraries(pm_logic PUBLIC core_c)
target_include_directories(pm_logic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ---- GUI Qt ----
add_executable(process_manager_gui
    main_gui.cpp
    MainWindow.h   MainWindow.cpp
    NetworkTab.h   NetworkTab.cpp
    LiveChart.h    LiveChart.cpp
    IconProvider.h IconProvider.cpp
    ViewUtil.h
)
target_link_libraries(process_manager_gui PRIVATE pm_logic Qt5::Widgets)

# ---- Test cho phan logic (khong can Qt). Chay: ctest --output-on-failure ----
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_netmon.cpp)
    enable_testing()
    add_executable(test_netmon tests/test_netmon.cpp)
    target_link_libraries(test_netmon PRIVATE pm_logic)
    add_test(NAME NetMonitor COMMAND test_netmon)

    add_executable(fakeapp tests/fakeapp.c)
    add_executable(test_proctree tests/test_proctree.cpp)
    target_link_libraries(test_proctree PRIVATE pm_logic)
    add_dependencies(test_proctree fakeapp)
    add_test(NAME ProcessTree COMMAND test_proctree $<TARGET_FILE:fakeapp>)
endif()
