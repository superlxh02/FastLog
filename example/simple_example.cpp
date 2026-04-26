#include "fastlog/fastlog.hpp"

#include <filesystem>
#include <vector>

namespace {

// 最基础的终端打印示例。
void demo_basic_console() {
  fastlog::set_console_level(fastlog::log_level::trace);
  fastlog::set_console_detail_mode(fastlog::detail_mode::compact);
  fastlog::console.trace("trace hello world");
  fastlog::console.debug("debug hello world");
  fastlog::console.info("info hello world");
  fastlog::console.warn("warn hello world");
  fastlog::console.error("error hello world");
  fastlog::console.fatal("fatal hello world");
}

// 最基础的终端配置示例。
void demo_configure_console() {
  fastlog::set_console_level(fastlog::log_level::trace);
  fastlog::set_console_detail_mode(fastlog::detail_mode::compact);
  fastlog::console.info(" hello world with compact mode");
  fastlog::set_console_detail_mode(fastlog::detail_mode::standard);
  fastlog::console.info("hello world with standard mode");
  fastlog::set_console_detail_mode(fastlog::detail_mode::full);
  fastlog::console.info("hello world with full mode");
  fastlog::set_console_detail_mode(fastlog::detail_mode::compact);
}

// 完全自定义 pattern 的终端输出示例。
void demo_custom_console_pattern() {
  auto sink = fastlog::make_stdout_sink();
  auto config = sink->format_config_value();
  config.timestamp_with_microseconds = false;
  config.source_path = fastlog::source_path_mode::relative;
  config.source_root = std::filesystem::current_path();
  sink->set_pattern("%D %H:%M:%S.%e | [%L/%l] | %@ | %v")
      .set_format_config(config);

  auto logger = fastlog::create_logger("pattern_console_demo", {sink},
                                       fastlog::log_level::trace);
  logger->info("hello world with custom pattern");
  logger->warn("hello world with custom pattern");
  logger->flush();
}

// 最基础的文件落盘示例。
void demo_basic_file() {
  auto logger =
      fastlog::file::make_logger("simple_file_basic", "logs/simple_basic.log");
  logger.info("file hello world");
  fastlog::file::flush(logger);
}

// 最基础的文件配置示例。
void demo_configure_file() {
  fastlog::FileLoggerOptions options{
      .level = fastlog::log_level::trace,
      .detail_mode = fastlog::detail_mode::compact,
      .source_path = fastlog::source_path_mode::filename,
      .source_root = std::filesystem::current_path(),
      .max_file_size = 1024 * 512,
      .max_files = 3,
      .async_write = true};

  auto logger = fastlog::file::make_logger("simple_file_config",
                                           "logs/simple_config.log", options);
  logger.info("file compact mode is the default");

  logger.set_detail_mode(fastlog::detail_mode::full);
  logger.warn("file full mode prints all context with absolute path");

  logger.set_detail_mode(fastlog::detail_mode::compact)
      .set_level(fastlog::log_level::debug)
      .set_max_file_size(1024 * 1024)
      .set_source_path_mode(fastlog::source_path_mode::filename,
                            std::filesystem::current_path());
  logger.debug("simple file configuration updated");
  fastlog::file::flush(logger);
}

// 独立链式构造接口示例：无宏、多 sink、异步文件、相对源码路径。
void demo_pipeline_builder() {
  auto console_sink = fastlog::make_stdout_sink();
  auto file_sink =
      fastlog::make_rotating_file_sink("logs/simple_pipeline.log",
                                       {.max_file_size = 1024 * 1024,
                                        .max_files = 3});

  auto logger =
      fastlog::pipeline("simple_pipeline")
          .at(fastlog::log_level::trace)
          .format_as("%Y [%^%L%$] [%n] [%@] %v")
          .source(fastlog::source_path_mode::relative,
                  std::filesystem::current_path())
          .write_to(console_sink)
          .write_to_async(file_sink, {.queue_size = 4096,
                                      .policy = fastlog::overflow_policy::block})
          .install();

  logger->info("pipeline logger without macros");
  logger->flush();
}

} // namespace

int main() {
  demo_basic_console();
  demo_configure_console();
  demo_custom_console_pattern();
  demo_basic_file();
  demo_configure_file();
  demo_pipeline_builder();
  return 0;
}
