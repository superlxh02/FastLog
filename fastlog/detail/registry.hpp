#pragma once

#include "fastlog/detail/logger.hpp"
#include "fastlog/detail/sinks.hpp"

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fastlog {

// 全局 logger 注册中心。
// 负责管理命名 logger、默认 logger，以及批量 flush 等全局操作。
class registry {
public:
  // 获取全局单例。
  static auto instance() -> registry & {
    static registry registry_instance;
    return registry_instance;
  }

  // 创建或注册一个命名 logger。
  auto create_logger(std::string name, std::vector<sink_ptr> sinks,
                     log_level level = log_level::info) -> logger_ptr {
    auto logger_ptr_value =
        std::make_shared<logger>(std::move(name), std::move(sinks), level);
    register_logger(logger_ptr_value);
    return logger_ptr_value;
  }

  // 注册一个已经构造好的 logger。
  void register_logger(logger_ptr logger_ptr_value) {
    logger_ptr retired_logger;
    {
      std::lock_guard lock(mutex_);
      if (auto it = loggers_.find(logger_ptr_value->name());
          it != loggers_.end()) {
        retired_logger = std::move(it->second);
        it->second = std::move(logger_ptr_value);
      } else {
        loggers_.emplace(logger_ptr_value->name(), std::move(logger_ptr_value));
      }
    }
  }

  // 通过名称获取 logger。
  [[nodiscard]] auto get_logger(std::string_view name) const -> logger_ptr {
    std::lock_guard lock(mutex_);
    const auto it = loggers_.find(std::string(name));
    return it == loggers_.end() ? nullptr : it->second;
  }

  // 删除一个命名 logger。
  void drop_logger(std::string_view name) {
    logger_ptr retired_logger;
    {
      std::lock_guard lock(mutex_);
      if (auto it = loggers_.find(std::string(name)); it != loggers_.end()) {
        retired_logger = std::move(it->second);
        loggers_.erase(it);
      }
    }
  }

  // 清空所有命名 logger。
  void drop_all() {
    std::unordered_map<std::string, logger_ptr> retired_loggers;
    {
      std::lock_guard lock(mutex_);
      retired_loggers.swap(loggers_);
    }
  }

  // 获取默认 logger。
  [[nodiscard]] auto default_logger() const -> logger_ptr {
    std::lock_guard lock(mutex_);
    return default_logger_;
  }

  // 获取独立的控制台 logger。
  [[nodiscard]] auto console_logger() const -> logger_ptr {
    std::lock_guard lock(mutex_);
    return console_logger_;
  }

  // 设置默认 logger。
  void set_default_logger(logger_ptr logger_ptr_value) {
    logger_ptr retired_logger;
    {
      std::lock_guard lock(mutex_);
      retired_logger = std::move(default_logger_);
      default_logger_ = std::move(logger_ptr_value);
    }
  }

  // 设置独立的控制台 logger。
  void set_console_logger(logger_ptr logger_ptr_value) {
    logger_ptr retired_logger;
    {
      std::lock_guard lock(mutex_);
      retired_logger = std::move(console_logger_);
      console_logger_ = std::move(logger_ptr_value);
    }
  }

  // 获取当前所有命名 logger 的快照。
  [[nodiscard]] auto snapshot() const -> std::vector<logger_ptr> {
    std::vector<logger_ptr> result;
    std::lock_guard lock(mutex_);
    result.reserve(loggers_.size());
    for (const auto &[name, logger_ptr_value] : loggers_) {
      (void)name;
      result.push_back(logger_ptr_value);
    }
    return result;
  }

  // 刷新所有已注册 logger 和默认 logger。
  void flush_all() {
    for (const auto &logger_ptr_value : snapshot()) {
      logger_ptr_value->flush();
    }
    if (const auto default_ptr = default_logger(); default_ptr != nullptr) {
      default_ptr->flush();
    }
  }

private:
  // 注册中心构造函数。
  // 默认自动创建一个带彩色 stdout sink 的 console logger。
  registry() {
    auto stdout_sink_ptr = make_stdout_sink();
    auto config = stdout_sink_ptr->format_config_value();
    detail::apply_detail_mode_preset(&config, detail_mode::compact);
    config.colorize = true;
    stdout_sink_ptr->set_format_config(config);
    console_logger_ = std::make_shared<logger>(
        "console", std::vector<sink_ptr>{stdout_sink_ptr}, log_level::info);
    default_logger_ = console_logger_;
  }

  mutable std::mutex mutex_; // 保护 loggers_ 和 default_logger_ 的互斥锁。
  std::unordered_map<std::string, logger_ptr> loggers_; // 命名 logger 注册表。
  logger_ptr default_logger_; // 默认 logger。
  logger_ptr console_logger_; // 独立控制台 logger。
};

// 独立风格的 logger 管线构造器。
// 目标是用可读的链式 API 组合 sink、pattern、级别和异步包装，而不引入宏。
class logger_builder {
public:
  explicit logger_builder(std::string name) : name_(std::move(name)) {}

  auto at(log_level level) -> logger_builder & {
    level_ = level;
    return *this;
  }

  auto write_to(sink_ptr sink_ptr_value) -> logger_builder & {
    if (sink_ptr_value == nullptr) {
      return *this;
    }
    apply_sink_defaults(*sink_ptr_value);
    sinks_.push_back(std::move(sink_ptr_value));
    return *this;
  }

  auto write_to_async(sink_ptr sink_ptr_value, async_options options = {})
      -> logger_builder & {
    if (sink_ptr_value == nullptr) {
      return *this;
    }
    apply_sink_defaults(*sink_ptr_value);
    sinks_.push_back(make_async_sink(std::move(sink_ptr_value), options));
    return *this;
  }

  auto fanout(std::vector<sink_ptr> sinks) -> logger_builder & {
    for (const auto &sink_ptr_value : sinks) {
      if (sink_ptr_value != nullptr) {
        apply_sink_defaults(*sink_ptr_value);
      }
    }
    sinks_.push_back(make_fanout_sink(std::move(sinks)));
    return *this;
  }

  auto format_as(std::string pattern) -> logger_builder & {
    pattern_ = std::move(pattern);
    for (const auto &sink_ptr_value : sinks_) {
      sink_ptr_value->set_pattern(*pattern_);
    }
    return *this;
  }

  auto detail(detail_mode mode) -> logger_builder & {
    detail_ = mode;
    for (const auto &sink_ptr_value : sinks_) {
      auto config = sink_ptr_value->format_config_value();
      detail::apply_detail_mode_preset(&config, mode);
      sink_ptr_value->set_format_config(config);
    }
    return *this;
  }

  auto clock(time_mode mode) -> logger_builder & {
    clock_mode_ = mode;
    for (const auto &sink_ptr_value : sinks_) {
      auto config = sink_ptr_value->format_config_value();
      config.clock_mode = mode;
      sink_ptr_value->set_format_config(config);
    }
    return *this;
  }

  auto source(source_path_mode mode, std::filesystem::path root = {})
      -> logger_builder & {
    source_path_ = mode;
    source_root_ = std::move(root);
    for (const auto &sink_ptr_value : sinks_) {
      auto config = sink_ptr_value->format_config_value();
      config.source_path = mode;
      config.source_root = source_root_;
      sink_ptr_value->set_format_config(config);
    }
    return *this;
  }

  auto color(bool enabled = true) -> logger_builder & {
    colorize_ = enabled;
    for (const auto &sink_ptr_value : sinks_) {
      auto config = sink_ptr_value->format_config_value();
      config.colorize = enabled;
      sink_ptr_value->set_format_config(config);
    }
    return *this;
  }

  auto flush_when(log_level level) -> logger_builder & {
    flush_on_ = level;
    for (const auto &sink_ptr_value : sinks_) {
      sink_ptr_value->set_flush_on(level);
    }
    return *this;
  }

  [[nodiscard]] auto make() const -> logger_ptr {
    return std::make_shared<logger>(name_, sinks_, level_);
  }

  auto install() const -> logger_ptr {
    auto logger_ptr_value = make();
    registry::instance().register_logger(logger_ptr_value);
    return logger_ptr_value;
  }

  auto install_as_default() const -> logger_ptr {
    auto logger_ptr_value = install();
    registry::instance().set_default_logger(logger_ptr_value);
    return logger_ptr_value;
  }

private:
  void apply_sink_defaults(sink &sink_ref) const {
    if (pattern_.has_value()) {
      sink_ref.set_pattern(*pattern_);
    }
    auto config = sink_ref.format_config_value();
    if (detail_.has_value()) {
      detail::apply_detail_mode_preset(&config, *detail_);
    }
    if (clock_mode_.has_value()) {
      config.clock_mode = *clock_mode_;
    }
    if (source_path_.has_value()) {
      config.source_path = *source_path_;
      config.source_root = source_root_;
    }
    if (colorize_.has_value()) {
      config.colorize = *colorize_;
    }
    sink_ref.set_format_config(config);
    if (flush_on_.has_value()) {
      sink_ref.set_flush_on(*flush_on_);
    }
  }

  std::string name_;
  std::vector<sink_ptr> sinks_;
  log_level level_{log_level::info};
  std::optional<std::string> pattern_;
  std::optional<detail_mode> detail_;
  std::optional<time_mode> clock_mode_;
  std::optional<source_path_mode> source_path_;
  std::filesystem::path source_root_;
  std::optional<bool> colorize_;
  std::optional<log_level> flush_on_;
};

[[nodiscard]] inline auto pipeline(std::string name) -> logger_builder {
  return logger_builder{std::move(name)};
}

[[nodiscard]] inline auto to_string(log_level level) -> std::string_view {
  return detail::level_to_string(level);
}

[[nodiscard]] inline auto to_short_string(log_level level) -> std::string_view {
  return detail::level_to_short_string(level);
}

[[nodiscard]] inline auto parse_level(std::string_view text)
    -> std::optional<log_level> {
  return detail::level_from_string(text);
}

// 创建命名 logger 的全局快捷接口。
inline auto create_logger(std::string name, std::vector<sink_ptr> sinks,
                          log_level level = log_level::info) -> logger_ptr {
  return registry::instance().create_logger(std::move(name), std::move(sinks),
                                            level);
}

// 获取命名 logger 的全局快捷接口。
[[nodiscard]] inline auto get_logger(std::string_view name) -> logger_ptr {
  return registry::instance().get_logger(name);
}

// 删除命名 logger 的全局快捷接口。
inline void drop_logger(std::string_view name) {
  registry::instance().drop_logger(name);
}

// 清空所有命名 logger。
inline void drop_all_loggers() { registry::instance().drop_all(); }

// 获取默认 logger。
[[nodiscard]] inline auto default_logger() -> logger_ptr {
  return registry::instance().default_logger();
}

// 获取控制台 logger。
[[nodiscard]] inline auto console_logger() -> logger_ptr {
  return registry::instance().console_logger();
}

// 设置默认 logger。
inline void set_default_logger(logger_ptr logger_ptr_value) {
  registry::instance().set_default_logger(std::move(logger_ptr_value));
}

// 设置控制台 logger。
inline void set_console_logger(logger_ptr logger_ptr_value) {
  registry::instance().set_console_logger(std::move(logger_ptr_value));
}

// 刷新所有 logger。
inline void flush_all() { registry::instance().flush_all(); }

// 设置控制台 logger 的级别。
inline void set_console_level(log_level level) {
  if (const auto logger_ptr_value = console_logger(); logger_ptr_value != nullptr) {
    logger_ptr_value->set_level(level);
  }
}

// 设置控制台 logger 的 detail 模式。
inline void set_console_detail_mode(detail_mode mode) {
  if (const auto logger_ptr_value = console_logger(); logger_ptr_value != nullptr) {
    for (const auto &sink_ptr_value : logger_ptr_value->sinks()) {
      auto config = sink_ptr_value->format_config_value();
      detail::apply_detail_mode_preset(&config, mode);
      config.show_logger_name = false;
      sink_ptr_value->set_format_config(config);
    }
  }
}

} // namespace fastlog
