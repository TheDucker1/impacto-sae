#pragma once

#include "music.h"

namespace Impacto::Saekano {
struct PlaySeCommand {
  uint32_t Route = 0;
  int32_t Track = 0, Channel = 0, Volume = 0;
  bool Remember = false;
  uint32_t Lane = 0;
  static PlaySeCommand Decode(std::span<const uint8_t> bytes);
};
struct SePlayRequest {
  int32_t Track = 0, Channel = 0, CommandVolume = 0;
  bool Remember = false;
  std::optional<BgmVolume> Volume;
};
// SE uses F32 multiplication/division before VCVT, unlike BGM's signed integer
// product/division. Both backend results are unsigned integer percentages.
BgmVolume SeVolume(int32_t master, int32_t command, float coefficient);
class SeResources {
 public:
  virtual ~SeResources() = default;
  virtual void Prepare(int32_t track) = 0;
  virtual float Coefficient() const = 0;
};
class ArchiveSeResources final : public SeResources {
 public:
  explicit ArchiveSeResources(const std::filesystem::path& usrdir);
  void Prepare(int32_t track) override;
  float Coefficient() const override { return environment_.SeCoefficient; }
  const HcaWaveInfo& Info(int32_t track) const;
  std::unique_ptr<HcaWaveReader> Open(int32_t track) const;

 private:
  AudioEnvironment environment_;
  std::shared_ptr<AwbBank> bank_;
  std::map<int32_t, HcaWaveInfo> prepared_;
};
void StartPlaySe(SemanticState& state, const PlaySeCommand& command,
                 SeResources& resources);
}  // namespace Impacto::Saekano
