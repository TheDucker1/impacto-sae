#pragma once

#include <stdexcept>

namespace Impacto {

class FormatError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

}  // namespace Impacto
