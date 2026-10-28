#pragma once

#include <filesystem>
#include <iosfwd>
#include <optional>

namespace Impacto::Saekano {
// Original profile destination, fresh unread controls, fixed16ms clock. Returns
// nonzero at unsupported behavior/budget exhaustion; never skips a command.
int RunSceneContinuation(const std::filesystem::path& usrdir, size_t frameBudget,
                         std::optional<size_t> untilMessage, std::ostream& report,
                         bool confirmFirstMission = false);
}
