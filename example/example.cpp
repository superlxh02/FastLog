#include "fastlog/fastlog.hpp"
#include <chrono>
#include <vector>

// 注册文件日志器
//Unix/linux/macOS
auto &test_logger = fastlog::file::make_logger("test_log", "log", "../logs");

//windows
// auto &test_logger = fastlog::file::make_logger("test_log", "log","..\\..\\logs");

std::atomic<bool> keep_running{true};
std::vector<int> vec = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

void file_log_test() {
  auto start = std::chrono::steady_clock::now();
  long long count = 0;
  while (true) {
    fastlog::file::get_logger("test_log")
        ->info("hello world ,count : {}, vec :{}", count++, vec);
    auto now = std::chrono::steady_clock::now();
    auto elapsed =
        std::chrono::duration_cast<std::chrono::seconds>(now - start);
    if (elapsed.count() >= 30) {
      break;
    }
  }
}

void console_log_test() {
  for (int i = 0; i < 10; i++) {
    fastlog::console.info("hello world {}", 1);
    fastlog::console.warn("hello world");
    fastlog::console.error("hello world");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
}

int main() {
  console_log_test();
   file_log_test();

  return 0;
}