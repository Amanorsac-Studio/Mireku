# Sanctuary — Amanorsac Studio. Live-service keys instrument (VST3 + AU + Standalone).
# Configure:  cmake -S apps/sanctuary -B apps/sanctuary/build -G Ninja
# Build:      cmake --build apps/sanctuary/build --config Release
#
# The UI is the HTML in resources/ui/index.html, hosted in JUCE's WebBrowserComponent
# (WebView2 on Windows, WKWebView on macOS) and wired to the engine through a
# native bridge (src/plugin/SanctuaryEditor.cpp). On Windows the WebView2 SDK
# NuGet package is required at configure time:
#     nuget install Microsoft.Web.WebView2 -Version 1.0.2903.40 -OutputDirectory <dir>
# and JUCE_WEBVIEW2_PACKAGE_LOCATION must point at <dir> (default: the user's NuGet cache).
#
# Reuses (read-only) the shared .jmi format header and JUCE checkout that live
# next to JM Signature. Nothing here writes into apps/signature.
cmake_minimum_required(VERSION 3.24)
project(Sanctuary VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# ------------------------------------------------------------------ JUCE 8
set(SANCTUARY_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../signature/external/JUCE" CACHE PATH "JUCE checkout")
if(WIN32)
    # JUCE's FindWebView2 wants a folder that CONTAINS a "*Microsoft.Web.WebView2*" directory with
    # build/native inside. The NuGet cache nests a version folder (and is lower-case), so stage a
    # copy of the newest cached package under the build tree and point JUCE at it.
    file(GLOB _wv2_headers "$ENV{USERPROFILE}/.nuget/packages/microsoft.web.webview2/*/build/native/include/WebView2.h")
    if(_wv2_headers)
        list(SORT _wv2_headers)
        list(GET _wv2_headers -1 _wv2_header)
        get_filename_component(_wv2_version_dir "${_wv2_header}/../../../.." ABSOLUTE)
        set(_wv2_staged "${CMAKE_BINARY_DIR}/webview2/Microsoft.Web.WebView2")
        if(NOT EXISTS "${_wv2_staged}/build/native/include/WebView2.h")
            file(COPY "${_wv2_version_dir}/build" DESTINATION "${_wv2_staged}")
        endif()
        set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${CMAKE_BINARY_DIR}/webview2" CACHE PATH "Folder containing Microsoft.Web.WebView2" FORCE)
        message(STATUS "Sanctuary: WebView2 SDK staged from ${_wv2_version_dir}")
    endif()
endif()
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

set(SANCTUARY_WEB_DEFS JUCE_WEB_BROWSER=1 JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1)

# ------------------------------------------------------------------ UI resource
juce_add_binary_data(SanctuaryUi HEADER_NAME SanctuaryUi.h NAMESPACE SanctuaryUi SOURCES
    resources/ui/index.html)

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
    NEEDS_WEB_BROWSER         TRUE
    NEEDS_WEBVIEW2            TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    FORMATS                   VST3 AU Standalone
    VST3_CATEGORIES           Instrument)

target_sources(Sanctuary PRIVATE
    src/plugin/SanctuaryProcessor.cpp
    src/plugin/SanctuaryEditor.cpp)

target_include_directories(Sanctuary PRIVATE src "${JMI_FORMAT_INCLUDE}")

target_compile_definitions(Sanctuary PUBLIC
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
    ${SANCTUARY_WEB_DEFS}
    ${SANCTUARY_AUDIO_DEFS})

if(TARGET Sanctuary_Standalone)
    target_sources(Sanctuary_Standalone PRIVATE src/plugin/StandaloneApp.cpp)
    target_include_directories(Sanctuary_Standalone PRIVATE src "${JMI_FORMAT_INCLUDE}"
        "${CMAKE_CURRENT_BINARY_DIR}/juce_binarydata_SanctuaryUi/JuceLibraryCode")
endif()

target_link_libraries(Sanctuary PRIVATE
    SanctuaryUi
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
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
# It compiles the processor only (the editor is web-hosted and not needed here).
juce_add_console_app(SanctuarySoak PRODUCT_NAME "SanctuarySoak")
target_sources(SanctuarySoak PRIVATE
    tests/SoakTest.cpp
    src/plugin/SanctuaryProcessor.cpp)
target_include_directories(SanctuarySoak PRIVATE src "${JMI_FORMAT_INCLUDE}")
target_compile_definitions(SanctuarySoak PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    SANCTUARY_HEADLESS=1
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
