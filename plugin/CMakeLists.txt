cmake_minimum_required(VERSION 3.24)

project(ScavengerSense VERSION 0.10.4 LANGUAGES CXX)

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
    src/FontCheck.cpp
    src/FrameworkABI.cpp
    src/FrameworkABI.h
    src/ImGuiCompat.h
    src/Perks.cpp
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
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../cimgui
    ${CMAKE_CURRENT_SOURCE_DIR}/../extern/imgui-abi)
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

option(SCAVENGER_BUILD_TESTS "Build offline config, HUD policy and runtime probes" OFF)
if(SCAVENGER_BUILD_TESTS)
    enable_testing()
    enable_language(RC)
    add_executable(FrameworkAbiProbe tests/FrameworkAbiProbe.cpp)
    target_include_directories(FrameworkAbiProbe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../cimgui ${CMAKE_CURRENT_SOURCE_DIR}/../extern/imgui-abi)
    add_test(NAME framework_abi_layout COMMAND FrameworkAbiProbe)
    add_executable(FrameworkAbiGuard tests/FrameworkAbiGuard.cpp src/FrameworkABI.cpp)
    target_precompile_headers(FrameworkAbiGuard PRIVATE src/PCH.h)
    target_compile_definitions(FrameworkAbiGuard PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(FrameworkAbiGuard PRIVATE /utf-8 /Zc:preprocessor)
    target_include_directories(FrameworkAbiGuard PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../cimgui ${CMAKE_CURRENT_SOURCE_DIR}/../extern/imgui-abi)
    target_link_libraries(FrameworkAbiGuard PRIVATE CommonLibSSE::CommonLibSSE)
    add_test(NAME framework_abi_guard COMMAND FrameworkAbiGuard)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/features")
    add_executable(ScavengerSenseFeatures tests/Features.cpp src/Config.cpp src/Locale.cpp)
    target_precompile_headers(ScavengerSenseFeatures PRIVATE src/PCH.h)
    target_compile_definitions(ScavengerSenseFeatures PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(ScavengerSenseFeatures PRIVATE /utf-8 /Zc:preprocessor)
    target_link_libraries(ScavengerSenseFeatures PRIVATE CommonLibSSE::CommonLibSSE)
    add_test(NAME features COMMAND ScavengerSenseFeatures)
    set_tests_properties(features PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/features" TIMEOUT 30)
    add_executable(ConfigProbe tests/ConfigProbe.cpp src/Config.cpp)
    target_precompile_headers(ConfigProbe REUSE_FROM ScavengerSenseFeatures)
    target_compile_definitions(ConfigProbe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(ConfigProbe PRIVATE /utf-8 /Zc:preprocessor)
    target_link_libraries(ConfigProbe PRIVATE CommonLibSSE::CommonLibSSE)
    add_executable(DisplayChecks tests/DisplayChecks.cpp)
    target_precompile_headers(DisplayChecks PRIVATE src/PCH.h)
    target_compile_definitions(DisplayChecks PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(DisplayChecks PRIVATE /utf-8 /Zc:preprocessor)
    target_include_directories(DisplayChecks PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../cimgui ${CMAKE_CURRENT_SOURCE_DIR}/../extern/imgui-abi)
    target_link_libraries(DisplayChecks PRIVATE CommonLibSSE::CommonLibSSE)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/display")
    add_test(NAME display COMMAND DisplayChecks)
    set_tests_properties(display PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/display" TIMEOUT 30)
    foreach(version IN ITEMS "1;5;97" "1;6;1170" "1;7;104")
        list(GET version 0 PROBE_MAJOR)
        list(GET version 1 PROBE_MINOR)
        list(GET version 2 PROBE_PATCH)
        set(probe "RuntimeProbe_${PROBE_MAJOR}_${PROBE_MINOR}_${PROBE_PATCH}")
        configure_file(tests/RuntimeProbe.rc.in "${CMAKE_CURRENT_BINARY_DIR}/${probe}.rc" @ONLY)
        add_executable(${probe} tests/RuntimeProbe.cpp "${CMAKE_CURRENT_BINARY_DIR}/${probe}.rc")
        target_precompile_headers(${probe} REUSE_FROM ScavengerSenseFeatures)
        target_compile_definitions(${probe} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_compile_options(${probe} PRIVATE /utf-8 /Zc:preprocessor)
        target_link_libraries(${probe} PRIVATE CommonLibSSE::CommonLibSSE)
        if(PROBE_MINOR LESS 6)
            set(runtime SE)
        else()
            set(runtime AE)
        endif()
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/${probe}")
        add_test(NAME ${probe} COMMAND ${probe} ${runtime})
        set_tests_properties(${probe} PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/${probe}" TIMEOUT 30)
    endforeach()
endif()
