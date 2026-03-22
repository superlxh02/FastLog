#pragma once
#include "fastlog/detail/logbuffer.hpp"
#include "fastlog/detail/loglevel.hpp"
#include "fastlog/detail/util.hpp"
#include "logfstream.hpp"
#include <cpptrace/cpptrace.hpp>
#include <cpptrace/from_current.hpp>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <format>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <print>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

namespace fastlog::detail {

// 日志细节模式：
// Compact 适合终端阅读，Full 适合文件落盘与问题定位
enum class LogDetailMode { Compact, Full };

// 文件 logger 配置项：
// 默认开启全量输出，并允许单独设置级别与单文件大小
struct FileLoggerOptions {
  LogLevel level{LogLevel::Debug};
  LogDetailMode detail_mode{LogDetailMode::Full};
  std::size_t max_file_size{1024 * 1024 * 100};
};

// 日志记录结构体
struct logrecord_t {
  const char *datetime;  // 日志记录时间
  uint64_t thread_id;    // 线程ID
  const char *file_name; // 文件名
  size_t line;           // 行号
  bool force_full_detail; // 即使处于简洁模式，也保留源码位置等完整信息
  std::string log;       // 日志内容
};

// 日志格式化参数类，封装日志格式化参数
template <typename... Args> struct basic_format_string_wrapper {
  template <typename T>
    requires std::convertible_to<T, std::string_view>
  consteval basic_format_string_wrapper(
      const T &s, std::source_location loc = std::source_location::current())
      : fmt(s), loc(loc) {}
  std::format_string<Args...> fmt;
  std::source_location loc;
};

// 重命名格式化字符串包装器，使用std::type_identity_t避免自动类型推导
template <typename... Args>
using format_string_wrapper =
    basic_format_string_wrapper<std::type_identity_t<Args>...>;

// 去掉末尾多余换行，避免多行异常块出现空行尾巴
[[nodiscard]]
inline auto trim_trailing_newlines(std::string text) -> std::string {
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
    text.pop_back();
  }
  return text;
}

// 为多行文本增加统一缩进，主要用于异常块中的 message/body 排版
[[nodiscard]]
inline auto indent_multiline(std::string_view text,
                             std::string_view prefix = "  ") -> std::string {
  std::string result;
  result.reserve(text.size() + 32);
  std::size_t start = 0;
  while (start <= text.size()) {
    const auto end = text.find('\n', start);
    const auto count =
        end == std::string_view::npos ? text.size() - start : end - start;
    result += prefix;
    result.append(text.substr(start, count));
    if (end == std::string_view::npos) {
      break;
    }
    result.push_back('\n');
    start = end + 1;
  }
  return result;
}

// 统一格式化异常输出块：
// 第一段是异常摘要，第二段是调用栈标题和栈帧内容
[[nodiscard]]
inline auto format_exception_block(std::string_view summary,
                                   std::string stacktrace) -> std::string {
  stacktrace = trim_trailing_newlines(std::move(stacktrace));
  if (stacktrace.empty()) {
    return std::format("Exception:\n{}", indent_multiline(summary));
  }

  auto title_pos = stacktrace.find('\n');
  std::string_view title = stacktrace;
  std::string_view body{};
  if (title_pos != std::string::npos) {
    title = std::string_view(stacktrace).substr(0, title_pos);
    body = std::string_view(stacktrace).substr(title_pos + 1);
  }

  if (body.empty()) {
    return std::format("Exception:\n{}\n{}\n{}",
                       indent_multiline(summary), title, "  <empty>");
  }

  return std::format("Exception:\n{}\n{}\n{}", indent_multiline(summary), title,
                     indent_multiline(body));
}

