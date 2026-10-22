cmake_minimum_required(VERSION 3.22)

project(SanctuaryChoir VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SANCTUARY_BUILD_PLUGIN "Build the JUCE plugin (VST3/AU/Standalone)" ON)
option(SANCTUARY_BUILD_TESTS "Build the DSP unit tests" ON)
option(SANCTUARY_BUILD_TOOLS "Build the offline render command-line tool" ON)

# DSP core: plain C++, no framework dependencies.
add_subdirectory(core)

if(SANCTUARY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(SANCTUARY_BUILD_TOOLS)
    add_subdirectory(tools/render)
endif()

if(SANCTUARY_BUILD_PLUGIN)
    include(FetchContent)
    set(FETCHCONTENT_QUIET OFF)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(juce)
    add_subdirectory(plugin)
endif()
