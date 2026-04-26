#include "fastlog/fastlog.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <print>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

template <typename Work>
void measure(std::string_view name, std::size_t messages, Work &&work) {
  const auto started = std::chrono::steady_clock::now();
  work();
  const auto elapsed = std::chrono::steady_clock::now() - started;
  const auto seconds = std::chrono::duration<double>(elapsed).count();
  const auto rate = static_cast<double>(messages) / seconds;
  std::println("{:<24} {:>10} messages in {:>8.3f}s  {:>12.0f} msg/s", name,
               messages, seconds, rate);
}

} // namespace

int main(int argc, char **argv) {
  const auto messages =
      argc > 1 ? static_cast<std::size_t>(std::stoull(argv[1])) : 1'000'000U;
  const auto threads =
      argc > 2 ? static_cast<std::size_t>(std::stoull(argv[2]))
               : std::max<std::size_t>(1, std::thread::hardware_concurrency());

  auto sync_logger =
      fastlog::pipeline("bench_sync")
          .at(fastlog::log_level::trace)
          .format_as("%v")
          .write_to(fastlog::make_null_sink())
          .make();

  measure("sync/null", messages, [&] {
    for (std::size_t i = 0; i < messages; ++i) {
      sync_logger->info("event {}", i);
    }
    sync_logger->flush();
  });

  auto async_logger =
      fastlog::pipeline("bench_async")
          .at(fastlog::log_level::trace)
          .format_as("%v")
          .write_to_async(fastlog::make_null_sink(),
                          {.queue_size = 262'144,
                           .policy = fastlog::overflow_policy::block,
                           .flush_interval = std::chrono::milliseconds(100)})
          .make();

  measure("async/null", messages, [&] {
    for (std::size_t i = 0; i < messages; ++i) {
      async_logger->info("event {}", i);
    }
    async_logger->flush();
  });

  auto threaded_logger =
      fastlog::pipeline("bench_threads")
          .at(fastlog::log_level::trace)
          .format_as("%v")
          .write_to_async(fastlog::make_null_sink(),
                          {.queue_size = 262'144,
                           .policy = fastlog::overflow_policy::block,
                           .flush_interval = std::chrono::milliseconds(100)})
          .make();

  measure("async/null/threaded", messages, [&] {
    std::vector<std::thread> workers;
    workers.reserve(threads);
    const auto per_thread = messages / threads;
    const auto remainder = messages % threads;
    for (std::size_t thread_index = 0; thread_index < threads; ++thread_index) {
      const auto count = per_thread + (thread_index < remainder ? 1 : 0);
      workers.emplace_back([threaded_logger, count, thread_index] {
        for (std::size_t i = 0; i < count; ++i) {
          threaded_logger->info("event {} {}", thread_index, i);
        }
      });
    }
    for (auto &worker : workers) {
      worker.join();
    }
    threaded_logger->flush();
  });

  return 0;
}
