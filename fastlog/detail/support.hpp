#pragma once

#include "fastlog/detail/types.hpp"

#include <concepts>
#include <ctime>
#include <exception>
#include <format>
#include <functional>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

#ifdef FASTLOG_WITH_CPPTRACE
#include <cpptrace/cpptrace.hpp>
#include <cpptrace/from_current.hpp>
#endif

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fastlog::detail {

// 将枚举级别转换为固定文本，供 formatter 和诊断输出复用。
inline auto level_to_string(log_level level) -> std::string_view {
  switch (level) {
  case log_level::trace:
    return "TRACE";
  case log_level::debug:
    return "DEBUG";
  case log_level::info:
    return "INFO";
  case log_level::warn:
    return "WARN";
  case log_level::error:
    return "ERROR";
  case log_level::fatal:
    return "FATAL";
  case log_level::off:
    return "OFF";
  }
  return "UNKNOWN";
}

// 将日志级别转换为 ANSI 颜色码，控制台 sink 会使用这里的结果。
inline auto level_color(log_level level) -> std::string_view {
  switch (level) {
  case log_level::trace:
    return "\033[36m";
  case log_level::debug:
    return "\033[34m";
  case log_level::info:
    return "\033[32m";
  case log_level::warn:
    return "\033[33m";
  case log_level::error:
    return "\033[31m";
  case log_level::fatal:
    return "\033[35m";
  case log_level::off:
    return "";
  }
  return "";
}

// ANSI 颜色重置码。
inline auto reset_color() -> std::string_view { return "\033[0m"; }

// 获取当前进程 ID，用于在多进程部署时区分日志来源。
inline auto current_process_id() -> std::uint32_t {
#if defined(_WIN32)
  return static_cast<std::uint32_t>(::GetCurrentProcessId());
#else
  return static_cast<std::uint32_t>(::getpid());
#endif
}

// 获取当前线程 ID，避免在热路径上直接格式化 std::thread::id。
inline auto current_thread_id() -> std::uint64_t {
  return static_cast<std::uint64_t>(
      std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

// 线程安全的本地时间转换。
inline void safe_localtime(std::time_t time_value, std::tm *tm) {
#if defined(_WIN32)
  localtime_s(tm, &time_value);
#else
  localtime_r(&time_value, tm);
#endif
}

// 线程安全的 UTC 时间转换。
inline void safe_gmtime(std::time_t time_value, std::tm *tm) {
#if defined(_WIN32)
  gmtime_s(tm, &time_value);
#else
  gmtime_r(&time_value, tm);
#endif
}

// 将路径中的反斜杠统一成正斜杠，便于跨平台展示。
inline auto normalize_path(std::string path) -> std::string {
  for (auto &ch : path) {
    if (ch == '\\') {
      ch = '/';
    }
  }
  return path;
}

// 按配置将源文件路径格式化为文件名或绝对路径。
inline auto format_source_path(std::string_view file_name,
                               const format_config &config) -> std::string {
  if (config.source_path == source_path_mode::absolute) {
    return std::string(file_name);
  }
  const auto source = std::filesystem::path(file_name);
  return normalize_path(source.filename().generic_string());
}

// 统一格式化日志时间戳，同时支持普通日志和文件名用时间戳。
inline auto make_timestamp(std::chrono::system_clock::time_point tp,
                           time_mode mode,
                           bool for_filename = false,
                           bool with_microseconds = true) -> std::string {
  const auto time_value = std::chrono::system_clock::to_time_t(tp);
  std::tm tm{};
  if (mode == time_mode::utc) {
    safe_gmtime(time_value, &tm);
  } else {
    safe_localtime(time_value, &tm);
  }

  const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                          tp.time_since_epoch()) %
                      std::chrono::seconds(1);

  if (for_filename) {
    return std::format("{:04}-{:02}-{:02}_{:02}-{:02}-{:02}", tm.tm_year + 1900,
                       tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
                       tm.tm_sec);
  }

  if (!with_microseconds) {
    return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}",
                       tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                       tm.tm_min, tm.tm_sec);
  }

  return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:06}",
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                     tm.tm_min, tm.tm_sec, micros.count());
}

