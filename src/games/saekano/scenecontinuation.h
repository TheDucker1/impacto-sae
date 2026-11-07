#pragma once

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <array>
#include <span>

namespace Impacto::Saekano {
// Original profile destination, fresh unread controls, fixed16ms clock. Returns
// nonzero at unsupported behavior/budget exhaustion; never skips a command.
int RunSceneContinuation(const std::filesystem::path& usrdir, size_t frameBudget,
                         std::optional<size_t> untilMessage, std::ostream& report,
                         bool confirmFirstMission = false,bool chooseFirstApproach = false,
                         std::span<const std::array<size_t,2>> approachPlan = {},
                         bool confirmApproachResult = false,bool followPostResult = false,
                         bool followPostChapter = false,
                         std::span<const std::array<size_t,2>> profilePlan = {},bool confirmBattleVersus=false,
                         std::span<const std::array<size_t,3>> battlePlan={},bool continueBattle=false,bool confirmBattleResult=false,
                         bool followBattleResult=false,std::optional<int32_t> endingDecision={},std::optional<size_t> untilApproachChoices={},
                         const std::filesystem::path& systemSave={},const std::filesystem::path& systemOutput={},
                         const std::filesystem::path& progressRoot={});
}
