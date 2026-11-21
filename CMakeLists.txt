cmake_minimum_required(VERSION 3.22)

# Universal binary on macOS (Apple Silicon + Intel)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project(SpaceOpera VERSION 1.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

option(RIOT_COPY_AFTER_BUILD "Copy plugins to the user plug-in folders after building" ON)

if(APPLE)
    set(PLUGIN_FORMATS AU VST3 Standalone)
else()
    set(PLUGIN_FORMATS VST3 Standalone)
endif()

juce_add_plugin(SpaceOpera
    COMPANY_NAME                "RiotAudio"
    BUNDLE_ID                   "com.riotaudio.spaceopera"
    PLUGIN_MANUFACTURER_CODE    Riot
    PLUGIN_CODE                 Spop
    FORMATS                     ${PLUGIN_FORMATS}
    PRODUCT_NAME                "SpaceOpera"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    AU_MAIN_TYPE                kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD     ${RIOT_COPY_AFTER_BUILD})

juce_add_binary_data(SpaceOperaAssets SOURCES
    Assets/background.jpg
    Assets/astronaut.png
    Assets/shuttle.png
    Assets/logo.png
    Assets/Michroma-Regular.ttf)

target_sources(SpaceOpera PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(SpaceOpera PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(SpaceOpera
    PRIVATE
        SpaceOperaAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
