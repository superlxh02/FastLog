# FastLog API Docs

FastLog 的 GitHub Pages 接口说明文档。

> 面向使用者的公开接口手册，适合查阅类型、配置项、函数签名、参数语义和返回值说明。

## 文档范围

- 日志级别、输出模式、配置对象
- 基础控制台接口
- 基础文件接口
- 高级 logger / sink / registry 接口
- 栈追踪与异常相关接口

## 快速导航

| 分类       | 说明                                  | 入口                                                   |
| ---------- | ------------------------------------- | ------------------------------------------------------ |
| 基础接口   | 开箱即用的控制台和文件日志接口        | [查看基础接口](./api-reference.md#4-基础控制台接口)    |
| 高级接口   | `logger / sink / registry` 组合式接口 | [查看高级接口](./api-reference.md#6-fastloglogger)     |
| 类型与配置 | 枚举、配置对象、公开结构体            | [查看类型与配置](./api-reference.md#2-基础类型)        |
| 异常与诊断 | 栈追踪、异常别名、异常广播接口        | [查看异常接口](./api-reference.md#10-栈追踪与异常接口) |
| 设计说明   | 整体架构、源码解析、流程说明          | [查看设计文档](./design.md)                            |

## 快速开始

```cpp
#include "fastlog/fastlog.hpp"

int main() {
    fastlog::set_console_level(fastlog::log_level::info);
    fastlog::set_console_detail_mode(fastlog::detail_mode::compact);
    fastlog::console.info("hello fastlog");
}
```

## 最常用接口速查

| 场景               | 接口                                    |
| ------------------ | --------------------------------------- |
| 控制台输出一条日志 | `fastlog::console.info("...")`          |
| 设置控制台级别     | `fastlog::set_console_level(...)`       |
| 设置控制台输出模式 | `fastlog::set_console_detail_mode(...)` |
| 创建文件 logger    | `fastlog::file::make_logger(...)`       |
| 刷新文件 logger    | `fastlog::file::flush(...)`             |
| 创建高级 logger    | `fastlog::create_logger(...)`           |
| 创建控制台 sink    | `fastlog::make_stdout_sink()`           |
| 创建轮转文件 sink  | `fastlog::make_rotating_file_sink(...)` |
| 创建异步 sink      | `fastlog::make_async_sink(...)`         |
| 获取栈追踪文本     | `fastlog::current_stacktrace()`         |

## 典型任务入口

### 我只想快速打印日志

先看：

- [基础控制台接口](./api-reference.md#4-基础控制台接口)
- [基础文件接口](./api-reference.md#5-基础文件接口)

### 我想自定义输出结构

先看：

- [公开结构体](./api-reference.md#3-公开结构体)
- [fastlog::sink](./api-reference.md#7-fastlogsink)
- [sink 工厂函数](./api-reference.md#8-sink-工厂函数)

### 我想做多 sink / 异步日志

先看：

- [fastlog::logger](./api-reference.md#6-fastloglogger)
- [sink 工厂函数](./api-reference.md#8-sink-工厂函数)
- [registry 与全局函数](./api-reference.md#9-registry-与全局函数)

### 我想处理异常和栈追踪

先看：

- [栈追踪与异常接口](./api-reference.md#10-栈追踪与异常接口)

## 接口分层

### 1. 基础接口

适合直接开始使用：

- `fastlog::console`
- `fastlog::set_console_level(...)`
- `fastlog::set_console_detail_mode(...)`
- `fastlog::FileLoggerOptions`
- `fastlog::file::make_logger(...)`
- `fastlog::file::flush(...)`

### 2. 高级接口

适合多 sink、自定义 formatter、自定义异步策略：

- `fastlog::create_logger(...)`
- `fastlog::get_logger(...)`
- `fastlog::drop_logger(...)`
- `fastlog::set_default_logger(...)`
- `fastlog::make_stdout_sink()`
- `fastlog::make_rotating_file_sink(...)`
- `fastlog::make_async_sink(...)`

### 3. 诊断与异常接口

- `fastlog::current_stacktrace(...)`
- `fastlog::current_exception_stacktrace()`
- `fastlog::log_exception_to_all(...)`
- `fastlog::log_current_exception_to_all(...)`

## 页面导航

- [API Reference](./api-reference.md)
- [设计文档](./design.md)
- [项目首页](../readme.md)

## 推荐阅读顺序

1. 先看 [API Reference](./api-reference.md) 了解公开接口。
2. 如果只想快速接入，优先阅读基础控制台接口和基础文件接口。
3. 如果需要自定义 sink、异步策略或多 sink 组合，再阅读高级接口章节。
4. 如果需要理解内部设计，再进入 [设计文档](./design.md)。

## 头文件入口

用户侧建议只包含：

```cpp
#include "fastlog/fastlog.hpp"
```

这个入口会统一导出当前公开 API。
