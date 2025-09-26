#pragma once
#include "fastlog/detail/util.hpp"
#include "logger.hpp"
#include <filesystem>
#include <unordered_map>

namespace fastlog::detail {
/*
  文件日志器管理器，用于创建、删除、获取文件日志器
  基于unordered_map实现，key为日志器名称，value为日志器
*/
class FileLoggerManager : util::noncopyable {
public:
  FileLogger &make_logger(const std::string &loggername,
                          std::filesystem::path filepath) {
    __loggers.emplace(loggername, filepath);
    return __loggers.at(loggername);
  }
  void delete_logger(const std::string &loggername) {
    __loggers.erase(loggername);
  }
  FileLogger *get_logger(const std::string &loggername) {
    if (this->__loggers.find(loggername) != this->__loggers.end()) {
      return std::addressof(__loggers.at(loggername));
    }
    return nullptr;
  }

private:
  std::unordered_map<std::string, FileLogger> __loggers;
};
} // namespace fastlog::detail