// 多行日志对齐工具：
// 第一行沿用完整日志前缀，续行统一使用 "  | "，便于观察堆栈或多行正文
[[nodiscard]]
inline auto align_multiline_message(std::string_view prefix,
                                    std::string_view,
                                    std::string_view message) -> std::string {
  const auto newline_pos = message.find('\n');
  if (newline_pos == std::string_view::npos) {
    return std::format("{}{}", prefix, message);
  }

  std::string result;
  constexpr std::string_view continuation = "  | ";
  result.reserve(prefix.size() + message.size() + continuation.size() * 4);
  result.append(prefix);
  result.append(message.substr(0, newline_pos));

  std::size_t start = newline_pos + 1;
  while (start <= message.size()) {
    result.push_back('\n');
    result.append(continuation);
    const auto end = message.find('\n', start);
    const auto count =
        end == std::string_view::npos ? message.size() - start : end - start;
    result.append(message.substr(start, count));
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  return result;
}

// 将一条日志记录渲染为最终输出字符串
// 控制台和文件 logger 共享这套格式化逻辑
template <LogLevel level>
[[nodiscard]]
inline auto format_log_message(const logrecord_t &record,
                               LogDetailMode detail_mode,
                               bool enable_color = false) -> std::string {
  LogLevelWrapper level_wrapper(level);
  const auto effective_mode =
      record.force_full_detail ? LogDetailMode::Full : detail_mode;
  if (effective_mode == LogDetailMode::Full) {
    const auto plain_prefix =
        std::format("{} [{}] [tid:{}] [{}:{}] ", record.datetime,
                    level_wrapper.to_string(), record.thread_id, record.file_name,
                    record.line);
    if (enable_color) {
      const auto color_prefix = std::format(
          "{} [{}{}{}] [tid:{}] [{}:{}] {}", record.datetime,
          level_wrapper.to_color(), level_wrapper.to_string(), reset_format(),
          record.thread_id, record.file_name, record.line, "");
      return align_multiline_message(color_prefix, plain_prefix, record.log);
    }
    return align_multiline_message(plain_prefix, plain_prefix, record.log);
  }

  const auto plain_prefix =
      std::format("{} [{}] [tid:{}] ", record.datetime, level_wrapper.to_string(),
                  record.thread_id);
  if (enable_color) {
    const auto color_prefix =
        std::format("{} [{}{}{}] [tid:{}] {}", record.datetime,
                    level_wrapper.to_color(), level_wrapper.to_string(),
                    reset_format(), record.thread_id, "");
    return align_multiline_message(color_prefix, plain_prefix, record.log);
  }
  return align_multiline_message(plain_prefix, plain_prefix, record.log);
}

// 日志器基类，基于 CRTP 实现日志公共能力
// 负责：
// 1. 级别过滤
// 2. source_location 捕获
// 3. 异常与堆栈格式化
template <typename DerviceLogger> class BaseLogger : util::noncopyable {
public:
  void set_level(LogLevel level) { __level = level; }
  [[nodiscard]]
  LogLevel level() const {
    return __level;
  }
  void set_detail_mode(LogDetailMode mode) { __detail_mode = mode; }
  [[nodiscard]]
  LogDetailMode detail_mode() const {
    return __detail_mode;
  }

  template <typename... Args>
  void trace(format_string_wrapper<Args...> fmt, Args &&...args) {
    format<LogLevel::Trace>(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void debug(format_string_wrapper<Args...> fmt, Args &&...args) {
    format<LogLevel::Debug, Args...>(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void info(format_string_wrapper<Args...> fmt, Args &&...args) {
    format<LogLevel::Info>(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void warn(format_string_wrapper<Args...> fmt, Args &&...args) {
    format<LogLevel::Warn, Args...>(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void error(format_string_wrapper<Args...> fmt, Args &&...args) {
    format<LogLevel::Error, Args...>(fmt, std::forward<Args>(args)...);
  }
  template <typename... Args>
  void fatal(format_string_wrapper<Args...> fmt, Args &&...args) {
    format<LogLevel::Fatal>(fmt, std::forward<Args>(args)...);
  }

  // 输出已捕获的异常，并尽量保留 traced exception 的栈信息
  void exception(
      const std::exception &ex,
      std::source_location loc = std::source_location::current()) {
    if (const auto *traceable = dynamic_cast<const cpptrace::exception *>(&ex);
        traceable != nullptr) {
      log_preformatted<LogLevel::Error>(
          format_exception_block(traceable->message(),
                                 traceable->trace().to_string(false)),
          loc);
      return;
    }
    log_preformatted<LogLevel::Error>(
        format_exception_block(ex.what(), get_current_exception_trace()),
        loc);
  }

  // 在 catch(...) 中输出当前异常
  void current_exception(
      std::string_view prefix = "Unhandled exception",
      std::source_location loc = std::source_location::current()) {
    try {
      log_preformatted<LogLevel::Error>(
          format_exception_block(prefix, get_current_exception_trace()), loc);
    } catch (const std::exception &ex) {
      exception(ex, loc);
    } catch (...) {
      log_preformatted<LogLevel::Error>(
          std::format("Exception:\n  {}\nStacktrace\n  <non-std exception>",
                      prefix),
          loc);
    }
  }

private:
  // 常规日志格式化路径：
  // 先做级别过滤，再收集时间、线程、文件、行号，最后交给派生类输出
  template <LogLevel LEVEL, typename... Args>
  void format(format_string_wrapper<Args...> fmt_w, Args &&...args) {
    if (LEVEL < __level) {
      return;
    }
    std::string time_str;
    auto res = util::get_current_time_tostring();
    if (res.has_value()) {
      time_str = res.value();
    }
    // 调用派生类的log方法记录日志
    static_cast<DerviceLogger *>(this)->template log<LEVEL>(logrecord_t{
        .datetime = time_str.c_str(),
        .thread_id = util::get_current_thread_id(),
        .file_name = fmt_w.loc.file_name(),
        .line = fmt_w.loc.line(),
        .force_full_detail = false,
        .log = std::format(fmt_w.fmt, std::forward<Args>(args)...)});
  }

  // 预格式化日志路径：
  // 主要给异常块、多行堆栈这类已经拼装好的文本使用
  template <LogLevel LEVEL>
  void log_preformatted(std::string message, std::source_location loc) {
    if (LEVEL < __level) {
      return;
    }
    std::string time_str;
    auto res = util::get_current_time_tostring();
    if (res.has_value()) {
      time_str = res.value();
    }
    static_cast<DerviceLogger *>(this)->template log<LEVEL>(logrecord_t{
        .datetime = time_str.c_str(),
        .thread_id = util::get_current_thread_id(),
        .file_name = loc.file_name(),
        .line = loc.line(),
        .force_full_detail = true,
        .log = std::move(message)});
  }

  // 获取当前异常栈，如果当前异常没有 trace，则退化为现场栈快照
  [[nodiscard]]
  static auto get_current_exception_trace() -> std::string {
    try {
      const auto &trace = cpptrace::from_current_exception();
      if (!trace.empty()) {
        return trace.to_string(false);
      }
    } catch (...) {
    }
    return cpptrace::generate_trace(2).to_string(false);
  }

private:
  LogLevel __level{LogLevel::Debug};
  LogDetailMode __detail_mode{LogDetailMode::Compact};
};

// 控制台日志器：默认精简输出，并带 ANSI 颜色
class ConsoleLogger : public BaseLogger<ConsoleLogger> {
public:
  template <LogLevel level> void log(const logrecord_t &record) {
    std::print("{}\n", format_log_message<level>(record, detail_mode(), true));
  }
};

// 文件日志器：基于三缓冲和后台线程实现异步写入
class FileLogger : public BaseLogger<FileLogger> {
public:
  explicit FileLogger(std::filesystem::path filepath,
                      const FileLoggerOptions &options = {})
      : __logfs(filepath), __current_buffer(std::make_unique<FileLogBuf>()),
        __work_thread{&FileLogger::work, this} {
    apply_options(options);
    for (int i = 0; i < 2; ++i) {
      __empty_buffers.push_back(std::make_unique<FileLogBuf>());
    }
  }

  ~FileLogger() {
    // 运行标志位置为false
    __running = false;
    // 通知工作线程
    __cv.notify_one();
    // 等待工作线程完成工作，回收工作线程
    if (__work_thread.joinable())
      __work_thread.join();
  }

  // 应用文件 logger 配置项
  void apply_options(const FileLoggerOptions &options) {
    set_level(options.level);
    set_detail_mode(options.detail_mode);
    set_max_file_size(options.max_file_size);
  }

  // 设置单文件最大大小，超过后由 logfstream 负责切分新文件
  void set_max_file_size(std::size_t max_file_size) {
    __logfs.set_maxsize(max_file_size);
  }

  [[nodiscard]]
  auto max_file_size() const noexcept -> std::size_t {
    return __logfs.maxsize();
  }

  // 主动 flush：
  // 将当前缓冲区切换到满缓冲区列表，通知后台线程尽快落盘
  void flush() {
    std::lock_guard lock{__mtx};
    if (!__current_buffer->empty()) {
      __full_buffers.push_back(std::move(__current_buffer));
      if (!__empty_buffers.empty()) {
        __current_buffer = std::move(__empty_buffers.front());
        __empty_buffers.pop_front();
      } else {
        __current_buffer = std::make_unique<FileLogBuf>();
      }
    }
    __cv.notify_one();
  }

  /*
    日志记录方法，基于crtp
    基于缓冲区机制，三缓冲：
  1. 当前缓冲区：当前正在写入日志的缓冲区
  2. 满缓冲区列表：满了的缓冲区，通知等待被消费的线程
  3. 空缓冲区列表：空的缓冲区，等待被填充
  */

  /*
    总体思路：1.写当前缓冲区，直到当前缓冲区被写满
            2.将当前缓冲区移动到满缓冲区列表
            3.创建新的当前缓冲区
            4.通知工作线程消费

  */

  /*
    当前缓冲区选择：
    1. 如果当前缓冲区能够容纳msg，就写入当前缓冲区
    2. 如果当前缓冲区不能容纳msg，就将当前缓冲区移动到满缓冲区列表
    3.如果空缓冲区列表不为空，就从空缓冲区列表中取出一个缓冲区，赋值给当前缓冲区
    4. 如果空缓冲区列表为空，就创建一个新的缓冲区，赋值给当前缓冲区
  */

  template <LogLevel level> void log(const logrecord_t &record) {
    if (!__running) {
      return;
    }
    std::string msg{
        format_log_message<level>(record, detail_mode()) + "\n"};
    std::lock_guard lock{__mtx}; // 加锁
    // 如果当前缓冲区能够容纳msg，就写入当前缓冲区
    if (__current_buffer->writeable() > msg.size()) {
      __current_buffer->write(msg);

    } else {
      // 如果当前缓冲区不能容纳msg，就将当前缓冲区移动到满缓冲区列表
      __full_buffers.push_back(std::move(__current_buffer));
      // 如果空缓冲区列表不为空，就从空缓冲区列表中取出一个缓冲区，赋值给当前缓冲区
      if (!__empty_buffers.empty()) {
        __current_buffer = std::move(__empty_buffers.front());
        __empty_buffers.pop_front();
      } else {
        // 如果空缓冲区列表为空，就创建一个新的缓冲区，赋值给当前缓冲区
        __current_buffer = std::make_unique<FileLogBuf>();
      }
      // 写入当前缓冲区
      __current_buffer->write(msg);
      //  通知工作线程消费新的日志记录
      __cv.notify_one();
    }
  }

private:
  using FileLogBuf = FileLogBuffer<4000 * 1024>;
  using logbuf_ptr = std::unique_ptr<FileLogBuf>;

private:
  // 工作线程，消费满缓冲区列表中的缓冲区
  /*
    工作线程：
    1. 等待满缓冲区列表不为空
    2. 如果满缓冲区过多，只保留两个
    3. 消费满缓冲区列表中的缓冲区，将数据写入文件缓冲区
    4. 如果满缓冲区列表的缓冲区数量超过2个，只保留2个
    5.如果运行标志为false,且当前缓冲区不为空，则处理关闭前剩余的日志记录，将当前缓冲区数据写入文件
    6. 刷新文件缓冲区，写入文件
    7. 将满缓冲区列表中的缓冲区移动到空缓冲区列表
  */
  void work() {
    constexpr std::size_t max_buffer_list_size = 15;

    while (__running || !__current_buffer->empty() || !__full_buffers.empty()) {
      std::unique_lock<std::mutex> lock(__mtx);
      // 等待满缓冲区列表不为空
      __cv.wait_for(lock, std::chrono::milliseconds(3),
                    [this]() -> bool {
                      return !this->__full_buffers.empty() || !this->__running;
                    });

      if (!__running && !__current_buffer->empty()) {
        __full_buffers.push_back(std::move(__current_buffer));
        if (!__empty_buffers.empty()) {
          __current_buffer = std::move(__empty_buffers.front());
          __empty_buffers.pop_front();
        } else {
          __current_buffer = std::make_unique<FileLogBuf>();
        }
      }

      // 如果缓冲区链表的缓冲区数量过多，只剩2个，其余丢弃掉
      if (__full_buffers.size() > max_buffer_list_size) {
        std::cerr << std::format("Dropped log messages {} larger buffers\n",
                                 __full_buffers.size() - 2);
        __full_buffers.resize(2);
      }
      // 消费满缓冲区列表中的缓冲区，将数据写入文件缓冲区
      for (auto &buffer : __full_buffers) {
        __logfs.write(buffer->data(), buffer->size());
        buffer->reset();
      }
      // 如果满缓冲区列表的缓冲区数量超过2个，只保留2个
      if (__full_buffers.size() > 2) {
        __full_buffers.resize(2);
      }

      // 刷新文件缓冲区，写入文件
      __logfs.flush();
      // 将满缓冲区列表中的缓冲区移动到空缓冲区列表
      __empty_buffers.splice(__empty_buffers.end(), __full_buffers);
    }
  }

private:
  logfstream __logfs;                      // 文件流
  logbuf_ptr __current_buffer;             // 当前日志缓冲区
  std::list<logbuf_ptr> __empty_buffers{}; // 空缓冲区列表
  std::list<logbuf_ptr> __full_buffers{};  // 满缓冲区列表
  std::mutex __mtx{};                      // 互斥锁
  std::condition_variable __cv{};          // 条件变量
  std::thread __work_thread{};             // 工作线程
  std::atomic<bool> __running{true};       // 运行标志
};

} // namespace fastlog::detail