// 根据简洁/标准/全量模式应用一组统一的默认输出策略。
// 简洁模式用于默认显示；
// 标准模式用于终端快速定位问题；
// 全量模式用于问题定位和完整诊断，强制输出所有上下文并使用绝对路径。
inline void apply_detail_mode_preset(format_config *config, detail_mode mode) {
  config->detail = mode;
  if (mode == detail_mode::compact) {
    config->show_timestamp = true;
    config->timestamp_with_microseconds = false;
    config->show_level = true;
    config->show_logger_name = false;
    config->show_thread_id = false;
    config->show_process_id = false;
    config->show_source_location = false;
    config->source_path = source_path_mode::filename;
    return;
  }

  if (mode == detail_mode::standard) {
    config->show_timestamp = true;
    config->timestamp_with_microseconds = false;
    config->show_level = true;
    config->show_logger_name = false;
    config->show_thread_id = false;
    config->show_process_id = true;
    config->show_source_location = true;
    config->source_path = source_path_mode::filename;
    return;
  }

  config->show_timestamp = true;
  config->timestamp_with_microseconds = true;
  config->show_level = true;
  config->show_logger_name = true;
  config->show_thread_id = true;
  config->show_process_id = true;
  config->show_source_location = true;
  config->source_path = source_path_mode::absolute;
}

// 带 source_location 的格式串包装器。
// 这样可以在不依赖宏的前提下，把调用点信息一起带入 logger 前端。
template <typename... Args>
struct format_string_with_location {
  template <typename T>
    requires std::convertible_to<T, std::string_view>
  consteval format_string_with_location(
      const T &fmt_string,
      std::source_location location = std::source_location::current())
      : fmt(fmt_string), loc(location) {}

  std::format_string<Args...> fmt;
  std::source_location loc;
};

// 对外暴露的格式串别名，避免模板参数被意外推导。
template <typename... Args>
using format_string =
    format_string_with_location<std::type_identity_t<Args>...>;

// 对 std::format 的简单封装，方便未来替换底层格式化后端。
template <typename... Args>
inline auto format_message(std::format_string<Args...> fmt, Args &&...args)
    -> std::string {
  return std::format(fmt, std::forward<Args>(args)...);
}

// 获取当前线程的调用栈文本。
[[nodiscard]] inline auto current_stacktrace_text(std::size_t skip = 0)
    -> std::string {
#ifdef FASTLOG_WITH_CPPTRACE
  return cpptrace::generate_trace(skip + 1).to_string(false);
#else
  (void)skip;
  return "stacktrace support disabled (FASTLOG_WITH_CPPTRACE=OFF)";
#endif
}

// 获取当前异常的调用栈文本。
[[nodiscard]] inline auto current_exception_stacktrace_text() -> std::string {
#ifdef FASTLOG_WITH_CPPTRACE
  return cpptrace::from_current_exception().to_string(false);
#else
  return "exception stacktrace support disabled (FASTLOG_WITH_CPPTRACE=OFF)";
#endif
}

// 统一格式化异常文本。
// 若启用了 cpptrace，会优先输出携带调用栈的增强异常信息。
[[nodiscard]] inline auto format_exception_text(const std::exception &exception)
    -> std::string {
#ifdef FASTLOG_WITH_CPPTRACE
  if (const auto *traceable = dynamic_cast<const cpptrace::exception *>(&exception);
      traceable != nullptr) {
    return std::format("Exception: {}\n{}", traceable->message(),
                       traceable->trace().to_string(false));
  }
  try {
    const auto trace = cpptrace::from_current_exception();
    if (!trace.empty()) {
      return std::format("Exception: {}\n{}", exception.what(),
                         trace.to_string(false));
    }
  } catch (...) {
  }
#endif
  return std::format("Exception: {}", exception.what());
}

} // namespace fastlog::detail
