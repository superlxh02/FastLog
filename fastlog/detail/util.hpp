#pragma once
#include <chrono>
#include <ctime>
#include <optional>
#include <string>
// 平台检测宏
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__) || defined(__linux__)
#include <pthread.h>
#include <unistd.h>
#endif

namespace fastlog::detail::util {
// 非拷贝类，用于防止类被拷贝
class noncopyable {
public:
  noncopyable(const noncopyable &) = delete;
  noncopyable &operator=(const noncopyable &) = delete;

protected:
  noncopyable() = default;
  ~noncopyable() noexcept = default;
};

// 单例类，用于创建全局唯一的实例
template <typename T> class Singleton {
  Singleton() = delete;
  ~Singleton() = delete;

public:
  [[nodiscard]]
  static auto instance() -> T & {
    static T instance;
    return instance;
  }
};
std::optional<std::string> get_current_time_tostring(bool is_repeat = true) {
  static thread_local std::array<char, 64> buf{};
  static thread_local std::chrono::seconds last_second{0};

  // 获取当前时间
  auto now = std::chrono::system_clock::now();
  // 转换为time_t类型
  auto time_t_now = std::chrono::system_clock::to_time_t(now);
  // 转换为秒
  auto current_second = std::chrono::seconds(time_t_now);

  // 检查是否是新的秒
  if ((current_second.count() != last_second.count()) || is_repeat) {
    // 转换为本地时间
    std::tm *local_tm = std::localtime(&time_t_now);

    // 根据平台使用不同的时间格式
#ifdef _WIN32
    // Windows平台：使用连字符替代冒号，避免文件名非法字符
    std::strftime(buf.data(), buf.size(), "%Y-%m-%d-%H-%M-%S", local_tm);
#else
    // Unix/Linux/macOS平台：可以使用冒号
    std::strftime(buf.data(), buf.size(), "%Y-%m-%d-%H:%M:%S", local_tm);
#endif

    last_second = current_second;
    return {buf.data()};
  }
  return std::nullopt;
}

// 获取当前pid
inline auto get_current_pid() -> uint32_t {
#ifdef _WIN32
  return static_cast<uint32_t>(GetCurrentProcessId());
#else
  return static_cast<uint32_t>(getpid());
#endif
}

} // namespace fastlog::detail::util
