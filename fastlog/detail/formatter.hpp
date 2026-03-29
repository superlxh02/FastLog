#pragma once

#include "fastlog/detail/support.hpp"

#include <string>

namespace fastlog {

// formatter 抽象基类。
// sink 会把 log_record 交给 formatter 渲染成最终文本。
class formatter {
public:
  // 虚析构函数，保证通过基类指针释放派生 formatter 时行为正确。
  virtual ~formatter() = default;

  // 将统一的日志记录和格式配置渲染成最终输出字符串。
  [[nodiscard]] virtual auto format(const log_record &record,
                                    const format_config &config) const
      -> std::string = 0;
};

// 基于 pattern 的默认 formatter。
// pattern 为空时走默认布局，否则按占位符逐个展开。
class pattern_formatter final : public formatter {
public:
  // 构造一个 pattern formatter；若 pattern 为空则使用默认布局。
  explicit pattern_formatter(std::string pattern = {})
      : pattern_(std::move(pattern)) {}

  // 更新 pattern 文本。
  void set_pattern(std::string pattern) { pattern_ = std::move(pattern); }

  // 渲染入口：优先判断是否启用了 pattern。
  [[nodiscard]] auto format(const log_record &record,
                            const format_config &config) const
      -> std::string override {
    if (pattern_.empty()) {
      return format_default(record, config);
    }
    return format_pattern(record, config);
  }

private:
  // 默认布局渲染路径。
  // 这里会根据 format_config 控制各字段是否输出，以及路径是相对还是绝对。
  [[nodiscard]] auto format_default(const log_record &record,
                                    const format_config &config) const
      -> std::string {
    std::string text;
    auto append_prefix = [&](std::string_view part) {
      if (!text.empty()) {
        text.push_back(' ');
      }
      text.append(part);
    };

    if (config.show_timestamp) {
      append_prefix(detail::make_timestamp(record.timestamp, config.clock_mode, false,
                                          config.timestamp_with_microseconds));
    }
    if (config.show_level) {
      if (config.colorize) {
        append_prefix(std::format("[{}{}{}]", detail::level_color(record.level),
                                  detail::level_to_string(record.level),
                                  detail::reset_color()));
      } else {
        append_prefix(std::format("[{}]", detail::level_to_string(record.level)));
      }
    }
    if (config.show_logger_name) {
      append_prefix(std::format("[{}]", record.logger_name));
    }
    if (config.show_thread_id) {
      append_prefix(std::format("[tid:{}]", record.thread_id));
    }
    if (config.show_process_id) {
      append_prefix(std::format("[pid:{}]", record.process_id));
    }

    const auto show_source =
        config.detail == detail_mode::full || config.show_source_location ||
        record.force_source_location;
    if (show_source) {
      append_prefix(
          std::format("[{}:{}]",
                      detail::format_source_path(record.location.file_name(),
                                                 config),
                      record.location.line()));
    }

    if (!text.empty()) {
      text.push_back(' ');
    }
    text += record.message;
    return text;
  }

  // pattern 渲染路径。
  // 这里只处理常见占位符，保持 header-only 实现简洁。
  [[nodiscard]] auto format_pattern(const log_record &record,
                                    const format_config &config) const
      -> std::string {
    std::string text;
    text.reserve(pattern_.size() + record.message.size() + 64);

    auto append_level = [&] {
      if (config.colorize) {
        text += std::format("{}{}{}", detail::level_color(record.level),
                            detail::level_to_string(record.level),
                            detail::reset_color());
      } else {
        text.append(detail::level_to_string(record.level));
      }
    };

    for (std::size_t i = 0; i < pattern_.size(); ++i) {
      if (pattern_[i] != '%') {
        text.push_back(pattern_[i]);
        continue;
      }
      ++i;
      if (i >= pattern_.size()) {
        break;
      }

      switch (pattern_[i]) {
      case '%':
        text.push_back('%');
        break;
      case 'v':
        text += record.message;
        break;
      case 'l':
        append_level();
        break;
      case 'n':
        text += record.logger_name;
        break;
      case 't':
        text += std::to_string(record.thread_id);
        break;
      case 'P':
        text += std::to_string(record.process_id);
        break;
      case 's':
        text += detail::format_source_path(record.location.file_name(), config);
        break;
      case '#':
        text += std::to_string(record.location.line());
        break;
      case 'Y':
        text += detail::make_timestamp(record.timestamp, config.clock_mode, false,
                                       config.timestamp_with_microseconds);
        break;
      default:
        text.push_back('%');
        text.push_back(pattern_[i]);
        break;
      }
    }

    return text;
  }

  std::string pattern_; // 用户配置的 pattern 文本。
};

} // namespace fastlog
