#pragma once

#include "fastlog/detail/registry.hpp"

#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fastlog {

[[nodiscard]] inline auto current_stacktrace(std::size_t skip = 0) -> std::string {
  return detail::current_stacktrace_text(skip);
}

[[nodiscard]] inline auto current_exception_stacktrace() -> std::string {
  return detail::current_exception_stacktrace_text();
}

#ifdef FASTLOG_WITH_CPPTRACE
using traced_exception = cpptrace::exception;
using runtime_error = cpptrace::runtime_error;
using logic_error = cpptrace::logic_error;
using invalid_argument = cpptrace::invalid_argument;
using out_of_range = cpptrace::out_of_range;
#else
using traced_exception = std::runtime_error;
using runtime_error = std::runtime_error;
using logic_error = std::logic_error;
using invalid_argument = std::invalid_argument;
using out_of_range = std::out_of_range;
#endif

inline void log_exception_to_all(
    const std::exception &exception,
    std::source_location location = std::source_location::current()) {
  if (const auto default_ptr = default_logger(); default_ptr != nullptr) {
    default_ptr->exception(exception, location);
  }
  for (const auto &logger_ptr_value : registry::instance().snapshot()) {
    logger_ptr_value->exception(exception, location);
  }
}

inline void log_current_exception_to_all(
    std::string_view prefix = "Unhandled exception",
    std::source_location location = std::source_location::current()) {
  if (const auto default_ptr = default_logger(); default_ptr != nullptr) {
    default_ptr->current_exception(prefix, location);
  }
  for (const auto &logger_ptr_value : registry::instance().snapshot()) {
    logger_ptr_value->current_exception(prefix, location);
  }
}

} // namespace fastlog
