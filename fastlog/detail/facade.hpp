#pragma once

#include "fastlog/detail/stacktrace.hpp"

#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace fastlog {

// 面向“基础日志输出需求”的简洁文件 logger 配置。
// 目标是让用户在只关心落盘、级别和细节模式时，不必直接接触底层 sink 组合。
struct FileLoggerOptions {
  ::fastlog::log_level level{
      ::fastlog::log_level::debug}; // 文件 logger 的最小日志级别。
  ::fastlog::detail_mode detail_mode{
      ::fastlog::detail_mode::compact}; // 文件输出默认使用简洁模式。
  ::fastlog::source_path_mode source_path{
      ::fastlog::source_path_mode::filename}; // 源码路径输出模式。
  std::filesystem::path
      source_root{}; // relative 源码路径模式使用的裁剪基准目录。
  std::size_t max_file_size{1024 * 1024 * 100}; // 单个日志文件最大大小。
  std::size_t max_files{5};                     // 保留的轮转文件数量。
  bool async_write{true};                       // 是否默认使用异步包装。
  ::fastlog::overflow_policy overflow{
      ::fastlog::overflow_policy::block};        // 异步队列满载策略。
  std::size_t queue_size{8192};                  // 异步队列容量。
  std::chrono::milliseconds flush_interval{250}; // 异步周期性 flush 间隔。
  bool show_thread_id{true};                     // 是否输出线程 ID。
  bool show_process_id{false};                   // 是否输出进程 ID。
};

namespace detail {

// 简洁 file facade 的上下文对象。
// 这里记录了通过简单文件接口创建的 logger 及其关键 sink，
// 让对外句柄在 logger 被从注册表移除后仍能保持稳定生命周期。
struct simple_file_logger_context {
  logger_ptr logger_handle; // 对外暴露的 logger 共享所有权。
  std::shared_ptr<rotating_file_sink>
      rotating_sink_handle; // 负责真实文件落盘的轮转 sink。
  std::shared_ptr<async_sink> async_sink_handle; // 可选的异步包装层。
  std::filesystem::path log_path;                // 当前 logger 对应的日志路径。
  bool async_write{true};                        // 当前是否启用了异步写入。
};

} // namespace detail

namespace file {

// 简洁文件 logger 句柄。
// 该对象采用共享所有权语义，避免把裸引用暴露给用户后发生悬空访问。
class FileLogger {
public:
  FileLogger() = default;

  // 由内部 registry 创建稳定句柄。
  FileLogger(logger_ptr logger_ptr_value,
             std::shared_ptr<detail::simple_file_logger_context> context)
      : logger_handle_(std::move(logger_ptr_value)),
        context_handle_(std::move(context)) {}

  // 当前句柄是否有效。
  [[nodiscard]] auto valid() const -> bool { return logger_handle_ != nullptr; }

  // 允许直接写 if (logger)。
  explicit operator bool() const { return valid(); }

  // 返回底层 logger 的共享指针，便于与高级 API 互操作。
  [[nodiscard]] auto share() const -> logger_ptr { return logger_handle_; }

  // 获取 logger 名称；若句柄为空则返回空字符串引用。
  [[nodiscard]] auto name() const -> const std::string & {
    static const std::string empty_name;
    return logger_handle_ != nullptr ? logger_handle_->name() : empty_name;
  }

  // 获取当前 sink 快照。
  [[nodiscard]] auto sinks() const -> std::vector<sink_ptr> {
    return logger_handle_ != nullptr ? logger_handle_->sinks()
                                     : std::vector<sink_ptr>{};
  }

  // 设置 logger 自身级别。
  auto set_level(log_level level) const -> const FileLogger & {
    if (logger_handle_ != nullptr) {
      logger_handle_->set_level(level);
    }
    return *this;
  }

  // 获取 logger 当前级别。
  [[nodiscard]] auto level() const -> log_level {
    return logger_handle_ != nullptr ? logger_handle_->level() : log_level::off;
  }

