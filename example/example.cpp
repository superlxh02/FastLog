#include "fastlog/fastlog.hpp"
#include <chrono>
#include <vector>

// 注册文件日志器
auto &test_logger1 = fastlog::file::make_logger("test_log1", "../logs/log1/");
auto &test_logger2 = fastlog::file::make_logger("test_log2", "../logs/log2/");

std::vector<int> vec = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

void file_log_func1() {
  auto start = std::chrono::steady_clock::now();
  long long count = 0;
  while (true) {
    fastlog::file::get_logger("test_log1")
        ->info("hello world log1,count : {}, vec :{}", count++, vec);
    auto now = std::chrono::steady_clock::now();
    auto time = std::chrono::duration_cast<std::chrono::seconds>(now - start);
    if (time.count() >= 10) {
      break;
    }
  }
}

void file_log_func2() {
  auto start = std::chrono::steady_clock::now();
  long long count = 0;
  while (true) {
    fastlog::file::get_logger("test_log2")
        ->info("hello world log2,count : {}, vec :{}", count++, vec);
    auto now = std::chrono::steady_clock::now();
    auto elapsed =
        std::chrono::duration_cast<std::chrono::seconds>(now - start);
    if (elapsed.count() >= 10) {
      break;
    }
  }
}

void console_log_test() {
  fastlog::set_log_level(fastlog::LogLevel::Trace);
  fastlog::console.trace("hello world");
  fastlog::console.debug("hello world");
  fastlog::console.info("hello world");
  fastlog::console.warn("hello world");
  fastlog::console.error("hello world");
  fastlog::console.fatal("hello world");
}

void file_log_test() {
  std::thread t1(file_log_func1);
  std::thread t2(file_log_func2);
  t1.join();
  t2.join();
}

int main() {
  console_log_test();
  fastlog::console.info("start file log test .............");
  file_log_test();
  return 0;
}