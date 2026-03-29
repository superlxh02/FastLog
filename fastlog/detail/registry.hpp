#pragma once

#include "fastlog/detail/logger.hpp"
#include "fastlog/detail/sinks.hpp"

#include <mutex>
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
