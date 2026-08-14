cmake_minimum_required(VERSION 3.24)

project(ScavengerSense VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(spdlog SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../spdlog OVERRIDE_FIND_PACKAGE)
FetchContent_MakeAvailable(spdlog)

set(RAPIDCSV_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../rapidcsv/src CACHE PATH "" FORCE)

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SKSE_SUPPORT_XBYAK OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../CommonLibSSE-NG CommonLibSSE-NG)

add_library(${PROJECT_NAME} SHARED
    src/Arousal.cpp
    src/Arousal.h
    src/Relations.cpp
    src/Relations.h
    src/Config.cpp
    src/GameMenus.cpp
    src/GameMenus.h
    src/Config.h
    src/Labels.cpp
    src/Labels.h
    src/Locale.cpp
    src/Locale.h
    src/Main.cpp
    src/Timing.cpp
    src/Timing.h
    src/Marks.cpp
    src/Marks.h
    src/Menu.cpp
    src/Menu.h
    src/Sense.cpp
    src/Sense.h
    src/Titles.cpp
    src/Titles.h
    src/Vision.cpp
    src/Vision.h
    src/PostFX.cpp
    src/PostFX.h
    src/PCH.h
)

target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
target_include_directories(${PROJECT_NAME} PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../cimgui)
target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
# SKSE Menu Framework re-exports the cimgui C API from its own DLL; this import
# library is generated from that DLL's export table (see extern/lib/*.def).
target_link_libraries(${PROJECT_NAME} PRIVATE
    CommonLibSSE::CommonLibSSE
    ${CMAKE_CURRENT_SOURCE_DIR}/../extern/lib/SKSEMenuFramework.lib
    windowscodecs.lib
    ole32.lib
)

target_compile_options(${PROJECT_NAME} PRIVATE
    /permissive-
    /utf-8
    /Zc:preprocessor
)
