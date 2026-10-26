# MIREKU — JUCE 8 instrument plugin (VST3 + AU + Standalone) + console tests.
# Configure:  cmake -B build -G "Visual Studio 18 2026" -A x64
# Build:      cmake --build build --config Release
cmake_minimum_required(VERSION 3.24)
project(Mireku VERSION 1.0.0 LANGUAGES C CXX)

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

# ------------------------------------------- Audio device backends (R6 issue 1)
# The client could not see his Behringer UMC 404HD outside DirectSound. Two
# separate things were wrong; this block fixes the build half of both.
#
#  * WASAPI: JUCE's default already builds shared, EXCLUSIVE and low-latency
#    shared modes (juce_AudioDeviceManager.cpp::createAudioDeviceTypes), so the
#    exclusive-mode type was there — it was never *pinned*, so the app opened
#    whatever Windows called the default endpoint. We now state the flags
#    explicitly (so a future JUCE default change cannot silently drop exclusive
#    mode) and the standalone prefers exclusive mode at first launch. No SDK.
#
#  * ASIO: JUCE 8.0.15 SHIPS the ASIO interface headers itself
#    (modules/juce_audio_devices/native/asio/{asio,asiosys,iasiodrv}.h), so
#    enabling ASIO needs no download and adds nothing to THIS repository.
#    Steinberg's licence terms for those files still apply to the binary — see
#    README.md. If the ASIO SDK is ever unbundled from JUCE, or a different SDK
#    version is wanted, point JM_ASIO_SDK_DIR at a local SDK's `common`
#    directory and JUCE's external-SDK path is used instead.
option(JM_ASIO "Compile ASIO device support into the standalone" ON)
set(JM_ASIO_SDK_DIR "" CACHE PATH
    "Optional: directory containing iasiodrv.h from a locally installed Steinberg ASIO SDK. \
Leave empty to use the ASIO headers bundled with JUCE. The SDK is NOT redistributable and is \
never committed to this repository.")

# Where did JUCE come from? (add_subdirectory sets JUCE_SOURCE_DIR, FetchContent juce_SOURCE_DIR)
set(JM_JUCE_DIR "${JUCE_SOURCE_DIR}")
if(NOT JM_JUCE_DIR)
    set(JM_JUCE_DIR "${juce_SOURCE_DIR}")
endif()

set(JM_AUDIO_BACKEND_DEFS
    JUCE_WASAPI=1              # shared + EXCLUSIVE + low-latency-shared device types
    JUCE_DIRECTSOUND=1)        # kept: it is the fallback that was working for the client

set(JM_ASIO_ORIGIN "disabled")
if(JM_ASIO AND WIN32)
    if(JM_ASIO_SDK_DIR)
        if(EXISTS "${JM_ASIO_SDK_DIR}/iasiodrv.h")
            list(APPEND JM_AUDIO_BACKEND_DEFS JUCE_ASIO=1 JUCE_ASIO_USE_EXTERNAL_SDK=1)
            set(JM_ASIO_INCLUDE_DIR "${JM_ASIO_SDK_DIR}")
            set(JM_ASIO_ORIGIN "external SDK at ${JM_ASIO_SDK_DIR}")
        else()
            message(WARNING
                "JM_ASIO_SDK_DIR is set to '${JM_ASIO_SDK_DIR}' but iasiodrv.h is not there. "
                "Point it at the SDK's 'common' directory. Falling back to JUCE's bundled headers.")
            set(JM_ASIO_SDK_DIR "")
        endif()
    endif()
    if(NOT JM_ASIO_SDK_DIR)
        if(EXISTS "${JM_JUCE_DIR}/modules/juce_audio_devices/native/asio/iasiodrv.h")
            list(APPEND JM_AUDIO_BACKEND_DEFS JUCE_ASIO=1)
            set(JM_ASIO_ORIGIN "ASIO headers bundled with JUCE (no download required)")
        else()
            set(JM_ASIO_ORIGIN
                "NOT AVAILABLE — this JUCE has no bundled ASIO headers. Download the Steinberg \
ASIO SDK and configure with -DJM_ASIO_SDK_DIR=<sdk>/common")
        endif()
    endif()
endif()
message(STATUS "MIREKU audio backends: WASAPI (shared/exclusive/low-latency) + DirectSound")
message(STATUS "MIREKU ASIO support    : ${JM_ASIO_ORIGIN}")

