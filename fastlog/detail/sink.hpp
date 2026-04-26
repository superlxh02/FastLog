#pragma once

#include "fastlog/detail/formatter.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <utility>

namespace fastlog {

// sink 抽象基类。
// 所有具体 sink 都复用这里的级别过滤、formatter 管理和统计逻辑。
class sink {
public:
  // 虚析构函数，保证派生 sink 能被正确析构。
  virtual ~sink() = default;

  // 设置该 sink 的最小接收级别。
  auto set_level(log_level level) -> sink & {
    level_.store(level, std::memory_order_relaxed);
    return *this;
  }

  // 读取该 sink 的最小接收级别。
  [[nodiscard]] auto level() const -> log_level {
    return level_.load(std::memory_order_relaxed);
  }

  // 设置触发自动 flush 的级别阈值。
  auto set_flush_on(log_level level) -> sink & {
    flush_on_.store(level, std::memory_order_relaxed);
    return *this;
  }

  // 读取自动 flush 阈值。
  [[nodiscard]] auto flush_on() const -> log_level {
    return flush_on_.load(std::memory_order_relaxed);
  }

  // 使用内置 pattern_formatter 替换当前 formatter。
  auto set_pattern(std::string pattern) -> sink & {
    std::lock_guard lock(mutex_);
    const auto current =
        std::atomic_load_explicit(&format_state_, std::memory_order_acquire);
    publish_format_state(
        std::make_shared<pattern_formatter>(std::move(pattern)),
        current->config);
    return *this;
  }

  // 注入自定义 formatter。
  auto set_formatter(std::shared_ptr<formatter> formatter_ptr) -> sink & {
    std::lock_guard lock(mutex_);
    const auto current =
        std::atomic_load_explicit(&format_state_, std::memory_order_acquire);
    publish_format_state(std::move(formatter_ptr), current->config);
    return *this;
  }

  // 更新格式化配置。
  auto set_format_config(format_config config) -> sink & {
    std::lock_guard lock(mutex_);
    const auto current =
        std::atomic_load_explicit(&format_state_, std::memory_order_acquire);
    publish_format_state(current->formatter, std::move(config));
    return *this;
  }

  // 读取当前格式化配置。
  [[nodiscard]] auto format_config_value() const -> format_config {
    const auto state =
        std::atomic_load_explicit(&format_state_, std::memory_order_acquire);
    return state->config;
  }

  // 获取运行时统计快照。
  [[nodiscard]] virtual auto stats() const -> sink_stats {
    return sink_stats{
        .enqueued_messages = enqueued_messages_.load(std::memory_order_relaxed),
        .dropped_messages = dropped_messages_.load(std::memory_order_relaxed),
        .flushed_messages = flushed_messages_.load(std::memory_order_relaxed),
        .current_queue_depth = 0,
        .peak_queue_depth = 0};
  }

  // 声明该 sink 需要 logger 前端采样哪些元数据。
  [[nodiscard]] virtual auto required_metadata() const -> record_metadata {
    return {};
  }

  // sink 的统一写入入口。
  // 默认流程是：级别过滤 -> 渲染 -> 写入 -> 按阈值决定是否 flush。
  virtual void log(const log_record &record) {
    if (record.level < level()) {
      return;
    }
    record_enqueue();
    auto rendered = render(record);
    sink_it(rendered);
    if (record.level >= flush_on()) {
      flush();
    }
  }

  // 默认 flush 只更新统计，具体 sink 会覆盖为真正的 I/O flush。
  virtual void flush() { record_flush(); }

protected:
  // 调用当前 formatter，把 record 渲染为文本。
  [[nodiscard]] auto render(const log_record &record) const -> std::string {
    const auto state =
        std::atomic_load_explicit(&format_state_, std::memory_order_acquire);
    return state->formatter->format(record, state->config);
  }

  // 记录一次消息被丢弃。
  void record_drop() {
    dropped_messages_.fetch_add(1, std::memory_order_relaxed);
  }

  // 记录一次消息进入 sink。
  void record_enqueue() {
    enqueued_messages_.fetch_add(1, std::memory_order_relaxed);
  }

  // 记录一次 flush 事件。
  void record_flush() {
    flushed_messages_.fetch_add(1, std::memory_order_relaxed);
  }

private:
  struct format_state {
    std::shared_ptr<formatter> formatter;
    format_config config;
  };

  void publish_format_state(std::shared_ptr<formatter> formatter_ptr,
                            format_config config) {
    auto next = std::make_shared<const format_state>(
        std::move(formatter_ptr), std::move(config));
    std::atomic_store_explicit(&format_state_, std::move(next),
                               std::memory_order_release);
  }

  // 具体 sink 的实际输出逻辑，由派生类实现。
  virtual void sink_it(const std::string &rendered) = 0;

  mutable std::mutex mutex_; // 串行化 formatter 和 format_config 的发布。
  std::shared_ptr<const format_state> format_state_{
      std::make_shared<const format_state>(
          std::make_shared<pattern_formatter>(),
          format_config{})}; // 当前使用的 formatter/config 快照。
  std::atomic<log_level> level_{log_level::trace}; // sink 的最小接收级别。
  std::atomic<log_level> flush_on_{log_level::fatal}; // 自动触发 flush 的级别阈值。
  std::atomic<std::uint64_t> enqueued_messages_{0}; // 进入 sink 的消息总数。
  std::atomic<std::uint64_t> dropped_messages_{0}; // 被丢弃的消息总数。
  std::atomic<std::uint64_t> flushed_messages_{0}; // flush 调用总数。
};

} // namespace fastlog
