#pragma once

#include <string_view>
namespace fastlog {
// 日志级别
enum class LogLevel { Debug, Info, Warn, Error };
} // namespace fastlog
namespace fastlog::detail {
// 日志级别包装器，提供类型转换（字符串和颜色）
class LogLevelWrapper {
public:
  LogLevelWrapper(LogLevel level) : __level(level) {}

  LogLevel get() const { return __level; }
  std::string_view to_string() {
    switch (__level) {
    case LogLevel::Debug:
      return "DEBUG";
    case LogLevel::Info:
      return "INFO";
    case LogLevel::Warn:
      return "WARN";
    case LogLevel::Error:
      return "ERROR";
    default:
      return "Unknown";
    }
  }

  std::string_view to_color() {
    switch (__level) {
    case LogLevel::Debug:
      return "\033[97;44m"; // blue
    case LogLevel::Info:
      return "\033[97;42m"; // green
    case LogLevel::Warn:
      return "\033[90;43m"; // yellow
    case LogLevel::Error:
      return "\033[97;41m"; // red
    default:
      return "NOT COLOR";
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