# ------------------------------------------------------------------ Plugin
# PRODUCT RENAME (2026-10-26): "JM Signature" -> MIREKU, at the client's
# instruction ("NAME OF APP CHANGE TO MIREKU, no more JM Signature Library").
#
#  * COMPANY_NAME is DELIBERATELY UNCHANGED. The client renamed the product,
#    not the company, and PLUGIN_MANUFACTURER_CODE/BUNDLE_ID prefix follow the
#    company. See DECISIONS-signature.md — this is an open question for him.
#  * PLUGIN_CODE changes JmSg -> Mrku. THIS CHANGES THE PLUGIN'S IDENTITY: a
#    DAW project that already references the VST3/AU will no longer find it,
#    because hosts key plugins on the manufacturer+plugin code and the VST3
#    unique id derived from them, not on the file name. That is cheap now and
#    expensive after shipping, which is why it is being done in the same pass
#    as the rename rather than later.
juce_add_plugin(Mireku
    COMPANY_NAME              "JM Studios"
    PLUGIN_MANUFACTURER_CODE  JmSt
    PLUGIN_CODE               Mrku
    PRODUCT_NAME              "MIREKU"
    BUNDLE_ID                 com.jmstudios.mireku
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    FORMATS                   VST3 AU Standalone   # AU is ignored by JUCE on non-Apple platforms
    VST3_CATEGORIES           Instrument)

