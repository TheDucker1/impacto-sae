#include "skipauto.h"
#include "commands.h"

#include <bit>

namespace Impacto::Saekano {
RegisteredReadHistory RegisteredReadHistory::Decode(
    std::span<const uint8_t> zz1, std::span<const uint8_t> zz2,
    std::span<const uint8_t> zz3, bool initiallyRead) {
  PayloadReader first(zz1), branches(zz2), segments(zz3);
  const auto count = first.U32();
  // Native trusts all counts/indexes. Bound allocations and reject corrupt
  // inputs before indexing; preserve grouping/order/narrowing for valid data.
  if (count > 65536) throw FormatError("history script count exceeds budget");
  RegisteredReadHistory result;
  result.Files.resize(count);
  const auto branchCount = branches.U32();
  if (branchCount > branches.Remaining() / 12 || branchCount > 1000000)
    throw FormatError("truncated registered history branches");
  std::optional<uint32_t> previous;
  size_t flagsTotal = 0;
  for (uint32_t i = 0; i < branchCount; ++i) {
    const auto file = branches.U32();
    const auto endpoint = uint16_t(branches.U32()); // High16 reserved/ignored.
    const auto length = branches.U32();
    if (file >= count || length > 1000000 || flagsTotal > 1000000 - length)
      throw FormatError("registered history branch exceeds bounds");
    flagsTotal += length;
    auto& rows = result.Files[file].Branches;
    // The native temporary list is flushed on each group change. A repeated
    // noncontiguous group replaces its earlier contents, rather than appending.
    if (!previous || *previous != file) rows.clear();
    rows.push_back({endpoint, std::vector<uint8_t>(length, initiallyRead ? 1 : 0)});
    previous = file;
  }
  const auto segmentCount = segments.U32();
  if (segmentCount > segments.Remaining() / 8 || segmentCount > 1000000)
    throw FormatError("truncated or excessive registered history segments");
  previous.reset();
  for (uint32_t i = 0; i < segmentCount; ++i) {
    const auto file = segments.U32();
    auto endpoint = segments.U32();
    if (file >= count) throw FormatError("registered history script out of range");
    auto& rows = result.Files[file].Segments;
    if (!previous || *previous != file) rows.clear();
    if (initiallyRead) endpoint |= 0x8000;
    rows.push_back(uint16_t(endpoint));
    previous = file;
  }
  return result;
}
RegisteredReadHistory RegisteredReadHistory::LoadFirst(Bytes firstDat,
                                                       bool initiallyRead) {
  GpdaArchive first(std::move(firstDat));
  GpdaArchive tables(first.Data(4));
  const auto zz1 = tables.Data(0), zz2 = tables.Data(1), zz3 = tables.Data(2);
  return Decode(zz1, zz2, zz3, initiallyRead);
}
HistoryEnvironment HistoryEnvironment::DecodeDebug(std::span<const uint8_t> bytes) {
  // Three header flags;20 feature bytes; two scalar words; one preceding
  // feature byte; then native8025c25d. The prefix has no counted fields.
  constexpr size_t At = 3 + 20 + 2 * 4 + 1;
  if (bytes.size() <= At) throw FormatError("truncated history debug settings");
  return {bytes[At], At + 1, bytes[3 + 11]};
}
const ReadHistoryBranch* FindChoiceHistory(const RegisteredReadHistory& history,
    int32_t script,uint32_t command) {
  if(script<0 || size_t(script)>=history.Files.size())
    throw FormatError("choice outside registered history scripts");
  for(const auto& row:history.Files[size_t(script)].Branches)
    if(row.Endpoint==command)return &row;
  return nullptr;
}
void MarkChoiceHistory(const HistoryEnvironment& environment,RegisteredReadHistory& history,
    int32_t script,uint32_t command,std::span<const size_t> flags) {
  if(environment.SuppressReadHistory)return;
  const auto* found=FindChoiceHistory(history,script,command);
  if(!found)return;
  for(const auto flag:flags)if(flag>=found->Flags.size())
    throw FormatError("choice history flag outside registered row");
  // The lookup borrows this mutable owner; keep the same first matching row.
  auto& row=history.Files[size_t(script)].Branches[size_t(found-history.Files[size_t(script)].Branches.data())];
  for(const auto flag:flags)row.Flags[flag]=1;
}
std::vector<std::array<uint8_t,4>> ApproachChoiceHistory(
    const HistoryEnvironment& environment,const RegisteredReadHistory& history,
    int32_t script,uint32_t command,size_t parents,bool readColor) {
  if(parents>6)throw FormatError("approach history parent budget exceeded");
  std::vector<std::array<uint8_t,4>> result(parents);
  if(!readColor || environment.SuppressReadHistory)return result;
  const auto* row=FindChoiceHistory(history,script,command);
  if(!row)return result;
  if(row->Flags.size()<parents*4)throw FormatError("truncated approach choice history");
  for(size_t i=0;i<parents;++i)
    for(size_t j=0;j<4;++j)result[i][j]=row->Flags[i*4+j];
  return result;
}
bool SkipHistoryEligible(const HistoryEnvironment& environment,
                          std::span<const ScriptRecord> records,
                          uint32_t current, const ScriptReadHistory* history) {
  for (size_t i = current; i < records.size(); ++i) {
    const auto opcode = records[i].Opcode;
    if (opcode != 0x64 && opcode != 0x65 && opcode != 0x2be) continue;
    if (environment.SuppressReadHistory) return false;
    if (!history) throw UnsupportedCommand("current-file read history is unknown");
    // Look-ahead locates a relevant opcode; the lookup still uses current,
    // not that future record's index. Preserve original first-match order.
    for (const auto segment : history->Segments) {
      const uint16_t endpoint = segment & 0x7fff;
      if (endpoint < current) continue;
      bool read = (segment & 0x8000) != 0;
      for (const auto& branch : history->Branches) {
        if (branch.Endpoint < current) continue;
        if (branch.Endpoint <= endpoint) {
          read = false;
          for (const auto value : branch.Flags)
            if (value) {
              read = true;
              break;
            }
        }
        break;
      }
      return read;
    }
    return true;  // Native empty/exhausted segment table behavior.
  }
  return false;
}
void MarkHistoryRead(const HistoryEnvironment& environment,
                      ScriptReadHistory& history, uint32_t command) {
  if (environment.SuppressReadHistory) return;
  for (auto& segment : history.Segments)
    if (segment == command) {
      segment |= 0x8000;
      break;
    }
}

void OrdinaryAutoRotation::Begin(int32_t duration, uint64_t now) {
  Channel.Start(360, 0, duration, now);
  Started = true;
}
void OrdinaryAutoRotation::Update(uint64_t now) {
  if (!Started) throw UnsupportedCommand("ordinary auto rotation is unknown");
  // Native Tick passes a signed low32 sample delta to its update routine.
  // A negative delta restarts the epoch, including gaps >=2^31 milliseconds.
  const auto delta = std::bit_cast<int32_t>(uint32_t(now - Channel.Sample));
  if (delta < 0) Channel.Epoch = Channel.Sample = now;
  Channel.Tick(Channel.Sample + uint32_t(delta < 0 ? 0 : delta));
  if (Channel.Finished())
    Channel.StartUnsigned(Channel.From, Channel.To, Channel.Duration, now);
}

SkipAutoEnvironment SkipAutoEnvironment::Decode(std::span<const uint8_t> bytes) {
  if (bytes.size() < 3) throw FormatError("truncated global environment flags");
  PayloadReader reader(bytes.subspan(3));
  for (int i = 0; i < 4; ++i) reader.String();
  reader.U32();
  reader.U32();
  for (int i = 0; i < 9; ++i) reader.String();
  // Height and two scalars, rect/point pairs, float, rect/point, float.
  // Each native rect/point reader consumes four/two serialized words.
  for (int count : {3, 4, 2, 4, 2, 1, 4, 2, 1})
    for (int i = 0; i < count; ++i) reader.U32();
  SkipAutoEnvironment result;
  result.AutoKeyDuration = reader.I32();
  if (result.AutoKeyDuration < 0)
    throw FormatError("negative auto key duration");
  result.PrefixBytes = bytes.size() - reader.Remaining();
  return result;
}

void UpdateSkipAutoPolicy(SemanticState& owner, SkipAutoState& policy,
                          const SkipAutoInput& input, int32_t autoKeyDuration,
                          uint64_t now) {
  if (autoKeyDuration < 0) throw FormatError("negative auto key duration");
  auto next = policy;
  auto mode = owner.SkipMode, phase = owner.SkipPhase;
  const bool blocked = input.HideMessages || input.ServicesBlocked;
  const bool eligible = owner.SkipUnreadEnabled != 0 || input.HistoryGate;
  bool heldEligible = false, resetEffects = false;
  if (!blocked) {
    if (mode != 2) {
      mode = input.SkipHeld ? 1 : 0;
      heldEligible = input.SkipHeld && eligible;
    }
    const bool gesture =
        input.PrimaryFlick &&
        (mode == 2 ? input.PrimaryVelocityX < 0 : input.PrimaryVelocityX > 0);
    if (input.SkipPressed || input.SkipButtonReleased || gesture) {
      if (mode == 2) {
        mode = 0;
        next.SkipIndicators = {false, false};
      } else
        mode = 2;
    }
    if ((mode == 2 && eligible) || heldEligible) {
      if (!owner.Effects.Known)
        throw UnsupportedCommand("skip policy common effect state is missing");
      resetEffects = true;
      next.Auto = false;
      if (phase == 0) {
        phase = 1;
        next.SkipIndicators = {true, true};
        next.SkipStartedAt = now;
      }
    } else {
      phase = 0;
      next.SkipIndicators = {mode != 0, mode != 0};
    }
  } else {
    phase = 0;
    next.SkipIndicators = {mode != 0, mode != 0};
  }
  if (phase == 3) {
    phase = 1;
    next.Auto = false;
    next.SkipIndicators = {true, true};
  }
  if (!blocked) {
    bool allowButton = true;
    if (input.AutoPressed) {
      next.AutoKeyTimer.Start(0, autoKeyDuration, autoKeyDuration, now);
      next.AutoKeyPlaying = true;
      allowButton = false;
    }
    if (input.AutoHeld) {
      if (next.AutoKeyPlaying) next.AutoKeyTimer.Tick(now);
      if (next.AutoKeyTimer.Finished()) {
        next.Auto = !next.Auto;
        next.AutoKeyTimer.Start(0, autoKeyDuration, autoKeyDuration, now);
        next.AutoKeyPlaying = false;
      }
    } else if (allowButton &&
               (input.AutoButtonReleased || input.PrimaryFullHold)) {
      next.Auto = !next.Auto;
    }
  }
  next.AutoIndicator = next.Auto;
  owner.SkipMode = mode;
  owner.SkipPhase = phase;
  if (resetEffects) {
    owner.Effects.CurrentValue = owner.Effects.RestoreValue;
    for (uint32_t i = 0; i < 2; ++i)
      owner.PendingEvents.push_back(
          {GameEventKind::StopChannel,
           std::bit_cast<int32_t>(uint32_t(owner.Effects.FirstAudioChannel) +
                                  i)});
  }
  policy = next;
}
}  // namespace Impacto::Saekano
