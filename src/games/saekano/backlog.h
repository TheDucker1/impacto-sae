#pragma once
#include "slotdata.h"
#include <string>

namespace Impacto::Saekano {
struct MessageBacklogGates {
  uint8_t Battle = 0, Approach = 0, Suppressed = 0;
  uint32_t CompletionPolicy = 0;
};
bool MessageBacklogEnabled(const MessageBacklogGates& gates);
struct BacklogTextEnvironment {
  uint32_t LayoutParameter = 0;  // Native global25bf1c -> text helper19cda.
  float Scale = 0;
  std::vector<std::array<uint8_t,4>> Colors;
  static BacklogTextEnvironment Decode(std::span<const uint8_t> bytes);
};
struct BacklogText {
  std::u16string Source;
  std::array<uint8_t,4> Primary{}, Secondary{};
  uint32_t LayoutParameter = 0;
  float Scale = 0;
  int32_t VoiceId = -1; // Retained prepared reference; muted/unvoiced records have none.
};
struct BacklogEntry {
  int32_t Kind = 0;
  uint8_t Flag = 0;
  std::vector<BacklogText> Text;
  NativeSlotRecord Record;
};
class BacklogHistory {
 public:
  static constexpr size_t Capacity = 200;
  size_t Count() const { return count_; }
  size_t Oldest() const { return oldest_; }
  size_t Next() const { return next_; }
  const BacklogEntry& At(size_t chronologicalIndex) const;
  void AppendChoice(std::u16string_view label, const NativeSlotRecord& working,
                    const BacklogTextEnvironment& environment);
  void AppendMessage(std::span<const std::u16string> text, bool readColor,
                     uint8_t voiceGroup, const NativeSlotRecord& working,
                     const BacklogTextEnvironment& environment,int32_t voiceId=-1);
  void Truncate(size_t count); // Native57d8 excludes the row that will restart.
  void Clear();
 private:
  std::vector<BacklogEntry> entries_;
  size_t oldest_ = 0, next_ = 0, count_ = 0;
  void Append(int32_t kind,uint8_t flag,std::span<const std::u16string> text,
              const NativeSlotRecord& working,const BacklogTextEnvironment& environment);
};
}
