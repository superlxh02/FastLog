#pragma once

#include "fastlog/detail/sink.hpp"

#include <atomic>
#include <chrono>
#include <deque>
#include <exception>
#include <format>
#include <memory>
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
      : name_(std::move(name)), level_(level),
        single_sink_(sinks.size() == 1 ? sinks.front() : nullptr),
        required_metadata_bits_(
            encode_metadata(combine_required_metadata(sinks))),
        sinks_snapshot_(
            std::make_shared<const std::vector<sink_ptr>>(std::move(sinks))) {}

  // 设置 logger 自身的最小级别。
  auto set_level(log_level level) -> logger & {
    level_.store(level, std::memory_order_relaxed);
    return *this;
  }

  // 读取 logger 自身的最小级别。
  [[nodiscard]] auto level() const -> log_level {
    return level_.load(std::memory_order_relaxed);
  }

  // 轻量级级别判断，便于调用方在构造昂贵参数前手动短路。
  [[nodiscard]] auto should_log(log_level level_value) const -> bool {
    return level_value >= level();
  }

  // 获取 logger 名称。
  [[nodiscard]] auto name() const -> const std::string & { return name_; }

  // 整体替换 sink 列表。
  auto set_sinks(std::vector<sink_ptr> sinks) -> logger & {
    std::lock_guard lock(sinks_mutex_);
    single_sink_ = sinks.size() == 1 ? sinks.front() : nullptr;
    required_metadata_bits_.store(
        encode_metadata(combine_required_metadata(sinks)),
        std::memory_order_release);
    auto next = std::make_shared<const std::vector<sink_ptr>>(std::move(sinks));
    std::atomic_store_explicit(&sinks_snapshot_, std::move(next),
                               std::memory_order_release);
    return *this;
  }

  // 追加一个新的 sink。
  auto add_sink(sink_ptr sink_ptr_value) -> logger & {
    std::lock_guard lock(sinks_mutex_);
    auto current =
        std::atomic_load_explicit(&sinks_snapshot_, std::memory_order_acquire);
    auto next = std::make_shared<std::vector<sink_ptr>>(
        current != nullptr ? *current : std::vector<sink_ptr>{});
    next->push_back(std::move(sink_ptr_value));
    single_sink_ = next->size() == 1 ? next->front() : nullptr;
    required_metadata_bits_.store(
        encode_metadata(combine_required_metadata(*next)),
        std::memory_order_release);
    std::shared_ptr<const std::vector<sink_ptr>> published = std::move(next);
    std::atomic_store_explicit(&sinks_snapshot_, std::move(published),
                               std::memory_order_release);
    return *this;
  }

  // 获取当前 sink 快照，避免调用方长期持有内部容器引用。
  [[nodiscard]] auto sinks() const -> std::vector<sink_ptr> {
    const auto snapshot =
        std::atomic_load_explicit(&sinks_snapshot_, std::memory_order_acquire);
    return snapshot != nullptr ? *snapshot : std::vector<sink_ptr>{};
  }

  // 开启 backtrace ring buffer。
  // capacity 表示最多保留多少条最近日志。
  auto enable_backtrace(std::size_t capacity) -> logger & {
    std::lock_guard lock(backtrace_mutex_);
    backtrace_capacity_ = capacity;
    backtrace_buffer_.clear();
    backtrace_enabled_.store(capacity > 0, std::memory_order_release);
    return *this;
  }

  // 关闭 backtrace ring buffer，并清空已缓存内容。
  auto disable_backtrace() -> logger & {
    backtrace_enabled_.store(false, std::memory_order_release);
    std::lock_guard lock(backtrace_mutex_);
    backtrace_capacity_ = 0;
    backtrace_buffer_.clear();
    return *this;
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

  // 设置所有当前 sink 的自动 flush 阈值。
  auto set_flush_on(log_level level_value) -> logger & {
    for (auto &sink_ptr_value : sinks()) {
      sink_ptr_value->set_flush_on(level_value);
    }
    return *this;
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
    if (!should_log(level_value)) {
      return;
    }

    const auto metadata = required_metadata();
    log_record record{
        .logger_name = {},
        .logger_name_ref = name_,
        .level = level_value,
        .force_source_location = force_source_location,
        .message = std::move(message)};
    apply_metadata(record, metadata);
    apply_source_location(record, metadata, location, force_source_location);

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
    if (!should_log(level_value)) {
      return;
    }

    // 第二步：构造统一日志记录对象，把所有公共元信息一次性采齐。
    const auto metadata = required_metadata();
    log_record record{
        .logger_name = {},
        .logger_name_ref = name_,
        .level = level_value,
        .force_source_location = false,
        .message = metadata.message
                       ? detail::format_message(fmt, std::forward<Args>(args)...)
                       : std::string{}};
    apply_metadata(record, metadata);
    apply_source_location(record, metadata, location, false);

    // 第三步：若开启了 ring buffer，则先缓存一份最近日志。
    remember_backtrace(record);
    // 第四步：将日志分发给当前 logger 绑定的全部 sink。
    dispatch(record);
  }

  // 将最近日志写入 ring buffer。
  void remember_backtrace(const log_record &record) {
    if (!backtrace_enabled_.load(std::memory_order_acquire)) {
      return;
    }
    std::lock_guard lock(backtrace_mutex_);
    if (backtrace_capacity_ == 0) {
      return;
    }
    if (backtrace_buffer_.size() >= backtrace_capacity_) {
      backtrace_buffer_.pop_front();
    }
    backtrace_buffer_.push_back(record);
  }

  [[nodiscard]] auto required_metadata() const -> record_metadata {
    return decode_metadata(
        required_metadata_bits_.load(std::memory_order_acquire));
  }

  [[nodiscard]] static auto
  combine_required_metadata(const std::vector<sink_ptr> &sinks)
      -> record_metadata {
    record_metadata metadata{.message = false,
                             .timestamp = false,
                             .thread_id = false,
                             .process_id = false,
                             .source_location = false};
    for (const auto &sink_ptr_value : sinks) {
      const auto sink_metadata = sink_ptr_value->required_metadata();
      metadata.message = metadata.message || sink_metadata.message;
      metadata.timestamp = metadata.timestamp || sink_metadata.timestamp;
      metadata.thread_id = metadata.thread_id || sink_metadata.thread_id;
      metadata.process_id = metadata.process_id || sink_metadata.process_id;
      metadata.source_location =
          metadata.source_location || sink_metadata.source_location;
    }
    return metadata;
  }

  static void apply_metadata(log_record &record,
                             const record_metadata &metadata) {
    if (metadata.timestamp) {
      record.timestamp = std::chrono::system_clock::now();
    }
    if (metadata.thread_id) {
      record.thread_id = detail::current_thread_id();
    }
    if (metadata.process_id) {
      record.process_id = detail::current_process_id();
    }
  }

  static void apply_source_location(log_record &record,
                                    const record_metadata &metadata,
                                    std::source_location location,
                                    bool force_source_location) {
    if (metadata.source_location || force_source_location) {
      record.location = location;
    }
  }

  [[nodiscard]] static auto encode_metadata(const record_metadata &metadata)
      -> unsigned {
    return (metadata.message ? 0x1U : 0U) |
           (metadata.timestamp ? 0x2U : 0U) |
           (metadata.thread_id ? 0x4U : 0U) |
           (metadata.process_id ? 0x8U : 0U) |
           (metadata.source_location ? 0x10U : 0U);
  }

  [[nodiscard]] static auto decode_metadata(unsigned bits) -> record_metadata {
    return {.message = (bits & 0x1U) != 0,
            .timestamp = (bits & 0x2U) != 0,
            .thread_id = (bits & 0x4U) != 0,
            .process_id = (bits & 0x8U) != 0,
            .source_location = (bits & 0x10U) != 0};
  }

  // 将一条日志分发给所有 sink。
  void dispatch(const log_record &record) {
    if (single_sink_ != nullptr) {
      single_sink_->log(record);
      return;
    }
    const auto snapshot =
        std::atomic_load_explicit(&sinks_snapshot_, std::memory_order_acquire);
    if (snapshot == nullptr) {
      return;
    }
    for (const auto &sink_ptr_value : *snapshot) {
      sink_ptr_value->log(record);
    }
  }

  std::string name_; // logger 名称。
  mutable std::mutex sinks_mutex_; // 保护 sink 列表更新的互斥锁。
  std::atomic<log_level> level_{log_level::info}; // logger 自身的最小级别。
  sink_ptr single_sink_; // 单 sink 热路径，避免每条日志读取 sink 快照。
  std::atomic<unsigned> required_metadata_bits_{0x1fU}; // 当前 sink 集合需要的元数据。
  std::shared_ptr<const std::vector<sink_ptr>>
      sinks_snapshot_; // 写路径原子读取的 sink 快照。
  mutable std::mutex backtrace_mutex_; // 保护 ring buffer 的互斥锁。
  std::deque<log_record> backtrace_buffer_; // 保存最近日志的 ring buffer。
  std::size_t backtrace_capacity_{0}; // ring buffer 容量。
  std::atomic<bool> backtrace_enabled_{false}; // 是否启用了 ring buffer。
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
