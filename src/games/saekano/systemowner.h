#pragma once
#include "systemsave.h"

namespace Impacto::Saekano {
struct SemanticState;
// Native pre-title defaults plus startup's no-slot result and authored tables.
// Title playback marks its own music history; construction does not invent it.
NativeSystemSave MakeFreshSystemProgress(Bytes firstDat,bool initiallyRead);
// Global progress/preferences only. A system.bin file is not a script-slot restore.
void LoadSystemOwner(SemanticState& owner,const NativeSystemSave& progress,
                     std::vector<int32_t> requiredCg);
// Apply only current preference fields; preserve histories, banks and runtime.
void ApplySystemPreferences(SemanticState& owner,const NativeSystemSave& progress);
NativeSystemSave SnapshotSystemOwner(const SemanticState& owner,
                                     const RegisteredReadHistory& history);
}
