cmake_minimum_required(VERSION 3.24)

project(ScavengerSense VERSION 0.8.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# CommonLibSSE's install(EXPORT) requires spdlog to be in an export set when
# spdlog is a real subproject target; without this, generation fails. It has to
# be set BEFORE spdlog is added: set afterwards, it only reaches spdlog via the
# cache on the *next* configure, so a clean clone fails the first time and
# succeeds the second.
set(SPDLOG_INSTALL ON CACHE BOOL "" FORCE)

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
# Do NOT add src to the include path: on case-insensitive filesystems (Windows)
# src/Locale.h would shadow the CRT's <locale.h> for angle-bracket includes.
# Sources include their own headers with quotes, which search the file's
# directory first and need no -I entry.
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../cimgui)
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
