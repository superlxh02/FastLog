#include "fastlog/fastlog.hpp"

#include <spdlog/async.h>
#include <spdlog/details/thread_pool.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <print>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct run_result {
  double seconds{0.0};
  double messages_per_second{0.0};
};

class latch {
public:
  explicit latch(std::size_t target) : target_(target) {}

  void arrive() {
    const auto current = count_.fetch_add(1, std::memory_order_release) + 1;
    if (current >= target_) {
      std::lock_guard lock(mutex_);
      condition_.notify_all();
    }
  }

  void wait() {
    std::unique_lock lock(mutex_);
    condition_.wait(lock, [this] {
      return count_.load(std::memory_order_acquire) >= target_;
    });
  }

private:
  std::size_t target_{0};
  std::atomic<std::size_t> count_{0};
  std::mutex mutex_;
  std::condition_variable condition_;
};

class fastlog_counting_sink final : public fastlog::sink {
public:
  explicit fastlog_counting_sink(std::shared_ptr<latch> latch_value)
      : latch_(std::move(latch_value)) {}

  void log(const fastlog::log_record &record) override {
    if (record.level < level()) {
      return;
    }
    latch_->arrive();
    if (record.level >= flush_on()) {
      flush();
    }
  }

  [[nodiscard]] auto required_metadata() const
      -> fastlog::record_metadata override {
    return {.message = false,
            .timestamp = false,
            .thread_id = false,
            .process_id = false,
            .source_location = false};
  }

private:
  void sink_it(const std::string &) override { latch_->arrive(); }

  std::shared_ptr<latch> latch_;
};

class spdlog_counting_sink final
    : public spdlog::sinks::base_sink<std::mutex> {
public:
  explicit spdlog_counting_sink(std::shared_ptr<latch> latch_value)
      : latch_(std::move(latch_value)) {}

protected:
  void sink_it_(const spdlog::details::log_msg &) override { latch_->arrive(); }
  void flush_() override {}

private:
  std::shared_ptr<latch> latch_;
};

template <typename Work>
auto measure(std::string_view name, std::size_t messages, Work &&work)
    -> run_result {
  const auto started = std::chrono::steady_clock::now();
  work();
  const auto elapsed = std::chrono::steady_clock::now() - started;
  const auto seconds = std::chrono::duration<double>(elapsed).count();
  const auto rate = static_cast<double>(messages) / seconds;
  std::println("{:<30} {:>10} messages in {:>8.3f}s  {:>12.0f} msg/s", name,
               messages, seconds, rate);
  return {.seconds = seconds, .messages_per_second = rate};
}

void run_threaded(std::size_t messages, std::size_t threads,
                  const auto &log_one) {
  std::vector<std::thread> workers;
  workers.reserve(threads);
  const auto per_thread = messages / threads;
  const auto remainder = messages % threads;
  for (std::size_t thread_index = 0; thread_index < threads; ++thread_index) {
    const auto count = per_thread + (thread_index < remainder ? 1 : 0);
    workers.emplace_back([&, count, thread_index] {
      for (std::size_t i = 0; i < count; ++i) {
        log_one(thread_index, i);
      }
    });
  }
  for (auto &worker : workers) {
    worker.join();
  }
}

void print_ratio(std::string_view label, const run_result &fastlog_result,
                 const run_result &spdlog_result) {
  const auto ratio =
      fastlog_result.messages_per_second / spdlog_result.messages_per_second;
  std::println("{:<30} FastLog/spdlog = {:>6.2f}x", label, ratio);
}

auto make_fastlog_sync(std::size_t messages)
    -> std::pair<fastlog::logger_ptr, std::shared_ptr<latch>> {
  auto completed = std::make_shared<latch>(messages);
  auto sink = std::make_shared<fastlog_counting_sink>(completed);
  sink->set_pattern("%v");
  auto logger =
      fastlog::pipeline("fastlog_compare_sync")
          .at(fastlog::log_level::trace)
          .write_to(std::move(sink))
          .make();
  return {std::move(logger), std::move(completed)};
}

auto make_fastlog_async(std::size_t messages)
    -> std::pair<fastlog::logger_ptr, std::shared_ptr<latch>> {
  auto completed = std::make_shared<latch>(messages);
  auto sink = std::make_shared<fastlog_counting_sink>(completed);
  sink->set_pattern("%v");
  auto logger =
      fastlog::pipeline("fastlog_compare_async")
          .at(fastlog::log_level::trace)
          .write_to_async(std::move(sink),
                          {.queue_size = 262'144,
                           .policy = fastlog::overflow_policy::block,
                           .flush_interval = std::chrono::milliseconds(100)})
          .make();
  return {std::move(logger), std::move(completed)};
}

