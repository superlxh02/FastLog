#include "fastlog/fastlog.hpp"

#include <filesystem>
#include <thread>
#include <vector>

namespace {

// 构造一个混合同步控制台和异步轮转文件的高级 logger。
auto build_app_logger() -> fastlog::logger_ptr {
  auto console_sink = fastlog::make_stdout_sink();
  auto console_config = console_sink->format_config_value();
  fastlog::detail::apply_detail_mode_preset(&console_config,
                                            fastlog::detail_mode::compact);
  console_config.colorize = true;
  console_sink->set_format_config(console_config);

  fastlog::rotating_file_sink_options rotating_options;
  rotating_options.max_file_size = 1024 * 256;
  rotating_options.max_files = 3;
  rotating_options.create_directories = true;
  auto file_sink =
      fastlog::make_rotating_file_sink("logs/advanced_app.log", rotating_options);
  auto file_config = file_sink->format_config_value();
  fastlog::detail::apply_detail_mode_preset(&file_config,
                                            fastlog::detail_mode::full);
  file_config.source_root = std::filesystem::current_path();
  file_sink->set_format_config(file_config).set_flush_on(fastlog::log_level::error);

  auto async_file_sink = fastlog::make_async_sink(
      file_sink,
      {.queue_size = 4096,
       .policy = fastlog::overflow_policy::block,
       .flush_interval = std::chrono::milliseconds(200)});

  auto logger = fastlog::create_logger(
      "advanced_app", {console_sink, async_file_sink}, fastlog::log_level::trace);
  logger->enable_backtrace(32).set_flush_on(fastlog::log_level::error);
  return logger;
}

} // namespace

int main() {
  auto logger = build_app_logger();
  fastlog::set_default_logger(logger);

  logger->info("advanced example started");
  logger->debug("vector size = {}", std::vector<int>{1, 2, 3, 4}.size());

  std::thread worker([logger] {
    for (int i = 0; i < 100; ++i) {
      logger->info("worker iteration {}", i);
    }
  });
  worker.join();

  try {
    throw fastlog::runtime_error("advanced example failure for stacktrace output");
  } catch (const std::exception &exception) {
    logger->exception(exception);
  }

  logger->flush_backtrace();
  logger->warn("current stacktrace:\n{}", fastlog::current_stacktrace());
  logger->flush();

  return 0;
}
