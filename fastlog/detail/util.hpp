#pragma once
#include <ctime>
#include <optional>
#include <print>
#include <pthread.h>
#include <string>
#include <string_view>
#include <sys/time.h>
#include <thread>
#include <unistd.h>
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
// 获取当前时间字符串,同一秒不重复返回
std::optional<std::string> get_current_time_tostring() {
  static thread_local std::array<char, 64> buf{};
  static thread_local time_t last_second{0};
  struct timeval tv;
  struct tm tm;
  gettimeofday(&tv, NULL);
  // 获取当前时间
  auto cur_second = tv.tv_sec;
  // 检查是否是新的秒
  if (cur_second != last_second) {
    // 转换为本地时间
    localtime_r(&tv.tv_sec, &tm);
    // 格式化为字符串
    strftime(buf.data(), buf.size(), "%Y-%m-%d-%H:%M:%S", &tm);
    last_second = cur_second;
    return {buf.data()};
  }
  return std::nullopt;
}

// 获取当前pid
pid_t get_current_pid() { return ::getpid(); }
static thread_local std::string
    t_name; // 设置静态全局线程局部存储，每个线程都有一个独立的t_name变量

// 设置当前线程名称
static inline auto set_current_thread_name(std::string_view name) {
  pthread_setname_np(name.data());
  t_name = name;
}

// 获取当前线程名称
static inline auto get_current_thread_name() -> std::string_view {
  return t_name;
}
} // namespace fastlog::detail::util
