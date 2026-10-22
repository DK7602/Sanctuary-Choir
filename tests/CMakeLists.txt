add_executable(sanctuary_tests
    TestMain.cpp
    TestSignals.cpp)
target_link_libraries(sanctuary_tests PRIVATE sanctuary_core)
add_test(NAME sanctuary_tests COMMAND sanctuary_tests)
