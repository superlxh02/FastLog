#pragma once

#include "fastlog/detail/sink.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <print>
#include <system_error>
#include <thread>
#include <vector>

namespace fastlog {

// 空 sink。
// 用于基准测试、按需禁用输出，或只测量 logger 前端格式化成本。
class null_sink final : public sink {
public:
  void log(const log_record &record) override {
    if (record.level < level()) {
      return;
    }
    record_enqueue();
    if (record.level >= flush_on()) {
      flush();
    }
  }

  [[nodiscard]] auto required_metadata() const -> record_metadata override {
    return {.message = false,
            .timestamp = false,
            .thread_id = false,
            .process_id = false,
            .source_location = false};
  }

private:
  void sink_it(const std::string &) override {}
};

// 分发 sink。
// 把多个 sink 包装成一个输出管道，便于复用相同的异步策略或统一注册。
class fanout_sink final : public sink {
public:
  explicit fanout_sink(std::vector<sink_ptr> sinks) : sinks_(std::move(sinks)) {}

  auto add_sink(sink_ptr sink_ptr_value) -> fanout_sink & {
    std::lock_guard lock(sinks_mutex_);
    sinks_.push_back(std::move(sink_ptr_value));
    return *this;
  }

  [[nodiscard]] auto sinks() const -> std::vector<sink_ptr> {
    std::lock_guard lock(sinks_mutex_);
    return sinks_;
  }

