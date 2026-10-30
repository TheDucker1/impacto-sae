#pragma once
#include "systemsave.h"

namespace Impacto::Saekano {
struct SemanticState;
// Global progress/preferences only. A system.bin file is not a script-slot restore.
void LoadSystemOwner(SemanticState& owner,const NativeSystemSave& progress,
                     std::vector<int32_t> requiredCg);
NativeSystemSave SnapshotSystemOwner(const SemanticState& owner,
                                     const RegisteredReadHistory& history);
}
