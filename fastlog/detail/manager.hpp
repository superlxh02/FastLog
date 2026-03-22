#pragma once
#include "fastlog/detail/util.hpp"
#include "logger.hpp"
#include <filesystem>
#include <mutex>
#include <unordered_map>

namespace fastlog::detail {
/*
  文件日志器管理器，用于创建、删除、获取文件日志器
  基于unordered_map实现，key为日志器名称，value为日志器
*/
class FileLoggerManager : util::noncopyable {
public:
  // 创建文件 logger。
  // 如果 logger 已存在，则复用原对象并应用新的配置。
  FileLogger &make_logger(const std::string &loggername,
                          std::filesystem::path filepath,
                          const FileLoggerOptions &options = {}) {
    std::lock_guard lock(__mtx);
    auto [it, inserted] =
        __loggers.try_emplace(loggername, filepath, options);
    if (!inserted) {
      it->second.apply_options(options);
    }
    return it->second;
  }

  // 删除指定名称的文件 logger
  void delete_logger(const std::string &loggername) {
    std::lock_guard lock(__mtx);
    __loggers.erase(loggername);
  }

  // 获取指定名称的文件 logger，不存在时返回 nullptr
  FileLogger *get_logger(const std::string &loggername) {
    std::lock_guard lock(__mtx);
    if (auto it = this->__loggers.find(loggername); it != this->__loggers.end()) {
      return std::addressof(it->second);
    }
    return nullptr;
  }

  // 遍历所有已注册的文件 logger，用于批量刷新或统一输出异常
  template <typename F> void for_each_logger(F &&fn) {
    std::lock_guard lock(__mtx);
    for (auto &[name, logger] : __loggers) {
      (void)name;
      std::forward<F>(fn)(logger);
    }
  }

private:
  std::mutex __mtx{};
  std::unordered_map<std::string, FileLogger> __loggers;
};
} // namespace fastlog::detail
