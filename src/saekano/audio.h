#pragma once

#include "formats.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <unordered_map>

namespace Impacto::Saekano {

struct AwbEntry {
  uint32_t Id = 0;
  uint64_t Offset = 0;
  uint64_t Size =
      0;  // Payload only; alignment before the next entry is excluded.
};

// AFS2 version 1, 16/32-bit IDs and offsets. Parsing requires only the index,
// never the bank's audio payload. Unknown formats fail explicitly.
class AwbIndex {
 public:
  static size_t IndexSize(std::span<const uint8_t> header, uint64_t fileSize);
  AwbIndex(std::span<const uint8_t> index, uint64_t fileSize);
  const std::vector<AwbEntry>& Entries() const { return entries_; }
  const AwbEntry& Find(uint32_t id) const;
  uint16_t Alignment() const { return alignment_; }
  uint16_t Subkey() const { return subkey_; }

 private:
  uint16_t alignment_ = 0;
  uint16_t subkey_ = 0;
  std::vector<AwbEntry> entries_;
  std::unordered_map<uint32_t, size_t> byId_;
};

// Independently opened banks/readers may run on separate threads. A shared bank
// is sequential: Read() updates its file cursor. All reads stay inside one
// wave.
class AwbBank {
 public:
  explicit AwbBank(const std::filesystem::path& path);
  const AwbIndex& Index() const { return *index_; }
  void Read(uint32_t id, uint64_t offset, std::span<uint8_t> output);

 private:
  std::ifstream file_;
  std::unique_ptr<AwbIndex> index_;
};

struct HcaWaveInfo {
  uint32_t SampleRate = 0;
  uint32_t Channels = 0;
  uint32_t HeaderSize = 0;
  uint32_t BlockSize = 0;
  uint32_t BlockCount = 0;
  uint32_t EncoderDelay = 0;
  uint32_t EncoderPadding = 0;
  uint64_t Frames = 0;  // Audible frames, with encoder delay/padding removed.
  bool Loop = false;
  uint64_t LoopStart = 0;
  uint64_t LoopEnd = 0;  // Exclusive; same audible-frame coordinate as Frames.
  bool Encrypted = false;
};

// Uses Impacto's existing vendor/clHCA decoder. No codec reimplementation or
// entire-bank/entire-track buffering. Read returns interleaved signed 16-bit
// PCM. Seek is exact: reset and replay decoder history. Loop scheduling belongs
// to the playback consumer; reads stop at Frames, without automatically
// looping.
class HcaWaveReader {
 public:
  HcaWaveReader(std::shared_ptr<AwbBank> bank, uint32_t id,
                std::optional<uint64_t> key = std::nullopt);
  ~HcaWaveReader();
  HcaWaveReader(const HcaWaveReader&) = delete;
  HcaWaveReader& operator=(const HcaWaveReader&) = delete;
  const HcaWaveInfo& Info() const;
  uint64_t Position() const;
  size_t Read(
      std::span<int16_t> output);  // Returns frames, not scalar samples.
  void Seek(uint64_t frame);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace Impacto::Saekano