  // 判断指定级别是否会被当前 logger 接收，便于昂贵日志内容手动短路。
  [[nodiscard]] auto should_log(log_level level_value) const -> bool {
    return logger_handle_ != nullptr && logger_handle_->should_log(level_value);
  }

  // 设置所有当前 sink 的自动 flush 阈值。
  auto set_flush_on(log_level level_value) const -> const FileLogger & {
    if (logger_handle_ != nullptr) {
      logger_handle_->set_flush_on(level_value);
    }
    return *this;
  }

  // 开启 backtrace ring buffer。
  auto enable_backtrace(std::size_t capacity) const -> const FileLogger & {
    if (logger_handle_ != nullptr) {
      logger_handle_->enable_backtrace(capacity);
    }
    return *this;
  }

  // 关闭 backtrace ring buffer。
  auto disable_backtrace() const -> const FileLogger & {
    if (logger_handle_ != nullptr) {
      logger_handle_->disable_backtrace();
    }
    return *this;
  }

  // 将缓存的 backtrace 补发到 sink。
  void flush_backtrace() const {
    if (logger_handle_ != nullptr) {
      logger_handle_->flush_backtrace();
    }
  }

  // 主动刷新所有 sink。
  void flush() const {
    if (logger_handle_ != nullptr) {
      logger_handle_->flush();
    }
  }

  // 通用日志入口：级别由调用方显式传入。
  template <typename... Args>
  void log(log_level level_value, detail::format_string<Args...> fmt,
           Args &&...args) const {
    if (logger_handle_ != nullptr) {
      logger_handle_->log(level_value, fmt, std::forward<Args>(args)...);
    }
  }

  // trace 级别快捷接口。
  template <typename... Args>
  void trace(detail::format_string<Args...> fmt, Args &&...args) const {
    if (logger_handle_ != nullptr) {
      logger_handle_->trace(fmt, std::forward<Args>(args)...);
    }
  }

  // debug 级别快捷接口。
  template <typename... Args>
  void debug(detail::format_string<Args...> fmt, Args &&...args) const {
    if (logger_handle_ != nullptr) {
      logger_handle_->debug(fmt, std::forward<Args>(args)...);
    }
  }

  // info 级别快捷接口。
  template <typename... Args>
  void info(detail::format_string<Args...> fmt, Args &&...args) const {
    if (logger_handle_ != nullptr) {
      logger_handle_->info(fmt, std::forward<Args>(args)...);
    }
  }

  // warn 级别快捷接口。
  template <typename... Args>
  void warn(detail::format_string<Args...> fmt, Args &&...args) const {
    if (logger_handle_ != nullptr) {
      logger_handle_->warn(fmt, std::forward<Args>(args)...);
    }
  }

  // error 级别快捷接口。
  template <typename... Args>
  void error(detail::format_string<Args...> fmt, Args &&...args) const {
    if (logger_handle_ != nullptr) {
      logger_handle_->error(fmt, std::forward<Args>(args)...);
    }
  }

  // fatal 级别快捷接口。
  template <typename... Args>
  void fatal(detail::format_string<Args...> fmt, Args &&...args) const {
    if (logger_handle_ != nullptr) {
      logger_handle_->fatal(fmt, std::forward<Args>(args)...);
    }
  }

  // 记录一条已经格式化好的消息。
  void
  log_message(log_level level_value, std::string message,
              std::source_location location = std::source_location::current(),
              bool force_source_location = false) const {
    if (logger_handle_ != nullptr) {
      logger_handle_->log_message(level_value, std::move(message), location,
                                  force_source_location);
    }
  }

  // 输出异常日志。
  void exception(
      const std::exception &exception,
      std::source_location location = std::source_location::current()) const {
    if (logger_handle_ != nullptr) {
      logger_handle_->exception(exception, location);
    }
  }

  // 输出当前异常上下文。
  void current_exception(
      std::string_view prefix = "Unhandled exception",
      std::source_location location = std::source_location::current()) const {
    if (logger_handle_ != nullptr) {
      logger_handle_->current_exception(prefix, location);
    }
  }