  [[nodiscard]] auto required_metadata() const -> record_metadata override {
    record_metadata metadata{.message = false,
                             .timestamp = false,
                             .thread_id = false,
                             .process_id = false,
                             .source_location = false};
    for (const auto &sink_ptr_value : sinks()) {
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

  void log(const log_record &record) override {
    if (record.level < level()) {
      return;
    }
    record_enqueue();
    for (const auto &sink_ptr_value : sinks()) {
      try {
        sink_ptr_value->log(record);
      } catch (...) {
        record_drop();
      }
    }
    if (record.level >= flush_on()) {
      flush();
    }
  }

  void flush() override {
    for (const auto &sink_ptr_value : sinks()) {
      sink_ptr_value->flush();
    }
    sink::flush();
  }

private:
  void sink_it(const std::string &) override {}

  mutable std::mutex sinks_mutex_;
  std::vector<sink_ptr> sinks_;
};

// 控制台/任意 ostream sink。
class stream_sink final : public sink {
public:
  // 构造时绑定一个输出流，例如 std::cout 或 std::cerr。
  explicit stream_sink(std::ostream &stream) : stream_(stream) {}

  // 刷新绑定的输出流。
  void flush() override {
    {
      std::lock_guard lock(stream_mutex_);
      stream_.flush();
    }
    sink::flush();
  }

private:
  // 将 formatter 渲染好的文本写入流。
  void sink_it(const std::string &rendered) override {
    std::lock_guard lock(stream_mutex_);
    std::print(stream_, "{}\n", rendered);
  }

  std::ostream &stream_;      // 目标输出流引用。
  std::mutex stream_mutex_;   // 保护输出流的互斥锁。
};

// 回调 sink。
// 适合接入 GUI、网络、测试捕获或自定义转发逻辑。
class callback_sink final : public sink {
public:
  using callback_type = std::function<void(std::string_view)>;

  // 构造时注入回调函数。
  explicit callback_sink(callback_type callback) : callback_(std::move(callback)) {}

private:
  // 将渲染结果直接转交给回调。
  void sink_it(const std::string &rendered) override {
    std::lock_guard lock(callback_mutex_);
    callback_(rendered);
  }

  callback_type callback_; // 用户提供的处理回调。
  std::mutex callback_mutex_; // 保证回调执行串行化。
};

// 基础文件 sink。
class basic_file_sink final : public sink {
public:
  // 构造时打开目标文件，并默认启用 full 模式。
  basic_file_sink(std::filesystem::path path, file_sink_options options = {}) {
    open_file(std::move(path), options.truncate, options.create_directories);
    auto config = format_config_value();
    detail::apply_detail_mode_preset(&config, detail_mode::compact);
    set_format_config(config);
  }

  // 刷新文件输出流。
  void flush() override {
    {
      std::lock_guard lock(file_mutex_);
      stream_.flush();
    }
    sink::flush();
  }

private:
  // 打开或重建文件句柄。
  void open_file(std::filesystem::path path, bool truncate, bool create_directories) {
    path_ = std::move(path);
    if (create_directories && path_.has_parent_path()) {
      std::filesystem::create_directories(path_.parent_path());
    }
    const auto mode = std::ios::out | std::ios::binary |
                      (truncate ? std::ios::trunc : std::ios::app);
    stream_.open(path_, mode);
    if (!stream_.is_open()) {
      throw std::runtime_error(std::format("failed to open log file: {}",
                                           path_.string()));
    }
  }

  // 将一条日志写入文件。
  void sink_it(const std::string &rendered) override {
    std::lock_guard lock(file_mutex_);
    stream_.write(rendered.data(),
                  static_cast<std::streamsize>(rendered.size()));
    stream_.put('\n');
  }

  std::filesystem::path path_; // 当前文件路径。
  std::ofstream stream_;       // 文件输出流。
  std::mutex file_mutex_;      // 保护文件流的互斥锁。
};

// 基于文件大小的轮转 sink。
class rotating_file_sink final : public sink {
public:
  // 构造时打开主日志文件，并默认启用 full 模式。
  rotating_file_sink(std::filesystem::path path,
                     rotating_file_sink_options options = {})
      : path_(std::move(path)), options_(options) {
    if (options_.create_directories && path_.has_parent_path()) {
      std::filesystem::create_directories(path_.parent_path());
    }
    open_file(options_.truncate);
    auto config = format_config_value();
    detail::apply_detail_mode_preset(&config, detail_mode::compact);
    set_format_config(config);
  }

  // 动态调整单文件最大大小。
  auto set_max_file_size(std::size_t max_file_size) -> rotating_file_sink & {
    std::lock_guard lock(file_mutex_);
    options_.max_file_size = max_file_size;
    return *this;
  }

  // 获取当前单文件最大大小。
  [[nodiscard]] auto max_file_size() const -> std::size_t {
    std::lock_guard lock(file_mutex_);
    return options_.max_file_size;
  }

  // 刷新当前文件。
  void flush() override {
    {
      std::lock_guard lock(file_mutex_);
      stream_.flush();
    }
    sink::flush();
  }

private:
  // 检查当前写入是否会超过阈值，若超过则执行轮转。
  void rotate_if_needed(std::size_t next_write_size) {
    if (current_size_ + next_write_size <= options_.max_file_size) {
      return;
    }

    if (stream_.is_open()) {
      stream_.flush();
      stream_.close();
    }

    if (options_.max_files > 0) {
      for (std::size_t i = options_.max_files; i > 0; --i) {
        const auto source = i == 1 ? path_ : rotated_path(i - 1);
        const auto target = rotated_path(i);
        if (std::filesystem::exists(source)) {
          std::error_code ec;
          std::filesystem::remove(target, ec);
          std::filesystem::rename(source, target, ec);
        }
      }
    }

    open_file(true);
  }

  // 生成轮转后的文件名，例如 app.log.1。
  [[nodiscard]] auto rotated_path(std::size_t index) const -> std::filesystem::path {
    return path_.string() + "." + std::to_string(index);
  }

  // 以截断或追加方式打开主文件。
  void open_file(bool truncate) {
    const auto mode = std::ios::out | std::ios::binary |
                      (truncate ? std::ios::trunc : std::ios::app);
    stream_.open(path_, mode);
    if (!stream_.is_open()) {
      throw std::runtime_error(std::format("failed to open rotating log file: {}",
                                           path_.string()));
    }
    current_size_ = std::filesystem::exists(path_) ?
                        std::filesystem::file_size(path_) :
                        0;
  }

  // 将一条日志写入文件，并在必要时触发轮转。
  void sink_it(const std::string &rendered) override {
    std::lock_guard lock(file_mutex_);
    rotate_if_needed(rendered.size() + 1);
    stream_.write(rendered.data(),
                  static_cast<std::streamsize>(rendered.size()));
    stream_.put('\n');
    current_size_ += rendered.size() + 1;
  }

  std::filesystem::path path_;          // 主日志文件路径。
  rotating_file_sink_options options_;  // 轮转配置。
  std::ofstream stream_;                // 当前打开的文件流。
  std::size_t current_size_{0};         // 当前文件已写入字节数。
  mutable std::mutex file_mutex_;       // 保护文件写入和轮转的互斥锁。
};

// 按天定时切分的文件 sink。
class daily_file_sink final : public sink {
public:
  // 构造时根据当前时间打开当天对应的日志文件。
  daily_file_sink(std::filesystem::path path,
                  daily_file_sink_options options = {})
      : path_(std::move(path)), options_(options) {
    if (options_.create_directories && path_.has_parent_path()) {
      std::filesystem::create_directories(path_.parent_path());
    }
    rotate_if_needed(std::chrono::system_clock::now());
    auto config = format_config_value();
    detail::apply_detail_mode_preset(&config, detail_mode::compact);
    set_format_config(config);
  }

  // 刷新当前文件。
  void flush() override {
    {
      std::lock_guard lock(file_mutex_);
      stream_.flush();
    }
    sink::flush();
  }

private:
  // 根据轮转时间生成对应的文件名。
  [[nodiscard]] auto current_filename(std::chrono::system_clock::time_point now) const
      -> std::filesystem::path {
    const auto stamp = detail::make_timestamp(now, time_mode::local, true);
    return path_.string() + "." + stamp + ".log";
  }

  // 检查是否到了新的轮转周期，若是则切换到新文件。
  void rotate_if_needed(std::chrono::system_clock::time_point now) {
    const auto time_value = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    detail::safe_localtime(time_value, &tm);

    std::tm period_tm = tm;
    const auto before_rotation =
        tm.tm_hour < options_.rotation_hour ||
        (tm.tm_hour == options_.rotation_hour &&
         tm.tm_min < options_.rotation_minute);
    if (before_rotation) {
      period_tm.tm_mday -= 1;
      std::mktime(&period_tm);
    }

    period_tm.tm_hour = options_.rotation_hour;
    period_tm.tm_min = options_.rotation_minute;
    period_tm.tm_sec = 0;

    const auto period_key = std::format(
        "{:04}-{:02}-{:02}-{:02}-{:02}", period_tm.tm_year + 1900,
        period_tm.tm_mon + 1, period_tm.tm_mday, period_tm.tm_hour,
        period_tm.tm_min);

    if (period_key == active_period_key_ && stream_.is_open()) {
      return;
    }

    if (stream_.is_open()) {
      stream_.flush();
      stream_.close();
    }

    active_period_key_ = period_key;
    const auto filename = current_filename(
        std::chrono::system_clock::from_time_t(std::mktime(&period_tm)));
    stream_.open(filename, std::ios::out | std::ios::binary | std::ios::app);
    if (!stream_.is_open()) {
      throw std::runtime_error(std::format("failed to open daily log file: {}",
                                           filename.string()));
    }
  }

  // 写入一条日志，并在必要时切换到新文件。
  void sink_it(const std::string &rendered) override {
    std::lock_guard lock(file_mutex_);
    rotate_if_needed(std::chrono::system_clock::now());
    stream_.write(rendered.data(),
                  static_cast<std::streamsize>(rendered.size()));
    stream_.put('\n');
  }

  std::filesystem::path path_;     // 日志文件基础路径。
  daily_file_sink_options options_; // 日切配置。
  std::string active_period_key_;  // 当前已打开文件对应的周期键。
  std::ofstream stream_;           // 当前打开的日志文件流。
  std::mutex file_mutex_;          // 保护文件切换和写入的互斥锁。
};

// 异步 sink。
// 该类把前端线程产生的 log_record 放入有界队列，
// 再由后台线程批量消费并转发给真正的 inner sink。
class async_sink final : public sink {
public:
  // 构造时绑定一个真实 sink，并立即启动后台工作线程。
  async_sink(sink_ptr inner_sink, async_options options = {})
      : inner_sink_(std::move(inner_sink)), options_(options),
        queue_(std::max<std::size_t>(std::size_t{1}, options_.queue_size)),
        worker_(&async_sink::run, this) {}

  // 析构时保证后台线程被安全回收。
  ~async_sink() override { shutdown(); }

  async_sink(const async_sink &) = delete;
  auto operator=(const async_sink &) -> async_sink & = delete;

  // 获取异步 sink 的运行时统计快照。
  [[nodiscard]] auto stats() const -> sink_stats override {
    auto base = sink::stats();
    base.current_queue_depth = current_queue_depth_.load(std::memory_order_relaxed);
    base.peak_queue_depth = peak_queue_depth_.load(std::memory_order_relaxed);
    return base;
  }

  [[nodiscard]] auto required_metadata() const -> record_metadata override {
    std::lock_guard lock(inner_sink_mutex_);
    return inner_sink_->required_metadata();
  }

  // 异步写入入口。
  // 这里是整个异步路径最核心的前端逻辑，所以按步骤逐行说明：
  // 1. 先拿到队列锁，保证队列状态一致。
  // 2. 若策略为 block，则在队列满时等待空位。
  // 3. 若策略为 drop_new，则直接丢弃当前新消息。
  // 4. 若策略为 drop_oldest，则弹出最旧消息给新消息腾位置。
  // 5. 若已经进入 shutdown，则不再接受新消息。
  // 6. 成功入队后唤醒后台线程。
  void log(const log_record &record) override {
    {
      // 前端线程先独占队列状态。
      std::unique_lock lock(queue_mutex_);
      // block 策略：队列满了就等待消费者释放空间。
      if (options_.policy == overflow_policy::block) {
        queue_not_full_.wait(lock, [this] {
          return shutdown_requested_ || queue_size_ < queue_.size();
        });
      } else if (queue_size_ >= queue_.size()) {
        // drop_new 策略：直接丢弃当前这条新日志。
        if (options_.policy == overflow_policy::drop_new) {
          record_drop();
          return;
        }
        // drop_oldest 策略：先丢最老的一条，再接纳新日志。
        if (options_.policy == overflow_policy::drop_oldest && queue_size_ > 0) {
          queue_[queue_head_].reset();
          queue_head_ = (queue_head_ + 1) % queue_.size();
          --queue_size_;
          record_drop();
          completed_sequence_.fetch_add(1, std::memory_order_relaxed);
        }
      }

      // 若 logger 已进入关闭流程，则不再接收新日志。
      if (shutdown_requested_) {
        return;
      }

      // 真正把日志放入队列。
      const auto tail = (queue_head_ + queue_size_) % queue_.size();
      queue_[tail] = record;
      ++queue_size_;
      // 更新统计，表示已经成功进入异步队列。
      record_enqueue();
      current_queue_depth_.store(queue_size_, std::memory_order_relaxed);
      peak_queue_depth_.store(
          std::max<std::uint64_t>(peak_queue_depth_.load(std::memory_order_relaxed),
                                  static_cast<std::uint64_t>(queue_size_)),
          std::memory_order_relaxed);
      submitted_sequence_.fetch_add(1, std::memory_order_relaxed);
    }
    // 告诉后台线程：有新日志可以消费了。
    queue_not_empty_.notify_one();
  }

  // 主动 flush。
  // 这里不会只发出“尽快刷新”的信号，而是等待：
  // 1. 当前 flush 之前入队的消息全部被后台线程消费；
  // 2. 然后再真正调用 inner sink 的 flush；
  // 从而让 logger.flush() 对调用方具备可预期的语义。
  void flush() override {
    std::uint64_t target_sequence = 0;
    {
      std::lock_guard lock(queue_mutex_);
      target_sequence = submitted_sequence_.load(std::memory_order_relaxed);
    }
    queue_not_empty_.notify_one();
    {
      std::unique_lock lock(queue_mutex_);
      queue_drained_.wait(lock, [this, target_sequence] {
        return completed_sequence_.load(std::memory_order_relaxed) >=
                   target_sequence &&
               queue_size_ == 0;
      });
    }
    std::lock_guard lock(inner_sink_mutex_);
    inner_sink_->flush();
    record_flush();
  }

  // 关闭异步 sink。
  // 该接口会：
  // 1. 只执行一次 shutdown；
  // 2. 唤醒所有等待中的线程；
  // 3. join 后台线程；
  // 4. 最后再刷一次 inner sink。
  void shutdown() {
    bool expected = false;
    if (!shutdown_requested_.compare_exchange_strong(expected, true)) {
      return;
    }
    queue_not_empty_.notify_all();
    queue_not_full_.notify_all();
    if (worker_.joinable()) {
      worker_.join();
    }
    std::lock_guard lock(inner_sink_mutex_);
    inner_sink_->flush();
  }

private:
  // async_sink 不直接输出文本，所以这个函数是空实现。
  void sink_it(const std::string &) override {}

  // 后台消费线程主循环。
  // 这段逻辑是异步 sink 的核心，因此逐行注释：
  // 1. 维护一个下次周期性 flush 的截止时间；
  // 2. 等待“有新日志”或“到达 flush 时间”两种事件；
  // 3. 将共享队列整体 swap 到本地 batch，缩短持锁时间；
  // 4. 逐条转发给 inner sink；
  // 5. 到达 flush 周期时执行底层 flush；
  // 6. 退出前把残留队列全部消费完。
  void run() {
    // 记录第一次周期性 flush 的时间点。
    auto next_flush_deadline =
        std::chrono::steady_clock::now() + options_.flush_interval;
    for (;;) {
      // 后台线程每轮都先准备一个本地批次，减少锁内工作。
      std::vector<log_record> batch;
      {
        // 加锁后统一检查“有新日志”或“需要退出/flush”的状态。
        std::unique_lock lock(queue_mutex_);
        queue_not_empty_.wait_until(lock, next_flush_deadline, [this] {
          return shutdown_requested_.load() || queue_size_ > 0;
        });

        // 若收到 shutdown 且队列为空，说明可以安全退出主循环。
        if (queue_size_ == 0 && shutdown_requested_) {
          break;
        }

        // 把共享队列搬到本地批次，减少后续持锁时间。
        batch.reserve(queue_size_);
        while (queue_size_ > 0) {
          auto &slot = queue_[queue_head_];
          if (slot.has_value()) {
            batch.push_back(std::move(*slot));
            slot.reset();
          }
          queue_head_ = (queue_head_ + 1) % queue_.size();
          --queue_size_;
        }
        current_queue_depth_.store(queue_size_, std::memory_order_relaxed);
        // 释放了队列空间后，通知可能阻塞中的生产者线程继续写入。
        queue_not_full_.notify_all();
      }

      // 真正的 I/O 或格式化操作放在锁外执行，避免阻塞前端线程。
      for (const auto &record : batch) {
        try {
          std::lock_guard lock(inner_sink_mutex_);
          inner_sink_->log(record);
        } catch (...) {
          // 对商用日志库来说，后台线程不能因为单条日志失败而崩掉。
          // 这里吞掉异常，保证日志系统自身不会拖垮业务线程。
        }
        completed_sequence_.fetch_add(1, std::memory_order_relaxed);
      }
      queue_drained_.notify_all();

      // 到了周期性 flush 时间则主动刷一次 inner sink。
      const auto now = std::chrono::steady_clock::now();
      if (now >= next_flush_deadline) {
        std::lock_guard lock(inner_sink_mutex_);
        inner_sink_->flush();
        next_flush_deadline = now + options_.flush_interval;
      }
    }

    // 主循环退出后，再把尾部残留日志做一次兜底消费。
    std::vector<log_record> tail;
    {
      std::lock_guard lock(queue_mutex_);
      tail.reserve(queue_size_);
      while (queue_size_ > 0) {
        auto &slot = queue_[queue_head_];
        if (slot.has_value()) {
          tail.push_back(std::move(*slot));
          slot.reset();
        }
        queue_head_ = (queue_head_ + 1) % queue_.size();
        --queue_size_;
      }
      current_queue_depth_.store(queue_size_, std::memory_order_relaxed);
      queue_not_full_.notify_all();
    }
    for (const auto &record : tail) {
      try {
        std::lock_guard lock(inner_sink_mutex_);
        inner_sink_->log(record);
      } catch (...) {
      }
      completed_sequence_.fetch_add(1, std::memory_order_relaxed);
    }
    queue_drained_.notify_all();
  }

  sink_ptr inner_sink_; // 真正执行 I/O 的下游 sink。
  mutable std::mutex inner_sink_mutex_; // 串行化对下游 sink 的 log/flush 调用。
  async_options options_; // 异步队列和刷新策略配置。
  std::vector<std::optional<log_record>> queue_; // 预分配有界环形队列。
  std::size_t queue_head_{0}; // 下一条待消费记录的位置。
  std::size_t queue_size_{0}; // 当前环形队列中的记录数。
  std::mutex queue_mutex_; // 保护队列状态的互斥锁。
  std::condition_variable queue_not_empty_; // 通知后台线程“队列里有新日志”。
  std::condition_variable queue_not_full_; // 通知前端线程“队列里腾出了空位”。
  std::condition_variable queue_drained_; // 通知 flush 调用方“指定批次已全部消费”。
  std::atomic<bool> shutdown_requested_{false}; // 是否已进入关闭流程。
  std::atomic<std::uint64_t> submitted_sequence_{0}; // 已成功入队的消息序号。
  std::atomic<std::uint64_t> completed_sequence_{0}; // 已被后台线程消费完成的消息序号。
  std::atomic<std::uint64_t> current_queue_depth_{0}; // 当前队列深度。
  std::atomic<std::uint64_t> peak_queue_depth_{0}; // 历史峰值队列深度。
  std::thread worker_; // 后台消费线程。
};

// 创建默认 stdout sink。
inline auto make_stdout_sink() -> sink_ptr {
  auto sink_ptr_value = std::make_shared<stream_sink>(std::cout);
  auto config = sink_ptr_value->format_config_value();
  config.colorize = true;
  sink_ptr_value->set_format_config(config);
  return sink_ptr_value;
}

// 创建默认 stderr sink。
inline auto make_stderr_sink() -> sink_ptr {
  auto sink_ptr_value = std::make_shared<stream_sink>(std::cerr);
  auto config = sink_ptr_value->format_config_value();
  config.colorize = true;
  sink_ptr_value->set_format_config(config);
  return sink_ptr_value;
}

// 创建空 sink。
inline auto make_null_sink() -> sink_ptr { return std::make_shared<null_sink>(); }

// 创建分发 sink。
inline auto make_fanout_sink(std::vector<sink_ptr> sinks) -> sink_ptr {
  return std::make_shared<fanout_sink>(std::move(sinks));
}

// 创建基础文件 sink。
inline auto make_basic_file_sink(const std::filesystem::path &path,
                                 file_sink_options options = {}) -> sink_ptr {
  return std::make_shared<basic_file_sink>(path, options);
}

// 创建大小轮转文件 sink。
inline auto make_rotating_file_sink(
    const std::filesystem::path &path,
    rotating_file_sink_options options = {}) -> sink_ptr {
  return std::make_shared<rotating_file_sink>(path, options);
}

// 创建按天切分文件 sink。
inline auto make_daily_file_sink(
    const std::filesystem::path &path,
    daily_file_sink_options options = {}) -> sink_ptr {
  return std::make_shared<daily_file_sink>(path, options);
}

// 创建回调 sink。
inline auto make_callback_sink(callback_sink::callback_type callback) -> sink_ptr {
  return std::make_shared<callback_sink>(std::move(callback));
}

// 创建异步 sink 包装器。
inline auto make_async_sink(sink_ptr inner_sink,
                            async_options options = {}) -> sink_ptr {
  return std::make_shared<async_sink>(std::move(inner_sink), options);
}

} // namespace fastlog
