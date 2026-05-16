cmake_minimum_required(VERSION 3.22)

project(BandForge
    VERSION 0.1.0
    DESCRIPTION "An original Ubuntu-native music creation workstation"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BANDFORGE_BUILD_APP "Build the JUCE desktop application when JUCE is available" ON)
option(BANDFORGE_FETCH_JUCE "Download JUCE with FetchContent when it is not installed" OFF)
option(BANDFORGE_BUILD_TESTS "Build core unit tests" ON)
option(BANDFORGE_BUILD_CLI "Build the core-only command line helper" ON)

add_library(bandforge_core
    src/core/AudioEngine.cpp
    src/core/Exporter.cpp
    src/core/InstrumentHost.cpp
    src/core/Json.cpp
    src/core/Library.cpp
    src/core/Midi.cpp
    src/core/Mixer.cpp
    src/core/Model.cpp
    src/core/PluginHost.cpp
    src/core/Timeline.cpp
    src/core/Transport.cpp
    src/core/Undo.cpp)

target_include_directories(bandforge_core PUBLIC src)

target_compile_features(bandforge_core PUBLIC cxx_std_20)
target_compile_definitions(bandforge_core PUBLIC
    BANDFORGE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

if(BANDFORGE_BUILD_CLI)
    add_executable(bandforge_cli tools/bandforge_cli.cpp)
    target_link_libraries(bandforge_cli PRIVATE bandforge_core)
endif()

if(BANDFORGE_BUILD_TESTS)
    enable_testing()
    add_executable(bandforge_core_tests tests/CoreTests.cpp)
    target_link_libraries(bandforge_core_tests PRIVATE bandforge_core)
    add_test(NAME bandforge_core_tests COMMAND bandforge_core_tests)
endif()

if(BANDFORGE_BUILD_APP)
    set(BANDFORGE_HAVE_JUCE OFF)

    if(BANDFORGE_FETCH_JUCE)
        include(FetchContent)
        FetchContent_Declare(
            JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.6)
        FetchContent_MakeAvailable(JUCE)
        set(BANDFORGE_HAVE_JUCE ON)
    else()
        find_package(JUCE CONFIG QUIET)
        if(JUCE_FOUND)
            set(BANDFORGE_HAVE_JUCE ON)
        elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt")
            add_subdirectory(third_party/JUCE)
            set(BANDFORGE_HAVE_JUCE ON)
        endif()
    endif()

    if(BANDFORGE_HAVE_JUCE)
        juce_add_gui_app(BandForge
            PRODUCT_NAME "BandForge"
            COMPANY_NAME "BandForge"
            BUNDLE_ID "studio.bandforge.app")

        target_sources(BandForge PRIVATE
            src/app/Main.cpp
            src/app/MainComponent.cpp)

        target_link_libraries(BandForge PRIVATE
            bandforge_core
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_dsp
            juce::juce_gui_extra)

        target_compile_definitions(BandForge PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_PLUGINHOST_VST3=1
            JUCE_PLUGINHOST_LV2=1
            JUCE_APPLICATION_NAME_STRING="BandForge"
            JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
    else()
        message(WARNING "JUCE was not found; building core library/tests only. Set BANDFORGE_FETCH_JUCE=ON or provide third_party/JUCE.")
    endif()
endif()
