# Sanctuary — Amanorsac Studio. Live-service keys instrument (VST3 + AU + Standalone).
# Configure:  cmake -S apps/sanctuary -B apps/sanctuary/build -G Ninja
# Build:      cmake --build apps/sanctuary/build --config Release
#
# Reuses (read-only) the shared .jmi format header and JUCE checkout that live
# next to JM Signature. Nothing here writes into apps/signature.
cmake_minimum_required(VERSION 3.24)
project(Sanctuary VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------ JUCE 8
set(SANCTUARY_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../signature/external/JUCE" CACHE PATH "JUCE checkout")
if(EXISTS "${SANCTUARY_JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${SANCTUARY_JUCE_DIR}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(JMI_FORMAT_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../packages/jmi-format/cpp")

set(SANCTUARY_AUDIO_DEFS JUCE_WASAPI=1 JUCE_DIRECTSOUND=1)
set(SANCTUARY_JUCE_ROOT "${JUCE_SOURCE_DIR}")
if(NOT SANCTUARY_JUCE_ROOT)
    set(SANCTUARY_JUCE_ROOT "${juce_SOURCE_DIR}")
endif()
if(WIN32 AND EXISTS "${SANCTUARY_JUCE_ROOT}/modules/juce_audio_devices/native/asio/iasiodrv.h")
    list(APPEND SANCTUARY_AUDIO_DEFS JUCE_ASIO=1)
    message(STATUS "Sanctuary: ASIO enabled (JUCE-bundled headers)")
endif()

# ------------------------------------------------------------------ Plugin
juce_add_plugin(Sanctuary
    COMPANY_NAME              "Amanorsac Studio"
    PLUGIN_MANUFACTURER_CODE  Amsc
    PLUGIN_CODE               Snct
    PRODUCT_NAME              "Sanctuary"
    BUNDLE_ID                 com.amanorsacstudio.sanctuary
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    FORMATS                   VST3 AU Standalone
    VST3_CATEGORIES           Instrument)

target_sources(Sanctuary PRIVATE
    src/plugin/SanctuaryProcessor.cpp
    src/plugin/SanctuaryEditor.cpp)

target_include_directories(Sanctuary PRIVATE src "${JMI_FORMAT_INCLUDE}")

target_compile_definitions(Sanctuary PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
    ${SANCTUARY_AUDIO_DEFS})

if(TARGET Sanctuary_Standalone)
    target_sources(Sanctuary_Standalone PRIVATE src/plugin/StandaloneApp.cpp)
    target_include_directories(Sanctuary_Standalone PRIVATE src "${JMI_FORMAT_INCLUDE}")
endif()

target_link_libraries(Sanctuary PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------ Tests
juce_add_console_app(SanctuaryTests PRODUCT_NAME "SanctuaryTests")
target_sources(SanctuaryTests PRIVATE tests/SanctuaryTests.cpp)
target_include_directories(SanctuaryTests PRIVATE src "${JMI_FORMAT_INCLUDE}")
target_compile_definitions(SanctuaryTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    SANCTUARY_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../testdata")
target_link_libraries(SanctuaryTests PRIVATE
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_events
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# Processor-level soak test (§9): switches Songs/Moments under sustained notes.
juce_add_console_app(SanctuarySoak PRODUCT_NAME "SanctuarySoak")
target_sources(SanctuarySoak PRIVATE
    tests/SoakTest.cpp
    src/plugin/SanctuaryProcessor.cpp
    src/plugin/SanctuaryEditor.cpp)
target_include_directories(SanctuarySoak PRIVATE src "${JMI_FORMAT_INCLUDE}")
target_compile_definitions(SanctuarySoak PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    SANCTUARY_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../testdata")
target_link_libraries(SanctuarySoak PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

enable_testing()
add_test(NAME SanctuaryUnit COMMAND SanctuaryTests)
add_test(NAME SanctuarySoakShort COMMAND SanctuarySoak --seconds 60)
set_tests_properties(SanctuarySoakShort PROPERTIES
    ENVIRONMENT "SANCTUARY_LIBRARY_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../../testdata"
    TIMEOUT 600)
