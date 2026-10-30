#pragma once
#include "formats.h"
#include <array>
#include <filesystem>

namespace Impacto::Saekano {
struct RegisteredReadHistory;
// Preserve numeric/byte fields without inventing meanings for untraced consumers.
// Source offsets refer to the native ScenarioOwner, not serialized file offsets.
struct SystemSaveChoice { int16_t Id=0;Bytes Values; };
struct SystemSaveHistory { std::vector<int32_t> Values;int32_t Fallback=0; };
struct NativeSystemSave {
  static constexpr size_t FileBytes=0x1e000;
  uint64_t Word60148=0,Word60140=0;
  std::array<int16_t,80> Bank1{};
  std::array<uint8_t,60> Bytes60150{};
  std::vector<std::vector<uint16_t>> Rows60244;
  std::vector<std::vector<SystemSaveChoice>> Rows60254;
  std::array<SystemSaveHistory,9> Histories;
  uint32_t Word60318=0;
  std::array<uint8_t,13> Bytes6022c{};
  std::array<uint8_t,9> Bytes6023a{};
  uint8_t Byte60239=0;
  size_t UsedBytes=0;
  static NativeSystemSave Decode(std::span<const uint8_t> bytes);
  Bytes Encode()const;
};
// Restore/store both per-script collections, preserving raw endpoint bits and flags.
RegisteredReadHistory RestoreSystemReadHistory(const NativeSystemSave& state);
void StoreSystemReadHistory(NativeSystemSave& state,const RegisteredReadHistory& history);
NativeSystemSave ReadSystemSaveFile(const std::filesystem::path& path);
// A flushed sibling file replaces the destination only after complete encoding.
// An existing .pending file is preserved and rejected for explicit recovery.
void WriteSystemSaveFile(const std::filesystem::path& path,const NativeSystemSave& state);
}
