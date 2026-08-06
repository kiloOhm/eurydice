cmake_minimum_required(VERSION 3.24)

set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET 12.0 CACHE STRING "" FORCE)

# Default to an optimised build: an unoptimised real-time audio engine drops
# buffers, and JUCE's debug assertions fire on benign conditions during playback.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

project(Eurydice VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(JUCE)

# Rubber Band (GPL) for time-stretching — built via its single-file amalgamation.
FetchContent_Declare(
    rubberband
    GIT_REPOSITORY https://github.com/breakfastquay/rubberband.git
    GIT_TAG v4.0.0
    GIT_SHALLOW ON
)
FetchContent_Populate(rubberband)

juce_add_gui_app(Eurydice
    PRODUCT_NAME "Eurydice"
    COMPANY_NAME "Eurydice"
    BUNDLE_ID "dev.eurydice.daw"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Eurydice needs microphone/line input access to record audio."
    PLUGINHOST_AU TRUE
)

juce_generate_juce_header(Eurydice)

file(GLOB_RECURSE EURYDICE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)
# The helper has its own JUCE application entry point.
list(REMOVE_ITEM EURYDICE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/sandbox/HostMain.cpp)

target_sources(Eurydice PRIVATE
    ${EURYDICE_SOURCES}
    ${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp
)

target_include_directories(Eurydice PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${rubberband_SOURCE_DIR}
)

set_source_files_properties(${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp
    PROPERTIES COMPILE_OPTIONS "-w")

target_compile_definitions(Eurydice PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Eurydice,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Eurydice,JUCE_VERSION>"
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

set(EURYDICE_JUCE_MODULES
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_dsp
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
)

target_link_libraries(Eurydice PRIVATE
    "-framework Carbon"
    "-framework AVFoundation"
    ${EURYDICE_JUCE_MODULES}
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# ---------------- plugin sandbox helper ----------------
# One plugin per process: a crashing VST kills this, not the DAW. Audio rides
# a shared-memory ring (src/sandbox/SharedAudioRing.h); control is JSON lines
# over stdin/stdout.
juce_add_gui_app(EurydiceHelper
    PRODUCT_NAME "EurydiceHelper"
    COMPANY_NAME "Eurydice"
    BUNDLE_ID "dev.eurydice.helper"
    PLUGINHOST_AU TRUE
)

target_sources(EurydiceHelper PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sandbox/HostMain.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/PluginManager.cpp
)

target_include_directories(EurydiceHelper PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(EurydiceHelper PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="EurydiceHelper"
    JUCE_APPLICATION_VERSION_STRING="0.1.0"
    JUCE_MODAL_LOOPS_PERMITTED=1
)

target_link_libraries(EurydiceHelper PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
)

# The DAW spawns the helper, so building one should build the other.
add_dependencies(Eurydice EurydiceHelper)

# ---------------- tests ----------------
option(EURYDICE_BUILD_TESTS "Build unit tests" ON)
if(EURYDICE_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
        GIT_SHALLOW ON
    )
    FetchContent_MakeAvailable(googletest)

    file(GLOB_RECURSE EURYDICE_TEST_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h
    )
    set(EURYDICE_LIB_SOURCES ${EURYDICE_SOURCES})
    list(REMOVE_ITEM EURYDICE_LIB_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/Main.cpp)

    juce_add_console_app(EurydiceTests PRODUCT_NAME "EurydiceTests")
    target_sources(EurydiceTests PRIVATE
        ${EURYDICE_LIB_SOURCES}
        ${EURYDICE_TEST_SOURCES}
        ${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp
    )
    target_include_directories(EurydiceTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${rubberband_SOURCE_DIR}
    )
    target_compile_definitions(EurydiceTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_DISPLAY_SPLASH_SCREEN=0
    )
    target_link_libraries(EurydiceTests PRIVATE
        "-framework Carbon"
        "-framework AVFoundation"
        ${EURYDICE_JUCE_MODULES}
        juce::juce_recommended_config_flags
        gtest
    )
endif()
