# JM Signature — JUCE 8 instrument plugin (VST3 + AU + Standalone) + console tests.
# Configure:  cmake -B build -G "Visual Studio 18 2026" -A x64
# Build:      cmake --build build --config Release
cmake_minimum_required(VERSION 3.24)
project(JMSignature VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------ JUCE 8
# Pinned to tag 8.0.15 (newest 8.x at build time; 8.0.4 superseded).
# A pre-cloned copy in external/JUCE is used when present (offline builds);
# otherwise FetchContent clones the pinned tag.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Shared read-only format header (packages/jmi-format/cpp/jmi_format.h)
set(JMI_FORMAT_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../packages/jmi-format/cpp")

# ------------------------------------------------------------------ Plugin
juce_add_plugin(JMSignature
    COMPANY_NAME              "JM Studios"
    PLUGIN_MANUFACTURER_CODE  JmSt
    PLUGIN_CODE               JmSg
    PRODUCT_NAME              "JM Signature"
    BUNDLE_ID                 com.jmstudios.jmsignature
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    FORMATS                   VST3 AU Standalone   # AU is ignored by JUCE on non-Apple platforms
    VST3_CATEGORIES           Instrument)

target_sources(JMSignature PRIVATE
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginEditor.cpp)

# Client UX pack assets (docs/svg-pack): kente panel rasters extracted from the
# *-panel.svg data URIs (per the pack's JUCE note) + vector marks/icons/branding.
juce_add_binary_data(JMAssets HEADER_NAME JMAssets.h NAMESPACE JMAssets SOURCES
    resources/kente_rhodes.png
    resources/kente_mks.jpeg
    resources/kente_fm.jpeg
    resources/kente_strings.jpeg
    resources/svg/jm-shield-reference.svg
    resources/svg/engraved-mark-flow.svg
    resources/svg/engraved-mark-knot.svg
    resources/svg/engraved-mark-orbit.svg
    resources/svg/engraved-mark-petal.svg
    resources/svg/engraved-mark-weave.svg
    resources/svg/chevron-left.svg
    resources/svg/chevron-right.svg
    resources/svg/gear.svg
    resources/svg/heart.svg
    resources/svg/kebab.svg
    resources/svg/star.svg
    # photoreal hardware kit (docs/hardware-kit) — client review round 2
    resources/kit/knob-large-00.png
    resources/kit/knob-small-00.png
    resources/kit/panel-teal.png
    resources/kit/panel-orange.png
    resources/kit/panel-purple.png
    resources/kit/panel-blue.png
    resources/kit/panel-red.png
    resources/kit/panel-green.png
    resources/kit/panel-master-empty.png
    resources/kit/button-empty-up.png
    resources/kit/button-empty-down.png
    resources/kit/button-master-empty.png
    resources/kit/meter-led-left.png
    resources/kit/meter-led-right.png
    resources/kit/screw-00.png
    resources/kit/icon-power.png
    resources/kit/rack-corner.png
    resources/kit/divider-gold.png
    resources/kit/flourish-gold.png)
target_link_libraries(JMSignature PRIVATE JMAssets)

target_include_directories(JMSignature PRIVATE src "${JMI_FORMAT_INCLUDE}")

target_compile_definitions(JMSignature PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    # Client review round 3 (B/E): we own the standalone app + window, so the
    # JUCE "Options" title-bar button is gone and all three window controls
    # (minimise / maximise / close) are available. See src/plugin/StandaloneApp.cpp.
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

# The custom standalone app lives ONLY in the Standalone target.
if(TARGET JMSignature_Standalone)
    target_sources(JMSignature_Standalone PRIVATE src/plugin/StandaloneApp.cpp)
    target_include_directories(JMSignature_Standalone PRIVATE
        src "${JMI_FORMAT_INCLUDE}"
        "${CMAKE_CURRENT_BINARY_DIR}/juce_binarydata_JMAssets/JuceLibraryCode")
endif()

target_link_libraries(JMSignature PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------ Tests
juce_add_console_app(JMSignatureTests PRODUCT_NAME "JMSignatureTests")

target_sources(JMSignatureTests PRIVATE tests/EngineTests.cpp)
target_include_directories(JMSignatureTests PRIVATE src "${JMI_FORMAT_INCLUDE}")
target_compile_definitions(JMSignatureTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JM_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../testdata")
target_link_libraries(JMSignatureTests PRIVATE
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_events
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------- V2 acceptance self-check
# Offscreen 1000x720 render + control-centre measurement vs specs/screen-layouts.json.
juce_add_console_app(JMSignatureSnapshot PRODUCT_NAME "JMSignatureSnapshot")
target_sources(JMSignatureSnapshot PRIVATE
    tests/SnapshotCheck.cpp
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginEditor.cpp)
target_include_directories(JMSignatureSnapshot PRIVATE src "${JMI_FORMAT_INCLUDE}"
    "${CMAKE_CURRENT_BINARY_DIR}/juce_binarydata_JMAssets/JuceLibraryCode")
target_compile_definitions(JMSignatureSnapshot PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JM_SNAPSHOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/build/snapshots")
target_link_libraries(JMSignatureSnapshot PRIVATE
    JMAssets
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

enable_testing()
add_test(NAME EngineTests COMMAND JMSignatureTests)
# Client review round 3: the UI/arrangement self-check is a real test now, not a
# manual tool - it carries the master-section, chrome, macro and letterbox assertions.
add_test(NAME UiSelfCheck COMMAND JMSignatureSnapshot)
