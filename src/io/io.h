#pragma once

#include <cstdint>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace Impacto {

enum IoError : int64_t {
  IoError_OK = 0,
  IoError_Fail = -1,
  IoError_Eof = -2,
  IoError_NotFound = -3
};

namespace Io {
inline size_t MemorySize(int64_t bytes, size_t extra = 0) {
  if (bytes < 0 || uint64_t(bytes) > std::numeric_limits<size_t>::max() - extra)
    throw std::length_error("stream size exceeds addressable memory");
  return static_cast<size_t>(bytes) + extra;
}
}  // namespace Io

}  // namespace Impacto