auto make_spdlog_sync(std::size_t messages)
    -> std::pair<std::shared_ptr<spdlog::logger>, std::shared_ptr<latch>> {
  auto completed = std::make_shared<latch>(messages);
  auto sink = std::make_shared<spdlog_counting_sink>(completed);
  auto logger = std::make_shared<spdlog::logger>("spdlog_compare_sync", sink);
  logger->set_level(spdlog::level::trace);
  logger->set_pattern("%v");
  return {std::move(logger), std::move(completed)};
}

auto make_spdlog_async(std::size_t messages)
    -> std::tuple<std::shared_ptr<spdlog::logger>, std::shared_ptr<latch>,
                  std::shared_ptr<spdlog::details::thread_pool>> {
  auto completed = std::make_shared<latch>(messages);
  auto sink = std::make_shared<spdlog_counting_sink>(completed);
  auto thread_pool =
      std::make_shared<spdlog::details::thread_pool>(262'144, 1);
  auto logger = std::make_shared<spdlog::async_logger>(
      "spdlog_compare_async", std::move(sink), thread_pool,
      spdlog::async_overflow_policy::block);
  logger->set_level(spdlog::level::trace);
  logger->set_pattern("%v");
  return {std::move(logger), std::move(completed), std::move(thread_pool)};
}

} // namespace

int main(int argc, char **argv) {
  const auto messages =
      argc > 1 ? static_cast<std::size_t>(std::stoull(argv[1])) : 1'000'000U;
  const auto threads =
      argc > 2 ? static_cast<std::size_t>(std::stoull(argv[2]))
               : std::max<std::size_t>(1, std::thread::hardware_concurrency());

  std::println("FastLog vs spdlog comparison");
  std::println("messages={}, threads={}", messages, threads);
  std::println("Each async case waits until the counting sink observes all messages.\n");

  auto [fastlog_sync, fastlog_sync_done] = make_fastlog_sync(messages);
  const auto fastlog_sync_result = measure("fastlog sync/count", messages, [&] {
    for (std::size_t i = 0; i < messages; ++i) {
      fastlog_sync->info("event {}", i);
    }
    fastlog_sync->flush();
    fastlog_sync_done->wait();
  });

  auto [spdlog_sync, spdlog_sync_done] = make_spdlog_sync(messages);
  const auto spdlog_sync_result = measure("spdlog sync/count", messages, [&] {
    for (std::size_t i = 0; i < messages; ++i) {
      spdlog_sync->info("event {}", i);
    }
    spdlog_sync->flush();
    spdlog_sync_done->wait();
  });

  auto [fastlog_async, fastlog_async_done] = make_fastlog_async(messages);
  const auto fastlog_async_result = measure("fastlog async/count", messages, [&] {
    for (std::size_t i = 0; i < messages; ++i) {
      fastlog_async->info("event {}", i);
    }
    fastlog_async->flush();
    fastlog_async_done->wait();
  });

  auto [spdlog_async, spdlog_async_done, spdlog_async_pool] =
      make_spdlog_async(messages);
  const auto spdlog_async_result = measure("spdlog async/count", messages, [&] {
    for (std::size_t i = 0; i < messages; ++i) {
      spdlog_async->info("event {}", i);
    }
    spdlog_async->flush();
    spdlog_async_done->wait();
  });

  auto [fastlog_threaded, fastlog_threaded_done] = make_fastlog_async(messages);
  const auto fastlog_threaded_result =
      measure("fastlog async/threaded", messages, [&] {
        run_threaded(messages, threads, [&](std::size_t thread_index,
                                            std::size_t message_index) {
          fastlog_threaded->info("event {} {}", thread_index, message_index);
        });
        fastlog_threaded->flush();
        fastlog_threaded_done->wait();
      });

  auto [spdlog_threaded, spdlog_threaded_done, spdlog_threaded_pool] =
      make_spdlog_async(messages);
  const auto spdlog_threaded_result =
      measure("spdlog async/threaded", messages, [&] {
        run_threaded(messages, threads, [&](std::size_t thread_index,
                                            std::size_t message_index) {
          spdlog_threaded->info("event {} {}", thread_index, message_index);
        });
        spdlog_threaded->flush();
        spdlog_threaded_done->wait();
      });

  std::println("\nRatios");
  print_ratio("sync/count", fastlog_sync_result, spdlog_sync_result);
  print_ratio("async/count", fastlog_async_result, spdlog_async_result);
  print_ratio("async/threaded", fastlog_threaded_result,
              spdlog_threaded_result);

  spdlog::drop_all();
  (void)spdlog_async_pool;
  (void)spdlog_threaded_pool;
  return 0;
}
