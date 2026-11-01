#pragma once
#include "slotdata.h"
#include <string>

namespace Impacto::Saekano {
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
  size_t Count() const { return entries_.size(); }
  size_t Oldest() const { return oldest_; }
  size_t Next() const { return next_; }
  const BacklogEntry& At(size_t chronologicalIndex) const;
  void AppendChoice(std::u16string_view label, const NativeSlotRecord& working,
                    const BacklogTextEnvironment& environment);
  void Clear();
 private:
  std::vector<BacklogEntry> entries_;
  size_t oldest_ = 0, next_ = 0;
};
}
