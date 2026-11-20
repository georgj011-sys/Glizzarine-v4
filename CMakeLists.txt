cmake_minimum_required(VERSION 3.22)
project(GlizzarineHarmonizer VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

option(GLZH_TESTS "Build the test tools in dev-tests" OFF)
option(GLZH_SANITIZE "Build with AddressSanitizer (+ UBSan on clang/gcc)" OFF)
option(GLZH_TSAN "Build with ThreadSanitizer (clang/gcc)" OFF)
if(GLZH_TSAN)
    add_compile_options(-fsanitize=thread -g)
    add_link_options(-fsanitize=thread)
endif()
if(GLZH_SANITIZE)
    if(MSVC)
        add_compile_options(/fsanitize=address /Zi)
    else()
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# Audio effect that also takes MIDI: the voice comes in as audio, the keyboard as MIDI.
juce_add_plugin(GlizzarineHarmonizer
    COMPANY_NAME                "NebulaAudio"
    PLUGIN_MANUFACTURER_CODE    Nbla
    PLUGIN_CODE                 Glzh
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "Glizzarine Harmonizer"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           TRUE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    VST3_CATEGORIES             Fx)

target_sources(GlizzarineHarmonizer PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp)

target_compile_definitions(GlizzarineHarmonizer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(GlizzarineHarmonizer
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Test tools (-DGLZH_TESTS=ON). The test sources may sit in dev-tests/ or directly next to this file
# (flat upload to GitHub works too). The programs always land in <build>/dev-tests[/Release].
if(GLZH_TESTS)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/dev-tests/qa.cpp)
        set(GLZH_TESTDIR ${CMAKE_CURRENT_SOURCE_DIR}/dev-tests)
    else()
        set(GLZH_TESTDIR ${CMAKE_CURRENT_SOURCE_DIR})
    endif()

    # JUCE modules + plugin code compiled once into a static library, shared by all tools
    add_library(glzh_testlib STATIC PluginProcessor.cpp PluginEditor.cpp)
    target_include_directories(glzh_testlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${GLZH_TESTDIR})
    target_compile_definitions(glzh_testlib PUBLIC
        GLZH_NO_CFG=1
        JUCE_STANDALONE_APPLICATION=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_Name="Glizzarine Harmonizer"
        JucePlugin_Build_VST3=0
        JucePlugin_Build_Standalone=0)
    target_link_libraries(glzh_testlib PUBLIC juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)

    foreach(t qa fuzz screenshots bounce soak uiclick)
        add_executable(${t} ${GLZH_TESTDIR}/${t}.cpp)
        target_link_libraries(${t} PRIVATE glzh_testlib)
        set_target_properties(${t} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/dev-tests)
    endforeach()
    # engine-only tools (no JUCE)
    foreach(t t10 stress pitch bench2 glide)
        add_executable(${t} ${GLZH_TESTDIR}/${t}.cpp)
        target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        set_target_properties(${t} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/dev-tests)
    endforeach()
endif()
