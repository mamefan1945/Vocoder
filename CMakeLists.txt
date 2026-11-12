cmake_minimum_required(VERSION 3.22)

project(Vocoder VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

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

juce_add_gui_app(Vocoder
    PRODUCT_NAME "Vocoder"
    COMPANY_NAME "Personal Audio Tools"
    BUNDLE_ID "com.personalaudiotools.vocoder"
)

juce_generate_juce_header(Vocoder)

target_sources(Vocoder PRIVATE
    Source/Main.cpp
    Source/MainComponent.h
    Source/MainComponent.cpp
    Source/CarrierSynth.h
    Source/CarrierSynth.cpp
    Source/SoundFontEngine.cpp
    Source/HarmonizerEngine.h
    Source/HarmonizerEngine.cpp
    Source/VocoderEngine.cpp
)

target_compile_definitions(Vocoder PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(Vocoder PRIVATE
    juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_dsp
    FluidSynth::libfluidsynth
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

if(MSVC)
    target_compile_options(Vocoder PRIVATE /W4 /permissive-)
endif()


add_custom_command(TARGET Vocoder POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Vocoder>/SoundFonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${GENERALUSER_SF2}" "$<TARGET_FILE_DIR:Vocoder>/SoundFonts/GeneralUser-GS.sf2"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${GENERALUSER_LICENSE}" "$<TARGET_FILE_DIR:Vocoder>/SoundFonts/GeneralUser-GS-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSES/FluidSynth-LGPL-2.1.txt" "$<TARGET_FILE_DIR:Vocoder>/FluidSynth-LGPL-2.1.txt"
)
