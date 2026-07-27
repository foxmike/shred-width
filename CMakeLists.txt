cmake_minimum_required(VERSION 3.22)

project(ShredWidth VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(
    SHREDWIDTH_CONFIGURE_ONLY
    "Configure the top-level project without requiring the JUCE submodule"
    OFF
)

set(
    SHREDWIDTH_XOVER_ORDER
    2
    CACHE STRING
    "Linkwitz-Riley crossover order (2 or 4)"
)
set_property(CACHE SHREDWIDTH_XOVER_ORDER PROPERTY STRINGS 2 4)

if(NOT SHREDWIDTH_XOVER_ORDER MATCHES "^(2|4)$")
    message(FATAL_ERROR "SHREDWIDTH_XOVER_ORDER must be 2 or 4")
endif()

if(SHREDWIDTH_CONFIGURE_ONLY)
    message(STATUS "ShredWidth configure-only mode enabled")
    return()
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(
        FATAL_ERROR
        "The pinned JUCE 8.0.13 submodule is missing. Populate JUCE before configuring a build."
    )
endif()

add_subdirectory(JUCE)

# clap-juce-extensions is optional at configure time. When populated at this
# path, add_subdirectory() provides clap_juce_extensions_plugin(), which creates
# the ShredWidth_CLAP target below.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/clap-juce-extensions/CMakeLists.txt")
    add_subdirectory(clap-juce-extensions EXCLUDE_FROM_ALL)
else()
    message(
        STATUS
        "clap-juce-extensions is not populated; ShredWidth_CLAP will not be generated"
    )
endif()

juce_add_plugin(ShredWidth
    PRODUCT_NAME ShredWidth
    COMPANY_NAME "nSpace Systems"
    VERSION 1.0.0
    PLUGIN_MANUFACTURER_CODE NSPC
    PLUGIN_CODE ShWd
    BUNDLE_ID com.nspacesystems.shredwidth
    LV2URI urn:nspacesystems:plugin:shredwidth
    FORMATS VST3 CLAP LV2 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
)

if(COMMAND clap_juce_extensions_plugin)
    clap_juce_extensions_plugin(
        TARGET ShredWidth
        CLAP_ID com.nspacesystems.shredwidth
        CLAP_FEATURES audio-effect stereo
    )
endif()

target_sources(ShredWidth
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/StateManager.cpp
)

target_compile_definitions(ShredWidth
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        SHREDWIDTH_XOVER_ORDER=${SHREDWIDTH_XOVER_ORDER}
)

target_link_libraries(ShredWidth
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

enable_testing()
