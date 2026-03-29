#pragma once

#include "fastlog/detail/sink.hpp"

#include <atomic>
#include <chrono>
#include <deque>
#include <exception>
#include <format>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fastlog {

// logger 是前端日志分发器。
// 它负责：
// 1. 做级别过滤；
// 2. 采集时间、线程、进程和 source_location；
// 3. 构造统一的 log_record；
// 4. 将 record 分发到一个或多个 sink。
class logger : public std::enable_shared_from_this<logger> {
public:
  // 构造 logger。
  // name 是逻辑名称，sinks 是输出目标列表，level 是 logger 自身的最小级别。
  logger(std::string name, std::vector<sink_ptr> sinks = {},
         log_level level = log_level::info)
      : name_(std::move(name)), sinks_(std::move(sinks)), level_(level) {}

  // 设置 logger 自身的最小级别。
  void set_level(log_level level) {
    level_.store(level, std::memory_order_relaxed);
  }

  // 读取 logger 自身的最小级别。
  [[nodiscard]] auto level() const -> log_level {
    return level_.load(std::memory_order_relaxed);
  }

  // 获取 logger 名称。
  [[nodiscard]] auto name() const -> const std::string & { return name_; }

  // 整体替换 sink 列表。
  void set_sinks(std::vector<sink_ptr> sinks) {
    std::lock_guard lock(sinks_mutex_);
    sinks_ = std::move(sinks);
  }

  // 追加一个新的 sink。
  void add_sink(sink_ptr sink_ptr_value) {
    std::lock_guard lock(sinks_mutex_);
    sinks_.push_back(std::move(sink_ptr_value));
  }

  // 获取当前 sink 快照，避免调用方长期持有内部容器引用。
  [[nodiscard]] auto sinks() const -> std::vector<sink_ptr> {
    std::lock_guard lock(sinks_mutex_);
    return sinks_;
  }

  // 开启 backtrace ring buffer。
  // capacity 表示最多保留多少条最近日志。
  void enable_backtrace(std::size_t capacity) {
    std::lock_guard lock(backtrace_mutex_);
    backtrace_capacity_ = capacity;
    backtrace_enabled_ = capacity > 0;
    backtrace_buffer_.clear();
  }

  // 关闭 backtrace ring buffer，并清空已缓存内容。
  void disable_backtrace() {
    std::lock_guard lock(backtrace_mutex_);
    backtrace_enabled_ = false;
    backtrace_capacity_ = 0;
    backtrace_buffer_.clear();
  }

  // 将 ring buffer 中缓存的日志重新补发到所有 sink。
  void flush_backtrace() {
    std::deque<log_record> snapshot;
    {
      std::lock_guard lock(backtrace_mutex_);
      snapshot = backtrace_buffer_;
    }
    for (const auto &record : snapshot) {
      dispatch(record);
    }
  }

  // 主动刷新所有 sink。
  void flush() {
    for (auto &sink_ptr_value : sinks()) {
      sink_ptr_value->flush();
    }
  }

  // 通用日志入口：级别由调用方显式传入。
  template <typename... Args>
  void log(log_level level_value, detail::format_string<Args...> fmt,
           Args &&...args) {
    log_with_location(level_value, fmt.loc, fmt.fmt, std::forward<Args>(args)...);
  }

  // trace 级别快捷接口。
  template <typename... Args>
  void trace(detail::format_string<Args...> fmt, Args &&...args) {
    log(log_level::trace, fmt, std::forward<Args>(args)...);
  }

  // debug 级别快捷接口。
  template <typename... Args>
  void debug(detail::format_string<Args...> fmt, Args &&...args) {
    log(log_level::debug, fmt, std::forward<Args>(args)...);
  }

  // info 级别快捷接口。
  template <typename... Args>
  void info(detail::format_string<Args...> fmt, Args &&...args) {
    log(log_level::info, fmt, std::forward<Args>(args)...);
  }

  // warn 级别快捷接口。
  template <typename... Args>
  void warn(detail::format_string<Args...> fmt, Args &&...args) {
    log(log_level::warn, fmt, std::forward<Args>(args)...);
  }

  // error 级别快捷接口。
  template <typename... Args>
  void error(detail::format_string<Args...> fmt, Args &&...args) {
    log(log_level::error, fmt, std::forward<Args>(args)...);
  }

  // fatal 级别快捷接口。
  template <typename... Args>
  void fatal(detail::format_string<Args...> fmt, Args &&...args) {
    log(log_level::fatal, fmt, std::forward<Args>(args)...);
  }

  // 记录一条已经预格式化好的消息。
  // 这个接口常用于异常输出、backtrace 回放等场景。
  void log_message(log_level level_value, std::string message,
                   std::source_location location =
                       std::source_location::current(),
                   bool force_source_location = false) {
    if (level_value < level()) {
      return;
    }

    log_record record{
        .logger_name = name_,
        .level = level_value,
        .timestamp = std::chrono::system_clock::now(),
        .thread_id = detail::current_thread_id(),
        .process_id = detail::current_process_id(),
        .location = location,
        .force_source_location = force_source_location,
        .message = std::move(message)};

    remember_backtrace(record);
    dispatch(record);
  }

