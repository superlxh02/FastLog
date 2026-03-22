#include "fastlog/fastlog.hpp"
#include <stdexcept>
#include <thread>
#include <vector>

// 示例数据：用于验证 format 能正确格式化容器内容
std::vector<int> vec = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

// 示例循环次数：数量足够覆盖异步文件写入，但不会让示例运行过久
inline constexpr long long kLoopCount = 2000;

// 抛出带 cpptrace 调用栈的异常，用于演示异常日志输出
void nested_throw() { throw fastlog::runtime_error("boom from cpptrace"); }

// 子线程 1：写入 file_log1，验证文件日志默认全量输出
void file_log_func1() {
  long long count = 0;
  while (count < kLoopCount) {
    fastlog::file::get_logger("file_log1")
        ->info("hello world log1,count : {}, vec :{}", count++, vec);
  }
}

// 子线程 2：写入 file_log2，验证文件日志最小级别过滤
void file_log_func2() {
  long long count = 0;
  while (count < kLoopCount) {
    fastlog::file::get_logger("file_log2")
        ->warn("hello world log2,count : {}, vec :{}", count++, vec);
  }
}

// 控制台日志测试：终端默认使用精简格式输出
void console_log_test() {
  fastlog::set_consolelog_level(fastlog::LogLevel::Trace);
  fastlog::console.trace("hello world");
  fastlog::console.debug("hello world");
  fastlog::console.info("hello world");
  fastlog::console.warn("hello world");
  fastlog::console.error("hello world");
  fastlog::console.fatal("hello world");
}

// 文件日志测试：
// 1. file_log1 使用默认全量模式
// 2. file_log2 设置最小级别为 Warn，用于验证级别过滤
void file_log_test() {
  // 注册文件日志器
  auto &file_logger1 = fastlog::file::make_logger("file_log1", FILE_LOG1_PATH);
  auto &file_logger2 = fastlog::file::make_logger("file_log2", FILE_LOG2_PATH);

  // 文件 logger 支持单独配置最小日志级别和单文件最大大小
  fastlog::file::set_level(file_logger1, fastlog::LogLevel::Debug);
  fastlog::file::set_max_file_size(file_logger1, 1024 * 1024);
  fastlog::file::set_level(file_logger2, fastlog::LogLevel::Warn);
  fastlog::file::set_max_file_size(file_logger2, 1024 * 1024);

  // 启动两个子线程并发写入文件日志
  std::thread t1(file_log_func1);
  std::thread t2(file_log_func2);
  t1.join();
  t2.join();

  // 补几条收尾日志，便于直接观察默认格式和级别过滤是否生效
  file_logger1.warn("file logger 1 keeps full detail by default");
  file_logger2.info("this line should be filtered by file logger 2 level");
  file_logger2.warn("file logger 2 min level is warn");

  // 主动 flush，确保示例退出前日志已经完全落盘
  fastlog::file::flush(file_logger1);
  fastlog::file::flush(file_logger2);
}

// 基础日志能力验证入口
void test() {
  console_log_test();
  fastlog::console.info("file log write start .............");
  file_log_test();
  fastlog::console.info("file log write finish .............");
}

// 仅输出到控制台的异常日志示例
void console_exception_log_test() {
  fastlog::console.error("stacktrace snapshot:\n{}",
                         fastlog::current_stacktrace());

  try {
    nested_throw();
  } catch (const std::exception &ex) {
    fastlog::console.exception(ex);
  }
}

// 仅输出到文件的异常日志示例
void file_exception_log_test() {
  if (auto *file_logger = fastlog::file::get_logger("file_log1");
      file_logger != nullptr) {
    file_logger->error("stacktrace snapshot:\n{}", fastlog::current_stacktrace());

    try {
      nested_throw();
    } catch (const std::exception &ex) {
      file_logger->exception(ex);
      fastlog::file::flush(*file_logger);
    }
  }
}

// 额外能力：同时输出到控制台和全部文件 logger
void broadcast_exception_log_test() {
  try {
    nested_throw();
  } catch (const std::exception &ex) {
    fastlog::log_exception_to_all(ex);
  }
}

int main() {
  // 先验证普通日志，再切换终端为全量模式验证异常定位信息
  test();
  fastlog::set_consolelog_detail_mode(fastlog::LogDetailMode::Full);
  console_exception_log_test();
  file_exception_log_test();
  broadcast_exception_log_test();

  return 0;
}
