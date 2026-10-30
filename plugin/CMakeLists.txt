# Sanctuary Choir audio plugin (JUCE).

set(SANCTUARY_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND SANCTUARY_FORMATS AU)
endif()

juce_add_plugin(SanctuaryChoir
    PRODUCT_NAME "Sanctuary Choir"
    COMPANY_NAME "Sanctuary Audio"
    COMPANY_WEBSITE "https://github.com/DK7602/Sanctuary-Choir"
    BUNDLE_ID com.sanctuaryaudio.sanctuarychoir
    PLUGIN_MANUFACTURER_CODE Snct
    PLUGIN_CODE SChr
    VERSION ${PROJECT_VERSION}
    FORMATS ${SANCTUARY_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    VST3_CATEGORIES Fx Vocals
    # The app icon (the standalone app on macOS and Windows), cut from design/icon/icon-artwork.webp
    # by tools/icon/make_icon.py.
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon-1024.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon-256.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Sanctuary Choir listens to your voice to turn it into a choir."
    NEEDS_WEB_BROWSER TRUE
    NEEDS_WEBVIEW2 TRUE
    NEEDS_CURL FALSE)

# Package the web UI as a zip that is embedded in the binary.
file(GLOB_RECURSE SANCTUARY_UI_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/ui/*")
set(SANCTUARY_UI_ZIP "${CMAKE_CURRENT_BINARY_DIR}/sanctuary_ui.zip")
add_custom_command(
    OUTPUT "${SANCTUARY_UI_ZIP}"
    COMMAND ${CMAKE_COMMAND} -E rm -f "${SANCTUARY_UI_ZIP}"
    COMMAND ${CMAKE_COMMAND} -E tar cf "${SANCTUARY_UI_ZIP}" --format=zip .
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/ui"
    DEPENDS ${SANCTUARY_UI_FILES}
    COMMENT "Packaging Sanctuary Choir web UI")
add_custom_target(SanctuaryChoirUIZip DEPENDS "${SANCTUARY_UI_ZIP}")

juce_add_binary_data(SanctuaryChoirAssets
    HEADER_NAME SanctuaryAssets.h
    NAMESPACE SanctuaryAssets
    SOURCES "${SANCTUARY_UI_ZIP}")
add_dependencies(SanctuaryChoirAssets SanctuaryChoirUIZip)

# The venues' measured rooms (see tools/rooms), built into the plug-in.
file(GLOB SANCTUARY_ROOM_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/rooms/*.ogg")
juce_add_binary_data(SanctuaryRooms
    HEADER_NAME SanctuaryRoomsData.h
    NAMESPACE SanctuaryRoomsData
    SOURCES ${SANCTUARY_ROOM_FILES} "${PROJECT_SOURCE_DIR}/rooms/SOURCES.json")

target_sources(SanctuaryChoir PRIVATE
    Source/ChoirVoices.cpp
    Source/LearnSession.cpp
    Source/MeasuredRooms.cpp
    Source/Parameters.cpp
    Source/PluginEditor.cpp
    Source/PluginProcessor.cpp
    Source/Presets.cpp
    Source/TakeExport.cpp)

target_compile_definitions(SanctuaryChoir PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

# Which commit this build is (shown in Settings and the manual), so a report can be matched to
# its code. CI passes it (-DSANCTUARY_BUILD_ID=...); otherwise it is read when CMake configures,
# with "+" when the checkout has uncommitted changes. A local build after new commits needs a
# re-configure to update it.
if(NOT SANCTUARY_BUILD_ID)
    execute_process(COMMAND git rev-parse --short HEAD
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE SANCTUARY_BUILD_ID
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(SANCTUARY_BUILD_ID)
        execute_process(COMMAND git diff-index --quiet HEAD --
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            RESULT_VARIABLE SANCTUARY_DIRTY
            ERROR_QUIET)
        if(SANCTUARY_DIRTY)
            string(APPEND SANCTUARY_BUILD_ID "+")
        endif()
    else()
        set(SANCTUARY_BUILD_ID "local")
    endif()
endif()
target_compile_definitions(SanctuaryChoir PRIVATE SANCTUARY_BUILD_ID="${SANCTUARY_BUILD_ID}")

target_link_libraries(SanctuaryChoir
    PRIVATE
        sanctuary_core
        SanctuaryChoirAssets
        SanctuaryRooms
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_gui_extra
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Tests for the LEARN / RENDER session logic (needs JUCE, so it lives with the plug-in).
if(SANCTUARY_BUILD_TESTS)
    juce_add_console_app(SanctuarySessionTests PRODUCT_NAME "SanctuarySessionTests")
    target_sources(SanctuarySessionTests PRIVATE
        Tests/SessionTests.cpp
        Source/ChoirVoices.cpp
        Source/LearnSession.cpp
        Source/MeasuredRooms.cpp
        Source/TakeExport.cpp
        "${PROJECT_SOURCE_DIR}/tests/TestSignals.cpp")
    target_include_directories(SanctuarySessionTests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_compile_definitions(SanctuarySessionTests PRIVATE
        SANCTUARY_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(SanctuarySessionTests PRIVATE
        sanctuary_core
        SanctuaryRooms
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_recommended_config_flags)
    add_test(NAME sanctuary_session_tests COMMAND SanctuarySessionTests)
endif()