target_sources(Mireku PRIVATE
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
target_link_libraries(Mireku PRIVATE JMAssets)

target_include_directories(Mireku PRIVATE src "${JMI_FORMAT_INCLUDE}")

target_compile_definitions(Mireku PUBLIC
    JM_APP_VERSION="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    # Client review round 3 (B/E): we own the standalone app + window, so the
    # JUCE "Options" title-bar button is gone and all three window controls
    # (minimise / maximise / close) are available. See src/plugin/StandaloneApp.cpp.
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
    # R6(1): device backends. These must be PUBLIC/on the shared-code target —
    # that is where JUCE compiles juce_audio_devices.
    ${JM_AUDIO_BACKEND_DEFS})
if(JM_ASIO_INCLUDE_DIR)
    target_include_directories(Mireku PUBLIC "${JM_ASIO_INCLUDE_DIR}")
endif()

# The custom standalone app lives ONLY in the Standalone target.
if(TARGET Mireku_Standalone)
    target_sources(Mireku_Standalone PRIVATE src/plugin/StandaloneApp.cpp)
    target_include_directories(Mireku_Standalone PRIVATE
        src "${JMI_FORMAT_INCLUDE}"
        "${CMAKE_CURRENT_BINARY_DIR}/juce_binarydata_JMAssets/JuceLibraryCode")
endif()

target_link_libraries(Mireku PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------ Tests
juce_add_console_app(MirekuTests PRODUCT_NAME "MirekuTests")

target_sources(MirekuTests PRIVATE tests/EngineTests.cpp)
target_include_directories(MirekuTests PRIVATE src "${JMI_FORMAT_INCLUDE}")
target_compile_definitions(MirekuTests PRIVATE
    JM_APP_VERSION="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JM_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../testdata")
target_link_libraries(MirekuTests PRIVATE
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_events
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------- V2 acceptance self-check
# Offscreen 1000x720 render + control-centre measurement vs specs/screen-layouts.json.
juce_add_console_app(MirekuSnapshot PRODUCT_NAME "MirekuSnapshot")
target_sources(MirekuSnapshot PRIVATE
    tests/SnapshotCheck.cpp
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginEditor.cpp)
target_include_directories(MirekuSnapshot PRIVATE src "${JMI_FORMAT_INCLUDE}"
    "${CMAKE_CURRENT_BINARY_DIR}/juce_binarydata_JMAssets/JuceLibraryCode")
target_compile_definitions(MirekuSnapshot PRIVATE
    JM_APP_VERSION="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JM_SNAPSHOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/build/snapshots"
    # AUTHORING MODE: engine/Authoring.h mirrors preset.schema.json in C++ so a
    # save can be validated BEFORE it touches a bundle. The self-check compares
    # the mirror against this file, so the two cannot drift apart unnoticed.
    JM_JMI_SCHEMA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../packages/jmi-format/schema"
    JM_LIBRARY_TESTDATA="${CMAKE_CURRENT_SOURCE_DIR}/../../testdata")
target_link_libraries(MirekuSnapshot PRIVATE
    JMAssets
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ------------------------------------- Validation suite (standards §4.1 / §5)
# The null test, the sample-rate/buffer-size matrix and the audio regression
# harness. They exercise the PROCESSOR (not just the engine objects), so this
# target compiles the plugin's shared code exactly as MirekuSnapshot does.
juce_add_console_app(MirekuValidation PRODUCT_NAME "MirekuValidation")
target_sources(MirekuValidation PRIVATE
    tests/ValidationTests.cpp
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginEditor.cpp)
target_include_directories(MirekuValidation PRIVATE src "${JMI_FORMAT_INCLUDE}"
    "${CMAKE_CURRENT_BINARY_DIR}/juce_binarydata_JMAssets/JuceLibraryCode")
target_compile_definitions(MirekuValidation PRIVATE
    JM_APP_VERSION="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JM_REF_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/refs"
    # NEUTRAL DEFAULTS acceptance test (--neutral): the default library it
    # renders against. The test only READS bundles, and the CTest entry must
    # not depend on which instruments a particular machine happens to have.
    JM_LIBRARY_TESTDATA="${CMAKE_CURRENT_SOURCE_DIR}/../../testdata")
target_link_libraries(MirekuValidation PRIVATE
    JMAssets
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ------------------------------------------- Device inventory tool (R6/1)
# Deliberately NOT a CTest: its output depends on what hardware is plugged in.
# Run it and send the output when a device cannot be found in the app.
juce_add_console_app(MirekuDevices PRODUCT_NAME "MirekuDevices")
target_sources(MirekuDevices PRIVATE tests/DeviceCheck.cpp)
target_compile_definitions(MirekuDevices PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    ${JM_AUDIO_BACKEND_DEFS})
if(JM_ASIO_INCLUDE_DIR)
    target_include_directories(MirekuDevices PRIVATE "${JM_ASIO_INCLUDE_DIR}")
endif()
target_link_libraries(MirekuDevices PRIVATE
    juce::juce_audio_devices
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

enable_testing()
add_test(NAME EngineTests COMMAND MirekuTests)
# Client review round 3: the UI/arrangement self-check is a real test now, not a
# manual tool - it carries the master-section, chrome, macro and letterbox assertions.
add_test(NAME UiSelfCheck COMMAND MirekuSnapshot)
# Client review round 5: the self-check must not depend on whatever bundles the
# developer happens to have in their own "Documents/MIREKU/Library" folder
# — with only the client's one-world Piano.jmi there, three of the four Perform
# slots came up empty and the routing/audio assertions failed for no good reason.
# AUTHORING MODE: the mode defaults to AUTHORING and is read from the
# developer's own settings.json, so leaving it unset would make the self-check
# behave differently on two machines — and the pre-authoring assertions (e.g.
# "a performance preset DOES carry modulation", which looks in the USER
# performance folder) would depend on it. The suite is therefore PINNED to
# release here, exactly as it is pinned to a known library folder, and the
# authoring section flips the mode itself for the cases that are about it.
set_tests_properties(UiSelfCheck PROPERTIES
    ENVIRONMENT "JM_LIBRARY_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../../testdata;JM_AUTHORING_MODE=release")

# --------------------------------------------------- validation standards
# Three separate CTest entries out of one binary so a failure names itself in
# the CI log. NullTest and RateBufferMatrix instantiate the processor, so they
# get the same fixed library folder the UI self-check uses.
add_test(NAME NullTest          COMMAND MirekuValidation --null)
add_test(NAME RateBufferMatrix  COMMAND MirekuValidation --matrix)
add_test(NAME AudioRegression   COMMAND MirekuValidation --regression)
# NEUTRAL DEFAULTS (2026-10-26): a note rendered at the shipped defaults must
# BE the recorded audio, not an EQ'd version of it. See the long comment in
# tests/ValidationTests.cpp. Point it at a real library by hand with
#   MirekuValidation --neutral "<folder>"
add_test(NAME NeutralDefaults   COMMAND MirekuValidation --neutral)
# MIREKU rename (2026-10-26): the migration off the old product name. Runs
# entirely in a temp folder; it never touches the developer's own Documents.
add_test(NAME RenameMigration   COMMAND MirekuValidation --migration)
set_tests_properties(NullTest RateBufferMatrix PROPERTIES
    ENVIRONMENT "JM_LIBRARY_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../../testdata")
set_tests_properties(RateBufferMatrix PROPERTIES TIMEOUT 900)
