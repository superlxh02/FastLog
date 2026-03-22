#pragma once

#include <string_view>
#include <utility>
namespace fastlog {
// 日志级别
enum class LogLevel { Trace, Debug, Info, Warn, Error, Fatal };
} // namespace fastlog
namespace fastlog::detail {
// 日志级别包装器，提供类型转换（字符串和颜色）
class LogLevelWrapper {
public:
  LogLevelWrapper(LogLevel level) : __level(level) {}

  std::string_view to_string() {
    switch (__level) {
    case LogLevel::Trace:
      return "TRACE";
    case LogLevel::Debug:
      return "DEBUG";
    case LogLevel::Info:
      return "INFO ";
    case LogLevel::Warn:
      return "WARN ";
    case LogLevel::Error:
      return "ERROR";
    case LogLevel::Fatal:
      return "FATAL";
    default:
      std::unreachable();
      return "unknown log level";
    }
  }

  std::string_view to_color() {
    switch (__level) {
    case LogLevel::Trace:
      return "\033[46m"; // cyan
    case LogLevel::Debug:
      return "\033[44m"; // blue
    case LogLevel::Info:
      return "\033[42m"; // green
    case LogLevel::Warn:
      return "\033[43m"; // yellow
    case LogLevel::Error:
      return "\033[41m"; // red
    case LogLevel::Fatal:
      return "\033[45m"; // purple
    default:
      std::unreachable();
      return "NOT DEFINE COLOR";
    }
  }

private:
  LogLevel __level;
};

[[nodiscard]]
auto reset_format() noexcept -> std::string_view {
  return "\033[0m";
}
} // namespace fastlog::detail
