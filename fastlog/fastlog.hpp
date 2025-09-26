#pragma once
#include "fastlog/detail/logger.hpp"
#include "fastlog/detail/manager.hpp"

namespace fastlog {
// 控制台日志器，单例，全局唯一
inline auto &console =
    detail::util::Singleton<detail::ConsoleLogger>::instance();

} // namespace fastlog

namespace fastlog::file {

// 文件日志器，单例，全局唯一
using FileLogger = detail::FileLogger;
inline auto &filelogger =
    detail::util::Singleton<detail::FileLoggerManager>::instance();
// 工厂函数，创建文件日志器
static inline auto make_logger(const std::string &logger_name,
                               const std::string &file_base_name = "",
                               const std::string &log_dir = "")
    -> detail::FileLogger & {
  // 文件名
  std::string filename = file_base_name;
  // 如果有log_dir，文件名 = log_dir + "/" + file_base_name
  if (!log_dir.empty()) {
    #ifdef _WIN32
   filename = log_dir + "\\" + file_base_name; 
    #endif 
    filename = log_dir + "/" + file_base_name;
  }
  return filelogger.make_logger(
      logger_name, file_base_name.empty() ? logger_name : filename);
}
// 删除文件日志器
static inline void delete_logger(const std::string &logger_name) {
  filelogger.delete_logger(logger_name);
}

// 获取文件日志器
[[nodiscard]]
static inline auto get_logger(const std::string &logger_name) {
  return filelogger.get_logger(logger_name);
}
} // namespace fastlog::file