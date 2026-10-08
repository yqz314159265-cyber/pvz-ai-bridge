cmake_minimum_required(VERSION 3.24)
project(pvz_ai_native LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "The PvZ native bridge requires Windows.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The game is 32-bit; configure with -A Win32.")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(minhook STATIC
    vendor/minhook/src/buffer.c
    vendor/minhook/src/hook.c
    vendor/minhook/src/trampoline.c
    vendor/minhook/src/hde/hde32.c)
target_include_directories(minhook PUBLIC vendor/minhook/include PRIVATE vendor/minhook/src vendor/minhook/src/hde)
target_compile_definitions(minhook PRIVATE WIN32_LEAN_AND_MEAN)
set_target_properties(minhook PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(pvz_bridge SHARED src/bridge.cpp src/game.cpp src/observations.cpp src/menu.cpp src/shop.cpp src/garden.cpp src/continue_dialog.cpp src/options_controls.cpp src/profiles.cpp src/zombatar.cpp src/ui_controls.cpp src/selector_controls.cpp src/lifecycle_controls.cpp src/game_input.cpp)
target_include_directories(pvz_bridge PRIVATE src vendor)
target_link_libraries(pvz_bridge PRIVATE minhook Advapi32 User32 Version Bcrypt)
set_target_properties(pvz_bridge PROPERTIES OUTPUT_NAME "pvz_bridge")

add_executable(pvz_injector src/injector.cpp)
target_link_libraries(pvz_injector PRIVATE Version Bcrypt)
set_target_properties(pvz_injector PROPERTIES OUTPUT_NAME "pvz_injector")

foreach(target pvz_bridge pvz_injector)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../build/native/$<CONFIG>"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../build/native/$<CONFIG>"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>")
endforeach()
