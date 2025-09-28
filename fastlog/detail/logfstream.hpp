#pragma once

#include "util.hpp"
#include <array>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
namespace fastlog::detail {
class logfstream {
public:
  logfstream(std::filesystem::path filepath) : __file_path(filepath) {
    // 如果文件路有父目录
    if (__file_path.has_parent_path()) {
      // 获取日志文件目录
      auto log_dir = __file_path.parent_path();
      // 如果日志目录不存在，创建目录
      if (!std::filesystem::exists(log_dir)) {
        std::filesystem::create_directories(log_dir);
      }
    }

    // 创建一个新文件
    this->create();
    // 设置文件缓冲区
    __file_stream.rdbuf()->pubsetbuf(__buffer.data(), __buffer.size());
  }

  ~logfstream() { __file_stream.close(); }

public:
  // 刷新输出流缓冲区
  void flush() { __file_stream.flush(); }

  // 设置单个文件最大大小
  void set_maxsize(std::size_t maxsize) { __file_maxsize = maxsize; }

  // 写入数据
  void write(const char *data, std::size_t size) {
    __file_stream.write(data, size);
    __file_size += size;
    // 检查文件大小是否超过最大限制
    if (__file_size > __file_maxsize) {
      create();
    }
  }

private:
  // 创建新文件
  void create() {
    auto time_str = util::get_current_time_tostring();
    if (time_str.has_value()) {
      std::filesystem::path log_path =
          std::format("{}-{}", __file_path.string(), time_str.value());
      __file_size = 0;
      if (__file_stream.is_open()) {
        __file_stream.close();
      }
      __file_stream.open(log_path, std::ios::out);
      if (!__file_stream.is_open()) {
        throw std::runtime_error("create log file failed");
      }
    }
  }

private:
  static inline constexpr std::size_t BUFFER_SIZE = 1024;

private:
  std::ofstream __file_stream{};                 // 文件输出流
  std::filesystem::path __file_path{};           // 文件路径
  std::size_t __file_maxsize{1024 * 1024 * 100}; // 单个文件最大大小
  std::array<char, BUFFER_SIZE> __buffer{};      // 文件输出流缓冲区
  std::size_t __file_size{0};                    // 当前文件大小
};
} // namespace fastlog::detail
