#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>

namespace fastlog {

// 日志级别枚举，按严重程度从低到高排列。
enum class log_level { trace, debug, info, warn, error, fatal, off };

// 日志细节模式：
// compact 适合终端快速浏览；
// standard 适合在终端快速定位问题；
// full 适合落盘或完整诊断。
enum class detail_mode { compact, standard, full };

// 异步队列满载时的处理策略。
enum class overflow_policy { block, drop_oldest, drop_new };

// 时间戳输出使用本地时间还是 UTC 时间。
enum class time_mode { local, utc };

// 源文件路径输出模式。
// filename：只输出源文件名，不带路径。
// relative：优先按 source_root 输出相对路径。
// absolute：输出编译器记录下来的完整路径。
enum class source_path_mode { filename, relative, absolute };

// sink 的运行时统计信息。
struct sink_stats {
  std::uint64_t enqueued_messages{0}; // 已进入 sink 的消息总数。
  std::uint64_t dropped_messages{0};  // 因队列溢出等原因被丢弃的消息数。
  std::uint64_t flushed_messages{0};  // 主动 flush 的累计次数。
  std::uint64_t current_queue_depth{0}; // 当前队列深度；同步 sink 恒为 0。
  std::uint64_t peak_queue_depth{0};    // 历史峰值队列深度；同步 sink 恒为 0。
};

// sink 声明自己实际需要 logger 前端采样哪些元数据。
struct record_metadata {
  bool message{true};
  bool timestamp{true};
  bool thread_id{true};
  bool process_id{true};
  bool source_location{true};
};

// formatter 和 sink 共享的格式化配置。
struct format_config {
  detail_mode detail{detail_mode::compact};         // 输出是简洁模式还是完整模式。
  bool show_timestamp{true};                        // 是否输出时间戳。
  bool timestamp_with_microseconds{false};          // 时间戳是否输出微秒；简洁模式默认关闭。
  bool show_level{true};                            // 是否输出日志级别。
  bool show_logger_name{false};                     // 是否输出 logger 名称。
  bool show_thread_id{false};                       // 是否输出线程 ID。
  bool show_process_id{false};                      // 是否输出进程 ID。
  bool show_source_location{false};                 // 是否输出源文件和行号。
  bool colorize{false};                             // 是否启用 ANSI 颜色。
  time_mode clock_mode{time_mode::local};           // 时间戳采用本地时间还是 UTC。
  source_path_mode source_path{source_path_mode::filename}; // 源文件路径采用文件名还是绝对路径输出。
  std::filesystem::path source_root{};              // 保留字段；当前源码位置输出模式不再依赖路径裁剪。
};

// 异步 sink 的后台队列配置。
struct async_options {
  std::size_t queue_size{8192};                          // 队列最大容量。
  overflow_policy policy{overflow_policy::block};        // 满载时的处理策略。
  std::chrono::milliseconds flush_interval{250};         // 后台线程的周期性刷盘间隔。
};

// 基础文件 sink 的打开策略。
struct file_sink_options {
  bool truncate{false};            // 是否在打开时清空旧文件。
  bool create_directories{true};   // 是否自动创建父目录。
};

// 大小轮转文件 sink 的配置项。
struct rotating_file_sink_options : file_sink_options {
  std::size_t max_file_size{1024 * 1024 * 100}; // 单个文件允许的最大字节数。
  std::size_t max_files{5};                     // 保留的轮转文件数量。
};

// 按天切分文件 sink 的配置项。
struct daily_file_sink_options : file_sink_options {
  int rotation_hour{0};    // 每天发生切分的小时。
  int rotation_minute{0};  // 每天发生切分的分钟。
};

// logger 前端构造出的统一日志记录对象。
struct log_record {
  std::string logger_name; // 产生日志的 logger 名称。
  std::string_view logger_name_ref{}; // logger 热路径使用的稳定名称引用。
  log_level level{log_level::info}; // 当前日志级别。
  std::chrono::system_clock::time_point timestamp{}; // 采样时刻。
  std::uint64_t thread_id{0};            // 采样线程的 ID。
  std::uint32_t process_id{0};           // 采样进程的 ID。
  std::source_location location{};       // 调用点源码位置。
  bool force_source_location{false};     // 是否强制以 full 模式输出源码位置。
  std::string message;                   // 已格式化好的正文内容。
};

// 前向声明，避免 detail 头之间相互 include 过重。
class formatter;
class sink;
class logger;
class registry;

// 公开层常用的共享指针别名。
using sink_ptr = std::shared_ptr<sink>;
using logger_ptr = std::shared_ptr<logger>;

} // namespace fastlog