  // 设置 detail 模式。
  auto set_detail_mode(detail_mode mode) const -> const FileLogger & {
    if (auto rotating = rotating_sink(); rotating != nullptr) {
      auto config = rotating->format_config_value();
      detail::apply_detail_mode_preset(&config, mode);
      rotating->set_format_config(config);
      return *this;
    }

    for (const auto &sink_ptr_value : sinks()) {
      auto config = sink_ptr_value->format_config_value();
      detail::apply_detail_mode_preset(&config, mode);
      sink_ptr_value->set_format_config(config);
    }
    return *this;
  }

  // 设置源码路径模式。
  auto set_source_path_mode(source_path_mode mode,
                            std::filesystem::path source_root = {}) const
      -> const FileLogger & {
    if (auto rotating = rotating_sink(); rotating != nullptr) {
      auto config = rotating->format_config_value();
      config.source_path = mode;
      config.source_root = std::move(source_root);
      rotating->set_format_config(config);
      return *this;
    }

    for (const auto &sink_ptr_value : sinks()) {
      auto config = sink_ptr_value->format_config_value();
      config.source_path = mode;
      config.source_root = source_root;
      sink_ptr_value->set_format_config(config);
    }
    return *this;
  }

  // 设置轮转文件大小阈值。
  auto set_max_file_size(std::size_t max_file_size) const
      -> const FileLogger & {
    if (auto rotating = rotating_sink(); rotating != nullptr) {
      rotating->set_max_file_size(max_file_size);
      return *this;
    }

    for (const auto &sink_ptr_value : sinks()) {
      if (auto rotating =
              std::dynamic_pointer_cast<rotating_file_sink>(sink_ptr_value);
          rotating != nullptr) {
        rotating->set_max_file_size(max_file_size);
      }
    }
    return *this;
  }

private:
  // 获取句柄关联的轮转 sink。
  [[nodiscard]] auto rotating_sink() const
      -> std::shared_ptr<rotating_file_sink> {
    return context_handle_ != nullptr ? context_handle_->rotating_sink_handle
                                      : nullptr;
  }

  logger_ptr logger_handle_; // 底层 logger 的共享所有权。
  std::shared_ptr<detail::simple_file_logger_context>
      context_handle_; // 与 facade 对应的稳定上下文。
};

} // namespace file

namespace detail {

class simple_file_logger_registry {
public:
  static auto instance() -> simple_file_logger_registry & {
    static simple_file_logger_registry registry_instance;
    return registry_instance;
  }

  auto make_logger(const std::string &logger_name,
                   std::filesystem::path log_path,
                   const FileLoggerOptions &options) -> file::FileLogger {
    // 统一路径语义：空路径时默认用 logger 名称；传入目录时自动补 logger 名称。
    if (log_path.empty()) {
      log_path = std::filesystem::path{logger_name};
    }
    if (!log_path.has_filename()) {
      log_path /= logger_name;
    }

    std::shared_ptr<simple_file_logger_context> retired_context_before_build;
    std::shared_ptr<simple_file_logger_context> retired_context_after_build;
    {
      std::lock_guard lock(mutex_);
      if (auto it = contexts_.find(logger_name); it != contexts_.end()) {
        if (it->second->log_path == log_path &&
            it->second->async_write == options.async_write) {
          apply_options(*it->second, options);
          return file::FileLogger{it->second->logger_handle, it->second};
        }
        retired_context_before_build = std::move(it->second);
        contexts_.erase(it);
      }
    }

    if (retired_context_before_build != nullptr) {
      registry::instance().drop_logger(logger_name);
    }

    rotating_file_sink_options rotating_options;
    rotating_options.truncate = false;
    rotating_options.create_directories = true;
    rotating_options.max_file_size = options.max_file_size;
    rotating_options.max_files = options.max_files;
    auto rotating =
        std::make_shared<rotating_file_sink>(log_path, rotating_options);
    apply_format(rotating, options);

    sink_ptr sink_ptr_value = rotating;
    std::shared_ptr<async_sink> async;
    if (options.async_write) {
      async = std::make_shared<async_sink>(
          rotating, async_options{.queue_size = options.queue_size,
                                  .policy = options.overflow,
                                  .flush_interval = options.flush_interval});
      sink_ptr_value = async;
    }

    auto logger_ptr_value = std::make_shared<logger>(
        logger_name, std::vector<sink_ptr>{sink_ptr_value}, options.level);
    auto context = std::make_shared<simple_file_logger_context>(
        simple_file_logger_context{.logger_handle = logger_ptr_value,
                                   .rotating_sink_handle = rotating,
                                   .async_sink_handle = async,
                                   .log_path = log_path,
                                   .async_write = options.async_write});

    {
      std::lock_guard lock(mutex_);
      if (auto it = contexts_.find(logger_name); it != contexts_.end()) {
        if (it->second->log_path == log_path &&
            it->second->async_write == options.async_write) {
          apply_options(*it->second, options);
          return file::FileLogger{it->second->logger_handle, it->second};
        }
        retired_context_after_build = std::move(it->second);
        it->second = context;
      } else {
        contexts_.emplace(logger_name, context);
      }
    }

    registry::instance().register_logger(logger_ptr_value);
    return file::FileLogger{logger_ptr_value, std::move(context)};
  }

