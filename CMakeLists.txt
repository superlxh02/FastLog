cmake_minimum_required(VERSION 3.20)
project(fastlog VERSION 0.2.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# 固定为现代 C++23，并关闭编译器扩展，保证示例、测试和消费方行为一致。
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# 未显式指定构建类型时，默认提供带调试信息的发布配置。
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# 项目开关：示例、测试和可选栈追踪能力默认按工程开发场景开启。
option(FASTLOG_BUILD_EXAMPLES "Build FastLog examples" ON)
option(FASTLOG_BUILD_TESTS "Build FastLog tests" ON)
option(FASTLOG_BUILD_BENCHMARKS "Build FastLog benchmarks" OFF)
option(FASTLOG_WITH_CPPTRACE "Enable cpptrace-powered stacktraces" ON)
option(FASTLOG_USE_STD_FORMAT "Use std::format backend" ON)
option(FASTLOG_FETCH_CPPTRACE "Fetch cpptrace if it is not available locally" ON)

if(NOT FASTLOG_USE_STD_FORMAT)
  message(FATAL_ERROR "FASTLOG currently requires FASTLOG_USE_STD_FORMAT=ON.")
endif()

# FastLog 以 INTERFACE 目标方式发布，消费方只需链接 fastlog::fastlog 即可。
add_library(fastlog INTERFACE)
add_library(fastlog::fastlog ALIAS fastlog)

target_compile_features(fastlog INTERFACE cxx_std_23)
target_include_directories(fastlog
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_definitions(fastlog INTERFACE FASTLOG_USE_STD_FORMAT=1)

# 栈追踪能力是可选依赖：优先复用本地安装，其次按需拉取 cpptrace。
if(FASTLOG_WITH_CPPTRACE)
  find_package(cpptrace CONFIG QUIET)

  if(NOT cpptrace_FOUND AND FASTLOG_FETCH_CPPTRACE)
    include(FetchContent)
    set(CPPTRACE_DISABLE_CXX_20_MODULES ON CACHE BOOL "Disable cpptrace C++ modules" FORCE)
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF CACHE BOOL "Disable module scanning" FORCE)
    set(FETCHCONTENT_UPDATES_DISCONNECTED_CPPTRACE ON CACHE BOOL "Avoid updating cpptrace on every configure" FORCE)
    FetchContent_Declare(
      cpptrace
      GIT_REPOSITORY https://github.com/jeremy-rifkin/cpptrace.git
      GIT_TAG v1.0.4
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(cpptrace)
  endif()

  if(TARGET cpptrace::cpptrace)
    target_link_libraries(fastlog INTERFACE cpptrace::cpptrace)
    target_compile_definitions(fastlog INTERFACE FASTLOG_WITH_CPPTRACE=1)
  elseif(TARGET cpptrace)
    target_link_libraries(fastlog INTERFACE cpptrace)
    target_compile_definitions(fastlog INTERFACE FASTLOG_WITH_CPPTRACE=1)
  else()
    message(WARNING "cpptrace was not found. FastLog will build without stacktrace support.")
  endif()
endif()

# 示例与测试都通过链接 fastlog::fastlog 继承相同的头文件与编译特性。
if(FASTLOG_BUILD_EXAMPLES)
  add_subdirectory(example)
endif()

if(FASTLOG_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# 安装头文件与导出目标，供 find_package(fastlog CONFIG REQUIRED) 使用。
install(TARGETS fastlog EXPORT fastlogTargets)
install(DIRECTORY
  ${CMAKE_CURRENT_SOURCE_DIR}/fastlog
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING
  PATTERN "*.hpp"
)

install(EXPORT fastlogTargets
  FILE fastlogTargets.cmake
  NAMESPACE fastlog::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fastlog
)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/fastlogConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/fastlogConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fastlog
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/fastlogConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/fastlogConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/fastlogConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fastlog
)
