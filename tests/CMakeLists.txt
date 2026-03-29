add_executable(fastlog_tests test_fastlog.cpp)
set_property(TARGET fastlog_tests PROPERTY CXX_SCAN_FOR_MODULES OFF)
target_link_libraries(fastlog_tests PRIVATE fastlog::fastlog)
add_test(NAME fastlog_tests COMMAND fastlog_tests)
