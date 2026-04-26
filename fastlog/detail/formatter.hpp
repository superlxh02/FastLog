#pragma once

#include "fastlog/detail/support.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <cstdint>
#include <string>
#include <string_view>

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
  auto set_pattern(std::string pattern) -> pattern_formatter & {
    pattern_ = std::move(pattern);
    return *this;
  }

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
    const auto timestamp = record_timestamp(record);
    std::string text;
    auto append_prefix = [&](std::string_view part) {
      if (!text.empty()) {
        text.push_back(' ');
      }
      text.append(part);
    };

    if (config.show_timestamp) {
      append_prefix(detail::make_timestamp(timestamp, config.clock_mode, false,
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
      append_prefix(std::format("[{}]", logger_name(record)));
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
    if (pattern_ == "%v") {
      return record.message;
    }

    const auto timestamp = record_timestamp(record);
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
        text.push_back('%');
        break;
      }

      switch (pattern_[i]) {
      case '%':
        text.push_back('%');
        break;
      case '^':
        if (config.colorize) {
          text.append(detail::level_color(record.level));
        }
        break;
      case '$':
        if (config.colorize) {
          text.append(detail::reset_color());
        }
        break;
      case 'v':
        text += record.message;
        break;
      case 'l':
        append_level();
        break;
      case 'L':
        text.append(detail::level_to_short_string(record.level));
        break;
      case 'n':
        text += logger_name(record);
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
      case 'g':
        text += detail::normalize_path(record.location.file_name());
        break;
      case '#':
        text += std::to_string(record.location.line());
        break;
      case '!':
        text += record.location.function_name();
        break;
      case '@':
        text += detail::format_source_path(record.location.file_name(), config);
        text.push_back(':');
        text += std::to_string(record.location.line());
        break;
      case 'u':
        text += std::to_string(record.location.column());
        break;
      case 'Y':
        text += detail::make_timestamp(timestamp, config.clock_mode, false,
                                       config.timestamp_with_microseconds);
        break;
      case 'D':
        text += format_time_part(timestamp, config.clock_mode, "%Y-%m-%d");
        break;
      case 'H':
        text += format_time_part(timestamp, config.clock_mode, "%H");
        break;
      case 'M':
        text += format_time_part(timestamp, config.clock_mode, "%M");
        break;
      case 'S':
        text += format_time_part(timestamp, config.clock_mode, "%S");
        break;
      case 'e':
        text += std::format("{:03}", fractional_milliseconds(timestamp));
        break;
      case 'f':
        text += std::format("{:06}", fractional_microseconds(timestamp));
        break;
      case 'z':
        text += format_timezone_offset(timestamp);
        break;
      default:
        text.push_back('%');
        text.push_back(pattern_[i]);
        break;
      }
    }

    return text;
  }

  [[nodiscard]] static auto fractional_microseconds(
      std::chrono::system_clock::time_point timestamp) -> std::int64_t {
    return (std::chrono::duration_cast<std::chrono::microseconds>(
                timestamp.time_since_epoch()) %
            std::chrono::seconds(1))
        .count();
  }

  [[nodiscard]] static auto fractional_milliseconds(
      std::chrono::system_clock::time_point timestamp) -> std::int64_t {
    return fractional_microseconds(timestamp) / 1000;
  }

  [[nodiscard]] static auto format_time_part(
      std::chrono::system_clock::time_point timestamp, time_mode mode,
      std::string_view pattern) -> std::string {
    const auto time_value = std::chrono::system_clock::to_time_t(timestamp);
    std::tm tm{};
    if (mode == time_mode::utc) {
      detail::safe_gmtime(time_value, &tm);
    } else {
      detail::safe_localtime(time_value, &tm);
    }
    char buffer[32]{};
    const auto written = std::strftime(buffer, sizeof(buffer), pattern.data(), &tm);
    return written == 0 ? std::string{} : std::string(buffer, written);
  }

  [[nodiscard]] static auto format_timezone_offset(
      std::chrono::system_clock::time_point timestamp) -> std::string {
    const auto time_value = std::chrono::system_clock::to_time_t(timestamp);
    std::tm local_tm{};
    std::tm utc_tm{};
    detail::safe_localtime(time_value, &local_tm);
    detail::safe_gmtime(time_value, &utc_tm);

    const auto local_time = std::mktime(&local_tm);
#if defined(_WIN32)
    const auto utc_time = _mkgmtime(&utc_tm);
#else
    const auto utc_time = timegm(&utc_tm);
#endif
    const auto offset_seconds =
        static_cast<long>(std::difftime(local_time, utc_time));
    const auto sign = offset_seconds >= 0 ? '+' : '-';
    const auto absolute = std::labs(offset_seconds);
    return std::format("{}{:02}{:02}", sign, absolute / 3600,
                       (absolute % 3600) / 60);
  }

  [[nodiscard]] static auto logger_name(const log_record &record)
      -> std::string_view {
    if (!record.logger_name_ref.empty()) {
      return record.logger_name_ref;
    }
    return record.logger_name;
  }

  [[nodiscard]] static auto record_timestamp(const log_record &record)
      -> std::chrono::system_clock::time_point {
    if (record.timestamp.time_since_epoch() !=
        std::chrono::system_clock::duration::zero()) {
      return record.timestamp;
    }
    return std::chrono::system_clock::now();
  }

  std::string pattern_; // 用户配置的 pattern 文本。
};

} // namespace fastlog
