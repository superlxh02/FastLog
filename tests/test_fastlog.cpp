#include "fastlog/fastlog.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

auto has_path_prefix(const std::filesystem::path &path,
                     std::string_view prefix) -> bool {
  const auto filename = path.filename().string();
  return filename.rfind(std::string(prefix), 0) == 0;
}

auto read_last_line(const std::filesystem::path &path) -> std::string {
  std::ifstream stream(path);
  std::string line;
  std::string last_line;
  while (std::getline(stream, line)) {
    if (!line.empty()) {
      last_line = line;
    }
  }
  return last_line;
}

auto read_all_lines(const std::filesystem::path &path) -> std::vector<std::string> {
  std::ifstream stream(path);
  std::vector<std::string> lines;
  for (std::string line; std::getline(stream, line);) {
    if (!line.empty()) {
      lines.push_back(line);
    }
  }
  return lines;
}

} // namespace

namespace {

class serialized_probe_sink final : public fastlog::sink {
public:
  explicit serialized_probe_sink(std::atomic<int> *max_inflight)
      : max_inflight_(max_inflight) {}

  void flush() override {
    enter();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    leave();
    fastlog::sink::flush();
  }

private:
  void sink_it(const std::string &) override {
    enter();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    leave();
  }

  void enter() {
    const auto inflight = current_inflight_.fetch_add(1) + 1;
    auto observed = max_inflight_->load();
    while (inflight > observed &&
           !max_inflight_->compare_exchange_weak(observed, inflight)) {
    }
  }

  void leave() { current_inflight_.fetch_sub(1); }

  std::atomic<int> current_inflight_{0};
  std::atomic<int> *max_inflight_{nullptr};
};

} // namespace

