#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace Impacto::Saekano {
// Semantic subset of the original env_string_format.dat grammar. Formatting
// remains renderer-owned; raw message text is retained for save/backlog state.
std::vector<int32_t> OriginalMessageKeywords(const std::u16string& text);
}  // namespace Impacto::Saekano
