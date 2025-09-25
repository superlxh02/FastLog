#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <string>
namespace fastlog::detail {
// 基于std::array封装文件日志器的缓冲区
template <std::size_t SIZE> class FileLogBuffer {
public:
  FileLogBuffer() noexcept : __cur{__data.begin()} {}

  void write(const std::string &str) noexcept {
    assert(writeable() > str.size());
    std::copy(str.begin(), str.end(), __cur);
    __cur += str.size();
  };

  [[nodiscard]]
  constexpr auto capacity() noexcept -> std::size_t {
    return SIZE;
  }

  [[nodiscard]]
  auto size() noexcept -> std::size_t {
    return std::distance(__data.begin(), __cur);
  }

  [[nodiscard]]
  auto writeable() noexcept -> std::size_t {
    return capacity() - size();
  }

  [[nodiscard]]
  auto data() const noexcept -> const char * {
    return __data.data();
  }

  [[nodiscard]]
  auto empty() const noexcept -> bool {
    return __cur == __data.begin();
  }

  void reset() noexcept { __cur = __data.begin(); }

private:
  std::array<char, SIZE> __data{};
  std::array<char, SIZE>::iterator __cur{};
};
} // namespace fastlog::detail