  void delete_logger(const std::string &logger_name) {
    std::shared_ptr<simple_file_logger_context> retired_context;
    {
      std::lock_guard lock(mutex_);
      if (auto it = contexts_.find(logger_name); it != contexts_.end()) {
        retired_context = std::move(it->second);
        contexts_.erase(it);
      }
    }
    registry::instance().drop_logger(logger_name);
  }

  [[nodiscard]] auto get_logger(const std::string &logger_name)
      -> file::FileLogger {
    std::shared_ptr<simple_file_logger_context> context;
    {
      std::lock_guard lock(mutex_);
      if (auto it = contexts_.find(logger_name); it != contexts_.end()) {
        context = it->second;
      }
    }
    return context != nullptr
               ? file::FileLogger{context->logger_handle, context}
               : file::FileLogger{};
  }

private:
  // 将简洁配置映射到内部 sink 配置。
  static void
  apply_format(const std::shared_ptr<rotating_file_sink> &sink_ptr_value,
               const FileLoggerOptions &options) {
    auto config = sink_ptr_value->format_config_value();
    detail::apply_detail_mode_preset(&config, options.detail_mode);
    if (options.detail_mode == detail_mode::compact) {
      config.show_thread_id = options.show_thread_id;
      config.show_process_id = options.show_process_id;
      config.source_path = options.source_path;
    }
    config.source_root = options.source_root;
    sink_ptr_value->set_format_config(config);
    sink_ptr_value->set_flush_on(log_level::error);
  }

  // 对已有上下文应用轻量配置更新。
  static void apply_options(simple_file_logger_context &context,
                            const FileLoggerOptions &options) {
    context.logger_handle->set_level(options.level);
    context.rotating_sink_handle->set_max_file_size(options.max_file_size);
    apply_format(context.rotating_sink_handle, options);
  }

  std::mutex mutex_; // 保护 facade 上下文表的互斥锁。
  std::unordered_map<std::string, std::shared_ptr<simple_file_logger_context>>
      contexts_; // 简洁 file facade 的上下文表。
};

} // namespace detail

// 面向基础场景的控制台 facade。
// 用户可以直接使用 `fastlog::console.info(...)` 这类写法。
class console_facade {
public:
  // 输出 trace 日志。
  template <typename... Args>
  void trace(detail::format_string<Args...> fmt, Args &&...args) const {
    if (const auto logger_ptr_value = console_logger();
        logger_ptr_value != nullptr) {
      logger_ptr_value->trace(fmt, std::forward<Args>(args)...);
    }
  }

  // 输出 debug 日志。
  template <typename... Args>
  void debug(detail::format_string<Args...> fmt, Args &&...args) const {
    if (const auto logger_ptr_value = console_logger();
        logger_ptr_value != nullptr) {
      logger_ptr_value->debug(fmt, std::forward<Args>(args)...);
    }
  }