int main() {
  std::mutex captured_mutex;
  std::vector<std::string> console_lines;
  std::vector<std::string> logger_lines;
  std::vector<std::string> filename_lines;
  std::vector<std::string> absolute_lines;

  auto console_sink = fastlog::make_callback_sink([&](std::string_view line) {
    std::lock_guard lock(captured_mutex);
    console_lines.emplace_back(line);
  });
  console_sink->set_pattern("%n|%l|%v");

  auto default_logger = fastlog::create_logger(
      "console_capture", {console_sink}, fastlog::log_level::trace);
  fastlog::set_console_logger(default_logger);
  fastlog::set_default_logger(default_logger);
  fastlog::set_console_level(fastlog::log_level::trace);
  fastlog::set_console_detail_mode(fastlog::detail_mode::compact);

  fastlog::console.info("console facade {}", 1);
  fastlog::console.warn("console warn");
  fastlog::console.flush();

  auto console_style_sink = fastlog::make_callback_sink([&](std::string_view line) {
    std::lock_guard lock(captured_mutex);
    console_lines.emplace_back(line);
  });
  auto console_style_logger = fastlog::create_logger(
      "console_style", {console_style_sink}, fastlog::log_level::trace);
  fastlog::set_console_logger(console_style_logger);
  fastlog::set_console_detail_mode(fastlog::detail_mode::compact);
  fastlog::console.info("compact style");
  fastlog::set_console_detail_mode(fastlog::detail_mode::standard);
  fastlog::console.warn("standard style");
  fastlog::set_console_detail_mode(fastlog::detail_mode::full);
  fastlog::console.error("full style");
  fastlog::console.flush();

  auto callback = fastlog::make_callback_sink([&](std::string_view line) {
    std::lock_guard lock(captured_mutex);
    logger_lines.emplace_back(line);
  });
  callback->set_pattern("%n|%l|%v");

  auto logger = fastlog::create_logger(
      "test_logger", {callback}, fastlog::log_level::trace);
  logger->enable_backtrace(8);
  logger->info("hello {}", 42);
  logger->warn("warn line");
  logger->flush_backtrace();
  logger->flush();

  auto filename_sink = fastlog::make_callback_sink([&](std::string_view line) {
    std::lock_guard lock(captured_mutex);
    filename_lines.emplace_back(line);
  });
  filename_sink->set_pattern("%s|%#|%v");
  auto filename_config = filename_sink->format_config_value();
  filename_config.source_path = fastlog::source_path_mode::filename;
  filename_sink->set_format_config(filename_config);

  auto absolute_sink = fastlog::make_callback_sink([&](std::string_view line) {
    std::lock_guard lock(captured_mutex);
    absolute_lines.emplace_back(line);
  });
  absolute_sink->set_pattern("%s|%#|%v");
  auto absolute_config = absolute_sink->format_config_value();
  absolute_config.source_path = fastlog::source_path_mode::absolute;
  absolute_sink->set_format_config(absolute_config);

  auto path_logger = fastlog::create_logger(
      "path_logger", {filename_sink, absolute_sink}, fastlog::log_level::trace);
  path_logger->info("path check");
  path_logger->flush();

  auto basic_sink = fastlog::make_basic_file_sink("logs/basic.log");
  auto basic_logger = fastlog::create_logger(
      "basic_logger", {basic_sink}, fastlog::log_level::trace);
  basic_logger->info("basic file write");
  basic_logger->flush();

  auto rotating = fastlog::make_rotating_file_sink(
      "logs/test_rotating.log", {.max_file_size = 128, .max_files = 2});
  auto async_rotating = fastlog::make_async_sink(
      rotating, {.queue_size = 64, .policy = fastlog::overflow_policy::block});
  auto file_logger = fastlog::create_logger(
      "file_logger", {async_rotating}, fastlog::log_level::trace);
  for (int i = 0; i < 100; ++i) {
    file_logger->info("line {}", i);
  }
  file_logger->flush();

  const auto async_stats = async_rotating->stats();
  assert(async_stats.dropped_messages == 0);
  assert(async_stats.enqueued_messages == 100);
  assert(async_stats.current_queue_depth == 0);
  assert(async_stats.peak_queue_depth > 0);

  auto daily_sink = fastlog::make_daily_file_sink("logs/daily_test");
  auto daily_logger = fastlog::create_logger(
      "daily_logger", {daily_sink}, fastlog::log_level::trace);
  daily_logger->info("daily line");
  daily_logger->flush();

  fastlog::FileLoggerOptions simple_options{
      .level = fastlog::log_level::trace,
      .detail_mode = fastlog::detail_mode::compact,
      .source_path = fastlog::source_path_mode::filename,
      .source_root = std::filesystem::current_path(),
      .max_file_size = 1024 * 32,
      .max_files = 2,
      .async_write = true};
  auto simple_file_logger =
      fastlog::file::make_logger("simple_file", "logs/simple_file.log", simple_options);
  fastlog::file::set_level(simple_file_logger, fastlog::log_level::trace);
  fastlog::file::set_detail_mode(simple_file_logger, fastlog::detail_mode::full);
  fastlog::file::set_source_path_mode(
      simple_file_logger,
      fastlog::source_path_mode::absolute,
      std::filesystem::current_path());
  fastlog::file::set_max_file_size(simple_file_logger, 1024 * 64);
  simple_file_logger.info("simple facade works");
  fastlog::file::flush(simple_file_logger);
  const auto simple_lookup = fastlog::file::get_logger("simple_file");
  assert(simple_lookup);
  assert(simple_lookup.name() == simple_file_logger.name());

  fastlog::file::delete_logger("simple_file");
  const auto deleted_lookup = fastlog::file::get_logger("simple_file");
  assert(!deleted_lookup);
  simple_file_logger.info("handle survives delete");
  fastlog::file::flush(simple_file_logger);

  auto level_filtered_sink = fastlog::make_callback_sink([](std::string_view) {});
  level_filtered_sink->set_level(fastlog::log_level::error);
  auto level_logger = fastlog::create_logger(
      "level_logger", {level_filtered_sink}, fastlog::log_level::trace);
  level_logger->info("filtered");
  level_logger->error("kept");
  level_logger->flush();

  try {
    throw fastlog::runtime_error("broadcast failure");
  } catch (const std::exception &exception) {
    fastlog::log_exception_to_all(exception);
  }

  try {
    throw fastlog::runtime_error("current exception");
  } catch (...) {
    fastlog::log_current_exception_to_all("broadcast current");
  }

  try {
    throw fastlog::runtime_error("stacktrace failure");
  } catch (...) {
    const auto exception_stack = fastlog::current_exception_stacktrace();
    assert(!exception_stack.empty());
  }

  const auto current_stack = fastlog::current_stacktrace();
  assert(!current_stack.empty());

  {
    auto lifetime_logger =
        fastlog::file::make_logger("lifetime_file", "logs/lifetime_file.log",
                                   simple_options);
    std::atomic<bool> stop{false};
    std::thread writer([&] {
      for (int i = 0; i < 200; ++i) {
        lifetime_logger.info("lifetime {}", i);
      }
      stop.store(true);
    });
    std::thread remover([&] {
      while (!stop.load()) {
        fastlog::file::delete_logger("lifetime_file");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    });
    writer.join();
    remover.join();
    lifetime_logger.info("after delete still valid");
    lifetime_logger.flush();
  }

  {
    std::atomic<int> max_inflight{0};
    auto probe = std::make_shared<serialized_probe_sink>(&max_inflight);
    auto async_probe =
        fastlog::make_async_sink(probe, {.queue_size = 64, .policy = fastlog::overflow_policy::block});
    auto probe_logger = fastlog::create_logger(
        "probe_logger", {async_probe}, fastlog::log_level::trace);

    std::thread producer([&] {
      for (int i = 0; i < 200; ++i) {
        probe_logger->info("probe {}", i);
      }
    });
    std::thread flusher([&] {
      for (int i = 0; i < 50; ++i) {
        probe_logger->flush();
      }
    });
    producer.join();
    flusher.join();
    probe_logger->flush();
    assert(max_inflight.load() == 1);
  }

  {
    std::atomic<int> callback_inflight{0};
    std::atomic<int> callback_max_inflight{0};
    auto callback_sink = fastlog::make_callback_sink([&](std::string_view) {
      const auto inflight = callback_inflight.fetch_add(1) + 1;
      auto observed = callback_max_inflight.load();
      while (inflight > observed &&
             !callback_max_inflight.compare_exchange_weak(observed, inflight)) {
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      callback_inflight.fetch_sub(1);
    });
    auto callback_logger = fastlog::create_logger(
        "callback_logger", {callback_sink}, fastlog::log_level::trace);

    std::vector<std::thread> threads;
    threads.reserve(8);
    for (int i = 0; i < 8; ++i) {
      threads.emplace_back([&, i] {
        for (int j = 0; j < 20; ++j) {
          callback_logger->info("callback {} {}", i, j);
        }
      });
    }
    for (auto &thread : threads) {
      thread.join();
    }
    callback_logger->flush();
    assert(callback_max_inflight.load() == 1);
  }

  { 
    const auto basic_line = read_last_line("logs/basic.log");
    assert(basic_line.find("[INFO]") != std::string::npos);
    assert(basic_line.find("[basic_logger]") == std::string::npos);
    assert(basic_line.find("basic file write") != std::string::npos);
    assert(basic_line.find(".cpp:") == std::string::npos);
  }

  {
    const auto simple_lines = read_all_lines("logs/simple_file.log");
    assert(!simple_lines.empty());
    auto found_full_line = false;
    for (const auto &line : simple_lines) {
      if (line.find("[simple_file]") != std::string::npos &&
          line.find("/Users/lxh/workspace/cpp/FastLog/tests/test_fastlog.cpp") !=
              std::string::npos) {
        found_full_line = true;
        break;
      }
    }
    assert(found_full_line);
  }

  {
    std::lock_guard lock(captured_mutex);
    assert(console_lines.size() >= 5);
    assert(console_lines[0].find("console_capture|INFO|console facade 1") !=
           std::string::npos);
    assert(console_lines[2].find("[INFO] compact style") != std::string::npos);
    assert(console_lines[2].find("[console_style]") == std::string::npos);
    assert(console_lines[3].find("[WARN]") != std::string::npos);
    assert(console_lines[3].find("[pid:") != std::string::npos);
    assert(console_lines[3].find("test_fastlog.cpp") != std::string::npos);
    assert(console_lines[3].find("/Users/lxh/workspace/cpp/FastLog/tests/test_fastlog.cpp") ==
           std::string::npos);
    assert(console_lines[4].find("[ERROR]") != std::string::npos);
    assert(console_lines[4].find("[console_style]") == std::string::npos);
    assert(console_lines[4].find("/Users/lxh/workspace/cpp/FastLog/tests/test_fastlog.cpp") !=
           std::string::npos);
    assert(!logger_lines.empty());
    assert(logger_lines[0].find("test_logger|INFO|hello 42") != std::string::npos);
    assert(!filename_lines.empty());
    assert(!absolute_lines.empty());
    assert(filename_lines.back().rfind("test_fastlog.cpp|", 0) == 0);
    assert(absolute_lines.back().rfind("/Users/lxh/workspace/cpp/FastLog/tests/test_fastlog.cpp|",
                                       0) == 0);
    assert(absolute_lines.back().find("/Users/lxh/workspace/cpp/FastLog/tests/test_fastlog.cpp") !=
           std::string::npos);
  }

  auto daily_found = false;
  for (const auto &entry : std::filesystem::directory_iterator("logs")) {
    if (entry.is_regular_file() &&
        has_path_prefix(entry.path(), "daily_test.")) {
      daily_found = true;
      break;
    }
  }

  assert(std::filesystem::exists("logs/basic.log"));
  assert(std::filesystem::exists("logs/test_rotating.log"));
  assert(std::filesystem::exists("logs/simple_file.log"));
  assert(std::filesystem::exists("logs/lifetime_file.log"));
  assert(daily_found);

  fastlog::file::delete_logger("lifetime_file");
  fastlog::drop_logger("test_logger");
  fastlog::drop_logger("path_logger");
  fastlog::drop_logger("basic_logger");
  fastlog::drop_logger("file_logger");
  fastlog::drop_logger("daily_logger");
  fastlog::drop_logger("level_logger");
  fastlog::drop_logger("console_style");
  fastlog::drop_logger("probe_logger");
  fastlog::drop_logger("callback_logger");

  return 0;
}
