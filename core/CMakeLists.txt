add_library(sanctuary_core STATIC
    src/ChoirEngine.cpp
    src/ChordDetector.cpp
    src/Effects.cpp
    src/FFT.cpp
    src/Harmonizer.cpp
    src/Learn.cpp
    src/PitchTracker.cpp
    src/Psola.cpp
    src/Lyrics.cpp
    src/SampleSet.cpp
    src/Theory.cpp
    src/VowelSynth.cpp)

target_include_directories(sanctuary_core PUBLIC include)
target_compile_features(sanctuary_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(sanctuary_core PRIVATE /W4 /fp:fast)
else()
    target_compile_options(sanctuary_core PRIVATE -Wall -Wextra -Wno-unused-parameter -ffast-math -fno-finite-math-only)
endif()