  // 输出 info 日志。
  template <typename... Args>
  void info(detail::format_string<Args...> fmt, Args &&...args) const {
    if (const auto logger_ptr_value = console_logger();
        logger_ptr_value != nullptr) {
      logger_ptr_value->info(fmt, std::forward<Args>(args)...);
    }
  }

  // 输出 warn 日志。
  template <typename... Args>
  void warn(detail::format_string<Args...> fmt, Args &&...args) const {
    if (const auto logger_ptr_value = console_logger();
        logger_ptr_value != nullptr) {
      logger_ptr_value->warn(fmt, std::forward<Args>(args)...);
    }
  }

  // 输出 error 日志。
  template <typename... Args>
  void error(detail::format_string<Args...> fmt, Args &&...args) const {
    if (const auto logger_ptr_value = console_logger();
        logger_ptr_value != nullptr) {
      logger_ptr_value->error(fmt, std::forward<Args>(args)...);
    }
  }

  // 输出 fatal 日志。
  template <typename... Args>
  void fatal(detail::format_string<Args...> fmt, Args &&...args) const {
    if (const auto logger_ptr_value = console_logger();
        logger_ptr_value != nullptr) {
      logger_ptr_value->fatal(fmt, std::forward<Args>(args)...);
    }
  }

  // 输出异常日志。
  void exception(
      const std::exception &exception,
      std::source_location location = std::source_location::current()) const {
    if (const auto logger_ptr_value = console_logger();
        logger_ptr_value != nullptr) {
      logger_ptr_value->exception(exception, location);
    }
  }

  // 输出当前异常上下文。
  void current_exception(
      std::string_view prefix = "Unhandled exception",
      std::source_location location = std::source_location::current()) const {
    if (const auto logger_ptr_value = console_logger();
        logger_ptr_value != nullptr) {
      logger_ptr_value->current_exception(prefix, location);
    }
  }

  // 主动刷新控制台 sink。
  void flush() const {
    if (const auto logger_ptr_value = console_logger();
        logger_ptr_value != nullptr) {
      logger_ptr_value->flush();
    }
  }
};

// 全局 console 对象。
inline const console_facade console{};

namespace file {

// 简洁文件 logger 创建接口。
inline auto make_logger(const std::string &logger_name,
                        std::filesystem::path log_path = {},
                        const FileLoggerOptions &options = {}) -> FileLogger {
  return detail::simple_file_logger_registry::instance().make_logger(
      logger_name, std::move(log_path), options);
}

// 简洁文件 logger 删除接口。
inline void delete_logger(const std::string &logger_name) {
  detail::simple_file_logger_registry::instance().delete_logger(logger_name);
}

// 简洁文件 logger 查询接口。
[[nodiscard]] inline auto get_logger(const std::string &logger_name)
    -> FileLogger {
  return detail::simple_file_logger_registry::instance().get_logger(
      logger_name);
}

// 设置文件 logger 的最小级别。
inline auto set_level(const FileLogger &logger_ref, log_level level)
    -> const FileLogger & {
  return logger_ref.set_level(level);
}

// 设置文件 logger 所有 sink 的 detail 模式。
inline auto set_detail_mode(const FileLogger &logger_ref, detail_mode mode)
    -> const FileLogger & {
  return logger_ref.set_detail_mode(mode);
}

// 设置文件 logger 的源码路径模式。
inline auto set_source_path_mode(const FileLogger &logger_ref,
                                 source_path_mode mode,
                                 std::filesystem::path source_root = {})
    -> const FileLogger & {
  return logger_ref.set_source_path_mode(mode, std::move(source_root));
}

// 设置文件 logger 的单文件最大大小。
inline auto set_max_file_size(const FileLogger &logger_ref,
                              std::size_t max_file_size)
    -> const FileLogger & {
  return logger_ref.set_max_file_size(max_file_size);
}

// 设置文件 logger 的自动 flush 阈值。
inline auto set_flush_on(const FileLogger &logger_ref, log_level level)
    -> const FileLogger & {
  return logger_ref.set_flush_on(level);
}

// 主动刷新文件 logger。
inline void flush(const FileLogger &logger_ref) { logger_ref.flush(); }

} // namespace file

} // namespace fastlog
