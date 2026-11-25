cmake_minimum_required(VERSION 3.22)

project(PrismaVox VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# Signalsmith Stretch is used by the comparative harmonizer DSP tests only.
# Pin both header-only dependencies to immutable upstream revisions.
FetchContent_Declare(
    signalsmith_stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG 88061fd4eda56a2353c5d65c74aa3f4ccf76f9da
    GIT_SHALLOW FALSE
)
FetchContent_Declare(
    signalsmith_linear
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/linear.git
    GIT_TAG 7bb1c8f36794d6980080ce817c91f8da8fe56a66
    GIT_SHALLOW FALSE
)
FetchContent_GetProperties(signalsmith_stretch)
if(NOT signalsmith_stretch_POPULATED)
    FetchContent_Populate(signalsmith_stretch)
endif()
FetchContent_GetProperties(signalsmith_linear)
if(NOT signalsmith_linear_POPULATED)
    FetchContent_Populate(signalsmith_linear)
endif()

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.10
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(JUCE)

find_package(FluidSynth CONFIG REQUIRED)

set(SOUNDFONT_DIR "${CMAKE_BINARY_DIR}/SoundFonts")
file(MAKE_DIRECTORY "${SOUNDFONT_DIR}")
set(GENERALUSER_SF2 "${SOUNDFONT_DIR}/GeneralUser-GS.sf2")
set(GENERALUSER_LICENSE "${SOUNDFONT_DIR}/GeneralUser-GS-LICENSE.txt")
if(NOT EXISTS "${GENERALUSER_SF2}")
    file(DOWNLOAD
        "https://github.com/HKT0131/audio-assets/releases/download/v2.0.3/GeneralUser-GS.sf2"
        "${GENERALUSER_SF2}"
        EXPECTED_HASH SHA256=9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe
        SHOW_PROGRESS
        STATUS SOUNDFONT_DOWNLOAD_STATUS
    )
    list(GET SOUNDFONT_DOWNLOAD_STATUS 0 SOUNDFONT_DOWNLOAD_RESULT)
    if(NOT SOUNDFONT_DOWNLOAD_RESULT EQUAL 0)
        message(FATAL_ERROR "Could not download bundled GeneralUser GS SoundFont: ${SOUNDFONT_DOWNLOAD_STATUS}")
    endif()
endif()
if(NOT EXISTS "${GENERALUSER_LICENSE}")
    file(DOWNLOAD
        "https://github.com/HKT0131/audio-assets/releases/download/v2.0.3/LICENSE.txt"
        "${GENERALUSER_LICENSE}"
        EXPECTED_HASH SHA256=7b32efefdf95ce38a043799f0659853ddc00fbaa14d8c50f0aca16b9b8b405be
        STATUS SOUNDFONT_LICENSE_STATUS
    )
    list(GET SOUNDFONT_LICENSE_STATUS 0 SOUNDFONT_LICENSE_RESULT)
    if(NOT SOUNDFONT_LICENSE_RESULT EQUAL 0)
        message(FATAL_ERROR "Could not download GeneralUser GS license: ${SOUNDFONT_LICENSE_STATUS}")
    endif()
endif()

juce_add_gui_app(PrismaVox
    PRODUCT_NAME "PrismaVox"
    COMPANY_NAME "Personal Audio Tools"
    BUNDLE_ID "com.personalaudiotools.prismavox"
)

juce_generate_juce_header(PrismaVox)

# Embed the original full-resolution splash artwork in the application.
juce_add_binary_data(PrismaVoxArtwork SOURCES "PrismaVox Neon Prism Audio Logo.png")

target_sources(PrismaVox PRIVATE
    Source/Main.cpp
    Source/MainComponent.h
    Source/MainComponent.cpp
    Source/CarrierSynth.h
    Source/CarrierSynth.cpp
    Source/SoundFontEngine.cpp
    Source/HarmonizerEngine.h
    Source/HarmonizerEngine.cpp
    Source/VocoderEngine.cpp
    Source/LpcVocoderEngine.h
    Source/LpcVocoderEngine.cpp
)

if(WIN32)
    target_sources(PrismaVox PRIVATE PrismaVox.rc)
endif()

target_compile_definitions(PrismaVox PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
)

target_link_libraries(PrismaVox PRIVATE
    juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_dsp
    FluidSynth::libfluidsynth
    PrismaVoxArtwork
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

if(MSVC)
    target_compile_options(PrismaVox PRIVATE /W4 /permissive-)
endif()

add_custom_command(TARGET PrismaVox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PrismaVox>/SoundFonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${GENERALUSER_SF2}" "$<TARGET_FILE_DIR:PrismaVox>/SoundFonts/GeneralUser-GS.sf2"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${GENERALUSER_LICENSE}" "$<TARGET_FILE_DIR:PrismaVox>/FluidSynth-LGPL-2.1.txt"
)

# A standalone, deterministic DSP test runner. It exercises the three
# harmonizer algorithms without requiring an audio device or GUI session.
juce_add_console_app(HarmonizerDSPTests
    PRODUCT_NAME "Harmonizer DSP Tests"
)
juce_generate_juce_header(HarmonizerDSPTests)
target_include_directories(HarmonizerDSPTests PRIVATE
    Source
    "${signalsmith_stretch_SOURCE_DIR}"
    "${signalsmith_linear_SOURCE_DIR}/include"
)
target_sources(HarmonizerDSPTests PRIVATE
    Tests/HarmonizerDSPTests.cpp
    Source/HarmonizerEngine.h
    Source/HarmonizerEngine.cpp
)
target_compile_definitions(HarmonizerDSPTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    PRISMAVOX_TEST_AUDIO_DIR="${CMAKE_SOURCE_DIR}/TestAudio"
)
target_link_libraries(HarmonizerDSPTests PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
if(MSVC)
    target_compile_options(HarmonizerDSPTests PRIVATE /W4 /permissive- /bigobj)
endif()

enable_testing()
add_test(NAME HarmonizerDSPTests COMMAND $<TARGET_FILE:HarmonizerDSPTests>)
set_tests_properties(HarmonizerDSPTests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Deterministic, device-independent vocoder signal-integrity tests.
juce_add_console_app(VocoderDSPTests
    PRODUCT_NAME "Vocoder DSP Tests"
)
juce_generate_juce_header(VocoderDSPTests)
target_include_directories(VocoderDSPTests PRIVATE Source)
target_sources(VocoderDSPTests PRIVATE
    Tests/VocoderDSPTests.cpp
    Source/VocoderEngine.h
    Source/VocoderEngine.cpp
    Source/LpcVocoderEngine.h
    Source/LpcVocoderEngine.cpp
)
target_compile_definitions(VocoderDSPTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    PRISMAVOX_TEST_AUDIO_DIR="${CMAKE_SOURCE_DIR}/TestAudio"
    PRISMAVOX_TEST_RENDER_DIR="${CMAKE_BINARY_DIR}/test-renders"
)
target_link_libraries(VocoderDSPTests PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
if(MSVC)
    target_compile_options(VocoderDSPTests PRIVATE /W4 /permissive-)
endif()

add_test(NAME VocoderDSPTests COMMAND $<TARGET_FILE:VocoderDSPTests>)

# Hosted, device-independent vocoder parameter search.
juce_add_console_app(VocoderOptimizer PRODUCT_NAME "Vocoder Parameter Optimizer")
juce_generate_juce_header(VocoderOptimizer)
target_include_directories(VocoderOptimizer PRIVATE Source)
target_sources(VocoderOptimizer PRIVATE
    Tests/VocoderOptimizer.cpp
    Source/CarrierSynth.h
    Source/CarrierSynth.cpp
    Source/VocoderEngine.h
    Source/VocoderEngine.cpp
)
target_compile_definitions(VocoderOptimizer PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    PRISMAVOX_TEST_AUDIO_DIR="${CMAKE_SOURCE_DIR}/TestAudio"
)
target_link_libraries(VocoderOptimizer PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
if(MSVC)
    target_compile_options(VocoderOptimizer PRIVATE /W4 /permissive-)
endif()
