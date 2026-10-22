add_executable(sanctuary_render
    RenderMain.cpp
    ../../tests/TestSignals.cpp)
target_include_directories(sanctuary_render PRIVATE ../../tests)
target_link_libraries(sanctuary_render PRIVATE sanctuary_core)
