add_executable(fastlog_benchmark throughput.cpp)
set_property(TARGET fastlog_benchmark PROPERTY CXX_SCAN_FOR_MODULES OFF)
target_link_libraries(fastlog_benchmark PRIVATE fastlog::fastlog)
fastlog_enable_warnings(fastlog_benchmark)

if(FASTLOG_BENCHMARK_WITH_SPDLOG)
  include(FetchContent)

  set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "Disable spdlog examples" FORCE)
  set(SPDLOG_BUILD_EXAMPLE_HO OFF CACHE BOOL "Disable spdlog header-only examples" FORCE)
  set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "Disable spdlog tests" FORCE)
  set(SPDLOG_BUILD_TESTS_HO OFF CACHE BOOL "Disable spdlog header-only tests" FORCE)
  set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "Disable spdlog benchmarks" FORCE)
  set(SPDLOG_INSTALL OFF CACHE BOOL "Disable spdlog install rules" FORCE)
  set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "Use bundled fmt for spdlog benchmark" FORCE)

  FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.17.0
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(spdlog)

  add_executable(fastlog_spdlog_benchmark compare_spdlog.cpp)
  set_property(TARGET fastlog_spdlog_benchmark PROPERTY CXX_SCAN_FOR_MODULES OFF)
  target_link_libraries(fastlog_spdlog_benchmark PRIVATE fastlog::fastlog spdlog::spdlog_header_only)
  fastlog_enable_warnings(fastlog_spdlog_benchmark)
endif()
