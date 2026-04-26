# API Reference

本文档说明 FastLog 当前对外暴露的公开接口。

## 目录

- [1. 公开入口](#1-公开入口)
- [2. 基础类型](#2-基础类型)
- [3. 公开结构体](#3-公开结构体)
- [4. 基础控制台接口](#4-基础控制台接口)
- [5. 基础文件接口](#5-基础文件接口)
- [6. `fastlog::logger`](#6-fastloglogger)
- [7. `fastlog::sink`](#7-fastlogsink)
- [8. sink 工厂函数](#8-sink-工厂函数)
- [9. registry 与全局函数](#9-registry-与全局函数)
- [10. 栈追踪与异常接口](#10-栈追踪与异常接口)
- [11. 综合示例](#11-综合示例)

## 使用建议

- 如果你只想快速开始，优先看“基础控制台接口”和“基础文件接口”。
- 如果你要做多 sink、异步输出或自定义格式，重点看 `logger`、`sink` 和 sink 工厂函数。
- 如果你要把文档当作配置手册使用，优先看“基础类型”和“公开结构体”。

## 1. 公开入口

```cpp
#include "fastlog/fastlog.hpp"
```

## 2. 基础类型

### `fastlog::log_level`

功能说明：表示日志严重级别。

定义：

```cpp
enum class log_level { trace, debug, info, warn, error, fatal, off };
```

枚举项说明：

- `trace`
  最详细的调试信息。
- `debug`
  调试信息。
- `info`
  常规运行信息。
- `warn`
  告警信息。
- `error`
  错误信息。
- `fatal`
  严重错误。
- `off`
  关闭输出。

### `fastlog::detail_mode`

功能说明：控制默认输出模式。

定义：

```cpp
enum class detail_mode { compact, standard, full };
```

枚举项说明：

- `compact`
  默认模式。输出时间到秒、级别和正文。
- `standard`
  输出时间到秒、级别、进程 ID、源码位置和正文。
- `full`
  输出完整上下文，默认包含线程、进程、logger 名称和绝对路径源码位置。

### `fastlog::overflow_policy`

功能说明：控制异步队列满载时的处理策略。

定义：

```cpp
enum class overflow_policy { block, drop_oldest, drop_new };
```

枚举项说明：

- `block`
  队列满时阻塞调用线程，直到队列有空位。
- `drop_oldest`
  丢弃最旧消息，保留新消息。
- `drop_new`
  丢弃当前新消息。

### `fastlog::time_mode`

功能说明：控制时间戳使用本地时间还是 UTC。

定义：

```cpp
enum class time_mode { local, utc };
```

### `fastlog::source_path_mode`

功能说明：控制源码位置输出形式。

定义：

```cpp
enum class source_path_mode { filename, relative, absolute };
```

枚举项说明：

- `filename`
  仅输出文件名和行号。
- `relative`
  优先按 `format_config::source_root` 输出相对路径。
- `absolute`
  输出绝对路径和行号。

## 3. 公开结构体

### `fastlog::FileLoggerOptions`

功能说明：基础文件接口使用的配置对象，用于快速创建文件 logger。

定义：

```cpp
struct FileLoggerOptions {
  log_level level{log_level::debug};
  detail_mode detail_mode{detail_mode::compact};
  source_path_mode source_path{source_path_mode::filename};
  std::filesystem::path source_root{};
  std::size_t max_file_size{1024 * 1024 * 100};
  std::size_t max_files{5};
  bool async_write{true};
  overflow_policy overflow{overflow_policy::block};
  std::size_t queue_size{8192};
  std::chrono::milliseconds flush_interval{250};
  bool show_thread_id{true};
  bool show_process_id{false};
};
```

字段说明：

- `level`
  文件 logger 的最小日志级别。
- `detail_mode`
  默认输出模式，常用值为 `compact`、`standard`、`full`。
- `source_path`
  源码位置显示模式。
- `source_root`
  保留字段。当前公开路径模式不依赖它。
- `max_file_size`
  单个日志文件最大大小，单位字节。
- `max_files`
  轮转文件保留数量。
- `async_write`
  是否默认使用异步写入。
- `overflow`
  异步队列满载策略。
- `queue_size`
  异步队列容量。
- `flush_interval`
  后台线程周期 flush 间隔。
- `show_thread_id`
  在简洁模式配置下是否显示线程 ID。
- `show_process_id`
  在简洁模式配置下是否显示进程 ID。

示例：

```cpp
fastlog::FileLoggerOptions options{
    .level = fastlog::log_level::debug,
    .detail_mode = fastlog::detail_mode::standard,
    .source_path = fastlog::source_path_mode::filename,
    .async_write = true
};
```

### `fastlog::sink_stats`

功能说明：描述一个 sink 的运行时统计信息。

定义：

```cpp
struct sink_stats {
  std::uint64_t enqueued_messages{0};
  std::uint64_t dropped_messages{0};
  std::uint64_t flushed_messages{0};
  std::uint64_t current_queue_depth{0};
  std::uint64_t peak_queue_depth{0};
};
```

字段说明：

- `enqueued_messages`
  已进入 sink 的消息总数。
- `dropped_messages`
  被丢弃的消息总数。
- `flushed_messages`
  flush 调用累计次数。
- `current_queue_depth`
  当前队列深度。同步 sink 一般为 0。
- `peak_queue_depth`
  历史峰值队列深度。

### `fastlog::format_config`

功能说明：高级 sink / formatter 侧的格式配置对象。

定义：

```cpp
struct format_config {
  detail_mode detail{detail_mode::compact};
  bool show_timestamp{true};
  bool timestamp_with_microseconds{false};
  bool show_level{true};
  bool show_logger_name{false};
  bool show_thread_id{false};
  bool show_process_id{false};
  bool show_source_location{false};
  bool colorize{false};
  time_mode clock_mode{time_mode::local};
  source_path_mode source_path{source_path_mode::filename};
  std::filesystem::path source_root{};
};
```

字段说明：

- `detail`
  当前 detail 模式。
- `show_timestamp`
  是否显示时间戳。
- `timestamp_with_microseconds`
  是否输出微秒。
- `show_level`
  是否显示日志级别。
- `show_logger_name`
  是否显示 logger 名称。
- `show_thread_id`
  是否显示线程 ID。
- `show_process_id`
  是否显示进程 ID。
- `show_source_location`
  是否显示源码位置。
- `colorize`
  是否启用 ANSI 颜色。
- `clock_mode`
  时间戳使用本地时间还是 UTC。
- `source_path`
  源码路径显示模式。
- `source_root`
  保留字段。

### `fastlog::async_options`

功能说明：异步 sink 的配置对象。

定义：

```cpp
struct async_options {
  std::size_t queue_size{8192};
  overflow_policy policy{overflow_policy::block};
  std::chrono::milliseconds flush_interval{250};
};
```

字段说明：

- `queue_size`
  异步队列最大容量。
- `policy`
  队列满载策略。
- `flush_interval`
  后台线程周期 flush 间隔。

### `fastlog::file_sink_options`

功能说明：基础文件 sink 的文件打开配置。

定义：

```cpp
struct file_sink_options {
  bool truncate{false};
  bool create_directories{true};
};
```

字段说明：

- `truncate`
  是否打开时清空旧文件。
- `create_directories`
  是否自动创建父目录。

### `fastlog::rotating_file_sink_options`

功能说明：轮转文件 sink 的配置对象。

定义：

```cpp
struct rotating_file_sink_options : file_sink_options {
  std::size_t max_file_size{1024 * 1024 * 100};
  std::size_t max_files{5};
};
```

字段说明：

- `max_file_size`
  单个文件允许的最大大小，单位字节。
- `max_files`
  保留的轮转文件数量。

### `fastlog::daily_file_sink_options`

功能说明：按天切分文件 sink 的配置对象。

定义：

```cpp
struct daily_file_sink_options : file_sink_options {
  int rotation_hour{0};
  int rotation_minute{0};
};
```

字段说明：

- `rotation_hour`
  每天执行切分的小时。
- `rotation_minute`
  每天执行切分的分钟。

## 4. 基础控制台接口

> 推荐起点：适合只想先把日志打印出来的使用场景。

### `fastlog::console`

功能说明：全局控制台门面对象，用于最简单的终端日志输出。

定义：

```cpp
inline const console_facade console{};
```

支持的方法：

#### `console.trace(fmt, args...)`

#### `console.debug(fmt, args...)`

#### `console.info(fmt, args...)`

#### `console.warn(fmt, args...)`

#### `console.error(fmt, args...)`

#### `console.fatal(fmt, args...)`

功能说明：按对应级别输出一条控制台日志。

参数说明：

- `fmt`
  格式串。
- `args...`
  参与格式化的参数。

返回值：

- 返回当前 `sink`，可继续链式配置。

示例：

```cpp
fastlog::console.info("service started on {}", 8080);
```

#### `console.exception(const std::exception& exception, std::source_location location = std::source_location::current())`

功能说明：把一个异常对象按错误日志输出到控制台。

参数说明：

- `exception`
  要输出的异常对象。
- `location`
  调用点源码位置，默认自动捕获。

返回值：

- 无返回值。

#### `console.current_exception(std::string_view prefix = "Unhandled exception", std::source_location location = std::source_location::current())`

功能说明：输出当前异常上下文，适合在 `catch(...)` 中使用。

参数说明：

- `prefix`
  自定义前缀。
- `location`
  调用点源码位置。

返回值：

- 无返回值。

#### `console.flush()`

功能说明：主动刷新控制台 sink。

参数说明：

- 无。

返回值：

- 无返回值。

### `fastlog::set_console_level(log_level level)`

功能说明：设置控制台 logger 的最小日志级别。

参数说明：

- `level`
  新的最小日志级别。

返回值：

- 无返回值。

示例：

```cpp
fastlog::set_console_level(fastlog::log_level::debug);
```

### `fastlog::set_console_detail_mode(detail_mode mode)`

功能说明：设置控制台 logger 的输出模式。

参数说明：

- `mode`
  输出模式，通常为 `compact`、`standard` 或 `full`。

返回值：

- 无返回值。

示例：

```cpp
fastlog::set_console_detail_mode(fastlog::detail_mode::standard);
```

## 5. 基础文件接口

> 推荐起点：适合只想快速接入文件日志，不想手动拼装 sink 的场景。

基础文件接口位于 `fastlog::file` 命名空间。

### `fastlog::file::FileLogger`

功能说明：基础文件 logger 的稳定句柄类型。

定义：

```cpp
class FileLogger { ... };
```

说明：

- `FileLogger` 采用共享所有权语义。
- logger 被从名称注册表删除后，已经拿到手的 `FileLogger` 句柄仍然有效，直到最后一个句柄释放。
- 这使基础文件接口在删除、重建和并发使用场景下具备稳定生命周期。

### `fastlog::file::make_logger(const std::string& logger_name, std::filesystem::path log_path = {}, const FileLoggerOptions& options = {}) -> FileLogger`

功能说明：创建或复用一个基础文件 logger。

参数说明：

- `logger_name`
  logger 的逻辑名称。
- `log_path`
  日志文件路径。为空时默认使用 `logger_name`。
- `options`
  文件 logger 配置。

返回值：

- 返回一个 `FileLogger` 句柄。

示例：

```cpp
fastlog::FileLoggerOptions options;
options.detail_mode = fastlog::detail_mode::standard;
options.async_write = true;

auto logger = fastlog::file::make_logger("app", "logs/app.log", options);
logger.info("file logger ready");
```

### `fastlog::file::delete_logger(const std::string& logger_name)`

功能说明：删除一个基础文件 logger。

参数说明：

- `logger_name`
  要删除的 logger 名称。

返回值：

- 无返回值。

### `fastlog::file::get_logger(const std::string& logger_name) -> FileLogger`

功能说明：按名称获取基础文件 logger。

参数说明：

- `logger_name`
  logger 名称。

返回值：

- 找到时返回有效句柄。
- 未找到时返回空句柄，可通过 `if (logger)` 判断。

### `fastlog::file::set_level(const FileLogger& logger_ref, log_level level) -> const FileLogger&`

功能说明：设置文件 logger 的最小日志级别。

参数说明：

- `logger_ref`
  目标 logger。
- `level`
  新的日志级别。

返回值：

- 返回 `logger_ref`，可继续链式配置。

### `fastlog::file::set_detail_mode(const FileLogger& logger_ref, detail_mode mode) -> const FileLogger&`

功能说明：设置文件 logger 的输出 detail 模式。

参数说明：

- `logger_ref`
  目标 logger。
- `mode`
  新的输出模式。

返回值：

- 返回 `logger_ref`，可继续链式配置。

### `fastlog::file::set_source_path_mode(const FileLogger& logger_ref, source_path_mode mode, std::filesystem::path source_root = {}) -> const FileLogger&`

功能说明：设置文件 logger 的源码位置输出模式。

参数说明：

- `logger_ref`
  目标 logger。
- `mode`
  源码路径模式。
- `source_root`
  `relative` 模式下用于裁剪源码路径的基准目录。

返回值：

- 返回 `logger_ref`，可继续链式配置。

### `fastlog::file::set_max_file_size(const FileLogger& logger_ref, std::size_t max_file_size) -> const FileLogger&`

功能说明：动态调整轮转文件大小阈值。

参数说明：

- `logger_ref`
  目标 logger。
- `max_file_size`
  新的单文件最大大小，单位字节。

返回值：

- 返回 `logger_ref`，可继续链式配置。

### `fastlog::file::flush(const FileLogger& logger_ref)`

功能说明：主动刷新文件 logger。

参数说明：

- `logger_ref`
  目标 logger。

返回值：

- 无返回值。

## 6. `fastlog::logger`

`logger` 是高级 API 的核心对象。

> 高级接口入口：适合需要多 sink、自定义组合、backtrace 或按对象管理日志器的场景。

### `logger::set_level(log_level level) -> logger&`

功能说明：设置 logger 自身的最小日志级别。

参数说明：

- `level`
  新的最小日志级别。

返回值：

- 返回当前 `logger`，可继续链式配置。

### `logger::level() const -> log_level`

功能说明：获取 logger 当前级别。

参数说明：

- 无。

返回值：

- 返回当前 logger 级别。

### `logger::name() const -> const std::string&`

功能说明：获取 logger 名称。

参数说明：

- 无。

返回值：

- 返回 logger 名称引用。

### `logger::set_sinks(std::vector<sink_ptr> sinks) -> logger&`

功能说明：整体替换当前 logger 绑定的 sink 列表。

参数说明：

- `sinks`
  新的 sink 集合。

返回值：

- 返回当前 `logger`，可继续链式配置。

### `logger::add_sink(sink_ptr sink_ptr_value) -> logger&`

功能说明：追加一个新的 sink。

参数说明：

- `sink_ptr_value`
  要追加的 sink。

返回值：

- 返回当前 `logger`，可继续链式配置。

### `logger::sinks() const -> std::vector<sink_ptr>`

功能说明：获取当前 sink 快照。

参数说明：

- 无。

返回值：

- 返回当前绑定 sink 的快照副本。

### `logger::enable_backtrace(std::size_t capacity) -> logger&`

功能说明：开启 backtrace ring buffer。

参数说明：

- `capacity`
  缓冲最近日志的条数。

返回值：

- 返回当前 `logger`，可继续链式配置。

### `logger::disable_backtrace() -> logger&`

功能说明：关闭 backtrace ring buffer 并清空已缓存内容。

参数说明：

- 无。

返回值：

- 返回当前 `logger`，可继续链式配置。

### `logger::flush_backtrace()`

功能说明：将 ring buffer 中缓存的日志重新补发到 sink。

参数说明：

- 无。

返回值：

- 无返回值。

### `logger::flush()`

功能说明：主动刷新当前 logger 绑定的所有 sink。

参数说明：

- 无。

返回值：

- 无返回值。

### `logger::set_flush_on(log_level level) -> logger&`

功能说明：设置当前所有 sink 的自动 flush 阈值。

参数说明：

- `level`
  flush 阈值。

返回值：

- 返回当前 `logger`，可继续链式配置。

### `logger::log(log_level level_value, fmt, args...)`

功能说明：通用日志入口，级别由调用方显式指定。

参数说明：

- `level_value`
  日志级别。
- `fmt`
  格式串。
- `args...`
  格式化参数。

返回值：

- 无返回值。

### 快捷级别接口

支持以下成员：

```cpp
trace(fmt, args...);
debug(fmt, args...);
info(fmt, args...);
warn(fmt, args...);
error(fmt, args...);
fatal(fmt, args...);
```

功能说明：按对应级别输出日志。

参数说明：

- `fmt`
  格式串。
- `args...`
  格式化参数。

返回值：

- 无返回值。

### `logger::log_message(log_level level_value, std::string message, std::source_location location = std::source_location::current(), bool force_source_location = false)`

功能说明：输出一条已经完成格式化的消息，适合异常输出、backtrace 回放等场景。

参数说明：

- `level_value`
  日志级别。
- `message`
  已格式化好的消息正文。
- `location`
  调用点源码位置。
- `force_source_location`
  是否强制输出源码位置。

返回值：

- 无返回值。

### `logger::exception(const std::exception& exception, std::source_location location = std::source_location::current())`

功能说明：按错误日志输出一个异常对象。

参数说明：

- `exception`
  异常对象。
- `location`
  调用点源码位置。

返回值：

- 无返回值。

### `logger::current_exception(std::string_view prefix = "Unhandled exception", std::source_location location = std::source_location::current())`

功能说明：输出当前异常上下文。

参数说明：

- `prefix`
  输出前缀。
- `location`
  调用点源码位置。

返回值：

- 无返回值。

### 自由函数包装

支持：

```cpp
log_trace(logger_ref, fmt, args...);
log_debug(logger_ref, fmt, args...);
log_info(logger_ref, fmt, args...);
log_warn(logger_ref, fmt, args...);
log_error(logger_ref, fmt, args...);
log_fatal(logger_ref, fmt, args...);
```

功能说明：以 free function 风格调用对应级别日志输出。

参数说明：

- `logger_ref`
  目标 logger。
- `fmt`
  格式串。
- `args...`
  格式化参数。

返回值：

- 无返回值。

## 7. `fastlog::sink`

`sink` 是高级输出通道的抽象基类。

> 输出抽象层：适合需要自定义格式、级别、flush 策略和下游目标的场景。

### `sink::set_level(log_level level) -> sink&`

功能说明：设置 sink 自身的最小接收级别。

参数说明：

- `level`
  最小接收级别。

返回值：

- 返回当前 `sink`，可继续链式配置。

### `sink::level() const -> log_level`

功能说明：读取 sink 当前级别。

参数说明：

- 无。

返回值：

- 当前 sink 级别。

### `sink::set_flush_on(log_level level) -> sink&`

功能说明：设置自动触发 flush 的级别阈值。

参数说明：

- `level`
  flush 阈值。

返回值：

- 返回当前 `sink`，可继续链式配置。

### `sink::flush_on() const -> log_level`

功能说明：读取自动 flush 阈值。

参数说明：

- 无。

返回值：

- 当前 flush 阈值。

### `sink::set_pattern(std::string pattern) -> sink&`

功能说明：使用内置 `pattern_formatter` 设置输出 pattern。

常用占位符：

- `%v`：日志正文。
- `%l` / `%L`：完整级别 / 短级别。
- `%n`：logger 名称。
- `%t` / `%P`：线程 ID / 进程 ID。
- `%s` / `%g` / `%@`：按配置输出源码文件 / 原始源码路径 / 源码位置。
- `%#` / `%!` / `%u`：行号 / 函数名 / 列号。
- `%Y` / `%D` / `%H:%M:%S` / `%e` / `%f` / `%z`：完整时间戳、日期、时分秒、毫秒、微秒、时区偏移。
- `%^` / `%$`：彩色输出起止标记。
- `%%`：字面量 `%`。

参数说明：

- `pattern`
  pattern 字符串。

返回值：

- 返回当前 `sink`，可继续链式配置。

### `sink::set_formatter(std::shared_ptr<formatter> formatter_ptr) -> sink&`

功能说明：注入自定义 formatter。

参数说明：

- `formatter_ptr`
  自定义 formatter 对象。

返回值：

- 返回当前 `sink`，可继续链式配置。

### `sink::set_format_config(format_config config) -> sink&`

功能说明：整体更新格式配置。

参数说明：

- `config`
  新的格式配置。

返回值：

- 返回当前 `sink`，可继续链式配置。

### `sink::format_config_value() const -> format_config`

功能说明：读取当前格式配置快照。

参数说明：

- 无。

返回值：

- 返回当前格式配置副本。

### `sink::stats() const -> sink_stats`

功能说明：获取运行时统计信息。

参数说明：

- 无。

返回值：

- 返回一个 `sink_stats` 快照。

### `sink::log(const log_record& record)`

功能说明：向 sink 提交一条结构化日志记录。

参数说明：

- `record`
  统一日志记录对象。

返回值：

- 无返回值。

### `sink::flush()`

功能说明：主动刷新当前 sink。

参数说明：

- 无。

返回值：

- 无返回值。

## 8. sink 工厂函数

> 常用装配入口：用这些工厂函数创建控制台、文件、回调和异步 sink。

### `make_stdout_sink() -> sink_ptr`

功能说明：创建一个默认输出到 `stdout` 的 sink。

参数说明：

- 无。

返回值：

- 返回 `sink_ptr`。

### `make_stderr_sink() -> sink_ptr`

功能说明：创建一个默认输出到 `stderr` 的 sink。

参数说明：

- 无。

返回值：

- 返回 `sink_ptr`。

### `make_null_sink() -> sink_ptr`

功能说明：创建一个丢弃所有输出的 sink，常用于基准测试或临时禁用输出。

参数说明：

- 无。

返回值：

- 返回 `sink_ptr`。

### `make_fanout_sink(std::vector<sink_ptr> sinks) -> sink_ptr`

功能说明：创建一个分发 sink，把一条日志转发给多个下游 sink。

参数说明：

- `sinks`
  下游 sink 集合。

返回值：

- 返回 `sink_ptr`。

### `make_basic_file_sink(const std::filesystem::path& path, file_sink_options options = {}) -> sink_ptr`

功能说明：创建一个基础文件 sink。

参数说明：

- `path`
  目标文件路径。
- `options`
  文件打开选项。

返回值：

- 返回 `sink_ptr`。

### `make_rotating_file_sink(const std::filesystem::path& path, rotating_file_sink_options options = {}) -> sink_ptr`

功能说明：创建一个按文件大小轮转的 sink。

参数说明：

- `path`
  主日志文件路径。
- `options`
  轮转配置。

返回值：

- 返回 `sink_ptr`。

### `make_daily_file_sink(const std::filesystem::path& path, daily_file_sink_options options = {}) -> sink_ptr`

功能说明：创建一个按天切分的文件 sink。

参数说明：

- `path`
  日志文件基础路径。
- `options`
  日切配置。

返回值：

- 返回 `sink_ptr`。

### `make_callback_sink(callback_sink::callback_type callback) -> sink_ptr`

功能说明：创建一个回调 sink。

参数说明：

- `callback`
  用户自定义回调，接收已格式化文本。

返回值：

- 返回 `sink_ptr`。

### `make_async_sink(sink_ptr inner_sink, async_options options = {}) -> sink_ptr`

功能说明：把一个已有 sink 包装成异步 sink。

参数说明：

- `inner_sink`
  被包装的真实 sink。
- `options`
  异步队列配置。

返回值：

- 返回 `sink_ptr`。

## 9. registry 与全局函数

> 全局管理入口：用于创建、查询、替换和批量刷新 logger。

### `create_logger(std::string name, std::vector<sink_ptr> sinks, log_level level = log_level::info) -> logger_ptr`

功能说明：创建并注册一个命名 logger。

参数说明：

- `name`
  logger 名称。
- `sinks`
  绑定的 sink 集合。
- `level`
  logger 初始级别。

返回值：

- 返回 `logger_ptr`。

### `pipeline(std::string name) -> logger_builder`

功能说明：创建链式 logger 构造器，用独立的零宏 API 组合级别、pattern、sink、异步包装和源码路径策略。

常用链式成员：

```cpp
fastlog::pipeline("app")
    .at(fastlog::log_level::trace)
    .format_as("%Y [%^%L%$] [%n] [%@] %v")
    .source(fastlog::source_path_mode::relative, std::filesystem::current_path())
    .write_to(fastlog::make_stdout_sink())
    .write_to_async(fastlog::make_rotating_file_sink("logs/app.log"))
    .install();
```

返回值：

- `make()`：只构造 logger，不注册。
- `install()`：构造并注册命名 logger。
- `install_as_default()`：构造、注册并设为默认 logger。

### `parse_level(std::string_view text) -> std::optional<log_level>`

功能说明：解析大小写不敏感的日志级别文本。

### `to_string(log_level level) -> std::string_view`

功能说明：返回完整级别文本。

### `to_short_string(log_level level) -> std::string_view`

功能说明：返回短级别文本。

### `get_logger(std::string_view name) -> logger_ptr`

功能说明：按名称获取命名 logger。

参数说明：

- `name`
  logger 名称。

返回值：

- 找到时返回 `logger_ptr`。
- 未找到时返回空指针。

### `drop_logger(std::string_view name)`

功能说明：删除一个命名 logger。

参数说明：

- `name`
  logger 名称。

返回值：

- 无返回值。

### `drop_all_loggers()`

功能说明：清空所有命名 logger。

参数说明：

- 无。

返回值：

- 无返回值。

### `default_logger() -> logger_ptr`

功能说明：获取默认 logger。

参数说明：

- 无。

返回值：

- 返回默认 logger 的共享指针。

### `console_logger() -> logger_ptr`

功能说明：获取控制台 logger。

参数说明：

- 无。

返回值：

- 返回控制台 logger 的共享指针。

### `set_default_logger(logger_ptr logger_ptr_value)`

功能说明：设置默认 logger。

参数说明：

- `logger_ptr_value`
  新的默认 logger。

返回值：

- 无返回值。

### `set_console_logger(logger_ptr logger_ptr_value)`

功能说明：设置控制台 logger。

参数说明：

- `logger_ptr_value`
  新的控制台 logger。

返回值：

- 无返回值。

### `flush_all()`

功能说明：刷新所有已注册 logger 和默认 logger。

参数说明：

- 无。

返回值：

- 无返回值。

## 10. 栈追踪与异常接口

> 诊断接口：适合异常增强输出、栈追踪文本采集和广播式异常日志。

### `current_stacktrace(std::size_t skip = 0) -> std::string`

功能说明：获取当前线程的栈追踪文本。

参数说明：

- `skip`
  需要跳过的栈帧数。

返回值：

- 返回栈追踪字符串。

### `current_exception_stacktrace() -> std::string`

功能说明：获取当前异常的栈追踪文本。

参数说明：

- 无。

返回值：

- 返回异常栈追踪字符串。

### 异常类型别名

功能说明：对外暴露统一异常类型别名。开启 `FASTLOG_WITH_CPPTRACE=ON` 时映射到 `cpptrace`，否则映射到标准库异常。

```cpp
using traced_exception = ...;
using runtime_error = ...;
using logic_error = ...;
using invalid_argument = ...;
using out_of_range = ...;
```

### `log_exception_to_all(const std::exception& exception, std::source_location location = std::source_location::current())`

功能说明：把一个异常对象广播到默认 logger 和所有已注册 logger。

参数说明：

- `exception`
  要输出的异常对象。
- `location`
  调用点源码位置。

返回值：

- 无返回值。

### `log_current_exception_to_all(std::string_view prefix = "Unhandled exception", std::source_location location = std::source_location::current())`

功能说明：把当前异常上下文广播到默认 logger 和所有已注册 logger。

参数说明：

- `prefix`
  自定义前缀。
- `location`
  调用点源码位置。

返回值：

- 无返回值。

## 11. 综合示例

```cpp
#include "fastlog/fastlog.hpp"

int main() {
    fastlog::set_console_level(fastlog::log_level::info);
    fastlog::set_console_detail_mode(fastlog::detail_mode::compact);

    fastlog::FileLoggerOptions options{
        .level = fastlog::log_level::debug,
        .detail_mode = fastlog::detail_mode::standard,
        .source_path = fastlog::source_path_mode::filename,
        .async_write = true
    };

    auto file_logger = fastlog::file::make_logger("app", "logs/app.log", options);

    fastlog::console.info("console ready");
    file_logger.warn("file logger ready");
    fastlog::file::flush(file_logger);
}
```
