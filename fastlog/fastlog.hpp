#pragma once
#include "fastlog/detail/logger.hpp"
#include "fastlog/detail/loglevel.hpp"
#include "fastlog/detail/manager.hpp"
#include <cpptrace/cpptrace.hpp>
#include <cpptrace/from_current.hpp>
#include <filesystem>

namespace fastlog {
// 对外暴露的日志细节模式：
// Compact: 时间 + 级别 + 线程 + 消息
// Full   : 额外输出源文件完整路径和行号
using LogDetailMode = detail::LogDetailMode;

// 文件 logger 的可选配置项
using FileLoggerOptions = detail::FileLoggerOptions;

// 控制台日志器，单例，全局唯一
inline auto &console =
    detail::util::Singleton<detail::ConsoleLogger>::instance();

static inline void set_consolelog_level(LogLevel level) {
  console.set_level(level);
}

// 设置控制台输出模式：
// 默认 Compact，需要定位源码时可切换为 Full
static inline void set_consolelog_detail_mode(LogDetailMode mode) {
  console.set_detail_mode(mode);
}

[[nodiscard]]
// 获取当前线程的调用栈快照，常用于诊断或主动输出调试信息
static inline auto current_stacktrace(std::size_t skip = 0) -> std::string {
  return cpptrace::generate_trace(skip + 1).to_string(false);
}

[[nodiscard]]
// 在 catch(...) 语境下提取当前异常的调用栈
static inline auto current_exception_stacktrace() -> std::string {
  return cpptrace::from_current_exception().to_string(false);
}

// 对外导出常用的 traced exception 类型，方便直接使用 fastlog 命名空间
using traced_exception = cpptrace::exception;
using runtime_error = cpptrace::runtime_error;
using logic_error = cpptrace::logic_error;
using invalid_argument = cpptrace::invalid_argument;
using out_of_range = cpptrace::out_of_range;
} // namespace fastlog

namespace fastlog::file {

using FileLogger = detail::FileLogger;

// 文件日志器管理器，单例，全局唯一
inline auto &fileloggermanager =
    detail::util::Singleton<detail::FileLoggerManager>::instance();
// 工厂函数，创建文件日志器
static inline auto make_logger(const std::string &logger_name,
                               std::filesystem::path log_path = "",
                               const FileLoggerOptions &options = {})
    -> detail::FileLogger & {

  if (log_path.empty()) {
    log_path = std::filesystem::path{logger_name};
  }
  if (!log_path.has_filename()) {
    log_path.append(logger_name);
  }
  return fileloggermanager.make_logger(logger_name, log_path, options);
}
// 删除文件日志器
static inline void delete_logger(const std::string &logger_name) {
  fileloggermanager.delete_logger(logger_name);
}

// 获取文件日志器
[[nodiscard]]
static inline auto get_logger(const std::string &logger_name) {
  return fileloggermanager.get_logger(logger_name);
}

// 单独设置某个文件 logger 的输出模式
static inline void set_detail_mode(FileLogger &logger, LogDetailMode mode) {
  logger.set_detail_mode(mode);
}

// 单独设置某个文件 logger 的最小日志级别
static inline void set_level(FileLogger &logger, LogLevel level) {
  logger.set_level(level);
}

// 单独设置某个文件 logger 的单文件最大大小
static inline void set_max_file_size(FileLogger &logger,
                                     std::size_t max_file_size) {
  logger.set_max_file_size(max_file_size);
}

// 主动刷新文件 logger，将当前缓冲区内容尽快落盘
static inline void flush(FileLogger &logger) { logger.flush(); }

} // namespace fastlog::file

namespace fastlog {

// 同时输出异常到控制台和所有已注册的文件 logger
static inline void log_exception_to_all(
    const std::exception &ex,
    std::source_location loc = std::source_location::current()) {
  console.exception(ex, loc);
  file::fileloggermanager.for_each_logger([&](auto &logger) {
    logger.exception(ex, loc);
    logger.flush();
  });
}

// 同时输出当前异常到控制台和所有已注册的文件 logger
// 适合在 catch(...) 场景下统一记录异常信息
static inline void log_current_exception_to_all(
    std::string_view prefix = "Unhandled exception",
    std::source_location loc = std::source_location::current()) {
  console.current_exception(prefix, loc);
  file::fileloggermanager.for_each_logger([&](auto &logger) {
    logger.current_exception(prefix, loc);
    logger.flush();
  });
}

} // namespace fastlog
