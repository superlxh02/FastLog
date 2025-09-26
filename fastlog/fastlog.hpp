#pragma once
#include "fastlog/detail/logger.hpp"
#include "fastlog/detail/loglevel.hpp"
#include "fastlog/detail/manager.hpp"
#include <filesystem>

namespace fastlog {
// 控制台日志器，单例，全局唯一
inline auto &console =
    detail::util::Singleton<detail::ConsoleLogger>::instance();
static inline void set_log_level(LogLevel level) { console.set_level(level); }
} // namespace fastlog

namespace fastlog::file {

// 文件日志器，单例，全局唯一
using FileLogger = detail::FileLogger;
inline auto &filelogger =
    detail::util::Singleton<detail::FileLoggerManager>::instance();
// 工厂函数，创建文件日志器
static inline auto make_logger(const std::string &logger_name,
                               std::filesystem::path log_path = "")
    -> detail::FileLogger & {

  if (log_path.empty()) {
    log_path = std::filesystem::path{logger_name};
  }
  if (!log_path.has_filename()) {
    log_path.append(logger_name);
  }
  return filelogger.make_logger(logger_name, log_path);
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

// 设置输出日志的最低级别

} // namespace fastlog::file