  // 记录一个 std::exception。
  void exception(const std::exception &exception,
                 std::source_location location =
                     std::source_location::current()) {
    log_message(log_level::error, detail::format_exception_text(exception),
                location, true);
  }

  // 记录当前异常上下文。
  // 适用于 catch(...) 场景。
  void current_exception(
      std::string_view prefix = "Unhandled exception",
      std::source_location location = std::source_location::current()) {
    const auto current = std::current_exception();
    if (current == nullptr) {
      log_message(log_level::error, std::string(prefix), location, true);
      return;
    }
    try {
      std::rethrow_exception(current);
    } catch (const std::exception &exception) {
      log_message(log_level::error,
                  std::format("{}\n{}", prefix,
                              detail::format_exception_text(exception)),
                  location, true);
    } catch (...) {
      log_message(log_level::error,
                  std::format("{}\n<non-std exception>", prefix), location, true);
    }
  }

private:
  // 真正的前端采样入口。
  // 这里是 logger 的核心热路径，所以把流程拆得很明确：
  // 1. 先做 logger 级别过滤；
  // 2. 采集时间、线程、进程和源码位置；
  // 3. 调用格式化后端生成正文；
  // 4. 记录到 backtrace ring buffer；
  // 5. 分发给所有 sink。
  template <typename... Args>
  void log_with_location(log_level level_value, std::source_location location,
                         std::format_string<Args...> fmt, Args &&...args) {
    // 第一步：先做 logger 级别过滤，避免无意义的格式化开销。
    if (level_value < level()) {
      return;
    }

    // 第二步：构造统一日志记录对象，把所有公共元信息一次性采齐。
    log_record record{
        .logger_name = name_,
        .level = level_value,
        .timestamp = std::chrono::system_clock::now(),
        .thread_id = detail::current_thread_id(),
        .process_id = detail::current_process_id(),
        .location = location,
        .force_source_location = false,
        .message = detail::format_message(fmt, std::forward<Args>(args)...)};

    // 第三步：若开启了 ring buffer，则先缓存一份最近日志。
    remember_backtrace(record);
    // 第四步：将日志分发给当前 logger 绑定的全部 sink。
    dispatch(record);
  }

  // 将最近日志写入 ring buffer。
  void remember_backtrace(const log_record &record) {
    std::lock_guard lock(backtrace_mutex_);
    if (!backtrace_enabled_ || backtrace_capacity_ == 0) {
      return;
    }
    if (backtrace_buffer_.size() >= backtrace_capacity_) {
      backtrace_buffer_.pop_front();
    }
    backtrace_buffer_.push_back(record);
  }

  // 将一条日志分发给所有 sink。
  void dispatch(const log_record &record) {
    for (auto &sink_ptr_value : sinks()) {
      sink_ptr_value->log(record);
    }
  }

  std::string name_; // logger 名称。
  mutable std::mutex sinks_mutex_; // 保护 sink 列表的互斥锁。
  std::vector<sink_ptr> sinks_; // 当前绑定的输出目标集合。
  std::atomic<log_level> level_{log_level::info}; // logger 自身的最小级别。
  mutable std::mutex backtrace_mutex_; // 保护 ring buffer 的互斥锁。
  std::deque<log_record> backtrace_buffer_; // 保存最近日志的 ring buffer。
  std::size_t backtrace_capacity_{0}; // ring buffer 容量。
  bool backtrace_enabled_{false}; // 是否启用了 ring buffer。
};

// trace 自由函数包装，便于统一风格调用。
template <typename... Args>
inline void log_trace(logger &logger_ref, detail::format_string<Args...> fmt,
                      Args &&...args) {
  logger_ref.trace(fmt, std::forward<Args>(args)...);
}

// debug 自由函数包装。
template <typename... Args>
inline void log_debug(logger &logger_ref, detail::format_string<Args...> fmt,
                      Args &&...args) {
  logger_ref.debug(fmt, std::forward<Args>(args)...);
}

// info 自由函数包装。
template <typename... Args>
inline void log_info(logger &logger_ref, detail::format_string<Args...> fmt,
                     Args &&...args) {
  logger_ref.info(fmt, std::forward<Args>(args)...);
}

// warn 自由函数包装。
template <typename... Args>
inline void log_warn(logger &logger_ref, detail::format_string<Args...> fmt,
                     Args &&...args) {
  logger_ref.warn(fmt, std::forward<Args>(args)...);
}

// error 自由函数包装。
template <typename... Args>
inline void log_error(logger &logger_ref, detail::format_string<Args...> fmt,
                      Args &&...args) {
  logger_ref.error(fmt, std::forward<Args>(args)...);
}

// fatal 自由函数包装。
template <typename... Args>
inline void log_fatal(logger &logger_ref, detail::format_string<Args...> fmt,
                      Args &&...args) {
  logger_ref.fatal(fmt, std::forward<Args>(args)...);
}

} // namespace fastlog
