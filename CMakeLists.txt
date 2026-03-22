cmake_minimum_required(VERSION 3.10)
project(fastlog)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_BUILD_TYPE Debug)

include(FetchContent)

set(FETCHCONTENT_SOURCE_DIR_CPPTRACE "${CMAKE_CURRENT_SOURCE_DIR}/external/cpptrace")

FetchContent_Declare(
    cpptrace
    GIT_REPOSITORY https://github.com/jeremy-rifkin/cpptrace.git
    GIT_TAG v1.0.4
    GIT_SHALLOW TRUE
)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
set(FETCHCONTENT_QUIET OFF)
set(FETCHCONTENT_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external")

FetchContent_MakeAvailable(cpptrace)

set(LIB  cpptrace::cpptrace)
# 定义文件日志路径
add_definitions(-DFILE_LOG1_PATH="${CMAKE_CURRENT_SOURCE_DIR}/logs/log1/")
add_definitions(-DFILE_LOG2_PATH="${CMAKE_CURRENT_SOURCE_DIR}/logs/log2/")
set(INC ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(example example/example.cpp)
target_include_directories(example PUBLIC ${INC})
target_link_libraries(example PRIVATE ${LIB})