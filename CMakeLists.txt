cmake_minimum_required(VERSION 3.22)

project(MinimalChainVST3 VERSION 1.0.0)

if(NOT DEFINED JUCE_DIR)
    message(FATAL_ERROR "JUCE_DIR is not set")
endif()

add_subdirectory("${JUCE_DIR}" JUCE)

juce_add_plugin(MinimalChainVST3
    COMPANY_NAME "MinimalAudio"
    BUNDLE_ID "com.minimalaudio.minimalchain"
    PLUGIN_MANUFACTURER_CODE MnAu
    PLUGIN_CODE MnC1
    FORMATS VST3
    PRODUCT_NAME "Minimal Chain"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(MinimalChainVST3 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

juce_generate_juce_header(MinimalChainVST3)

target_compile_features(MinimalChainVST3 PRIVATE
    cxx_std_17
)

target_compile_definitions(MinimalChainVST3 PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(MinimalChainVST3 PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
)
