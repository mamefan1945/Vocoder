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
)

target_compile_definitions(Vocoder PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(Vocoder PRIVATE
    juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

if(MSVC)
    target_compile_options(Vocoder PRIVATE /W4 /permissive-)
endif()
