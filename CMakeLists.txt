cmake_minimum_required(VERSION 3.10)
project(fastlog)
set(CMAKE_CXX_STANDARD 23)
# 定义文件日志路径
add_definitions(-DFILE_LOG1_PATH="${CMAKE_CURRENT_SOURCE_DIR}/logs/log1/")
add_definitions(-DFILE_LOG2_PATH="${CMAKE_CURRENT_SOURCE_DIR}/logs/log2/")
set(INC ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(example example/example.cpp)
target_include_directories(example PUBLIC ${INC})