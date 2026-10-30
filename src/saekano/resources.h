#pragma once

#include "vm.h"
#include "image.h"
#include "profile.h"
#include "profiletexture.h"

#include <array>
#include <functional>

namespace Impacto::Saekano {

// One gzip member, with CRC/ISIZE validation and an explicit output budget.
// Archive padding is not compressed input: pass the entry's logical extent.
Bytes InflateGzip(std::span<const uint8_t> input,
                  size_t outputLimit = 256 * 1024 * 1024);

struct ResourceAsset {
  Bytes Name;  // Native counted bytes, including trailing spaces.
  Bytes Data;
};
// Message Load registers voices with the side selected by preceding mode Load
// effects. Mouth envelopes keep their separate all-voice registration order.
std::array<std::vector<int16_t>,2> CollectMessageVoiceCatalogs(const ScriptObject& script);
// Resource.dat child8. Keep counted archive names and authored file order;
// file decoding/simulation belongs to the effect consumer.
struct EffectResourceAssets {
  Bytes Name;
  std::vector<ResourceAsset> Files;
};
// Native startup8002d4a0 loads first.dat child1 into owner+c's texture vector.
class SharedUiAssets {
 public:
  explicit SharedUiAssets(Bytes firstDat);
  size_t Count() const { return archive_.Entries().size(); }
  ResourceAsset Load(size_t entry) const;

 private:
  GpdaArchive archive_;
};
// Native startup2c0b4 installs first.dat child7 as the UI channel6 bank.
class SystemSoundAssets {
 public:
  explicit SystemSoundAssets(Bytes firstDat);
  size_t Count() const { return archive_.Entries().size(); }
  ResourceAsset Load(size_t entry) const;

 private:
  GpdaArchive archive_;
};
struct CharacterPoseAssets {
  Bytes Name;
  // Native pose record+18..44: main, eye and lip rectangles, four words each.
  std::array<int32_t, 12> Geometry{};
  std::array<ResourceAsset, 15> Textures;  // Main then indexed facial overlays.
};
struct ProfileVoiceAssets {
  Bytes Hca;
  ProfileMouthAnimation Mouth;
};
struct ScriptVoiceEnvelope {
  int32_t VoiceId = -1;
  Bytes Name;
  ProfileMouthAnimation Mouth;
};
// Native companion stage consumes one .wav.vol for each first-occurring ID
// from the message-load list. Preserve all payload samples (native strlen+2
// sizing drops the last sample). Unknown IDs are never aliased through int16.
std::vector<ScriptVoiceEnvelope> DecodeScriptVoiceEnvelopes(
    std::span<const int32_t> voiceIds, Bytes envelopeArchive);
// Load-order registrations include ordinary messages and approach mission cues.
// Duplicate filtering belongs to the shared catalog parser, not this traversal.
std::vector<int32_t> CollectScriptVoiceIds(const ScriptObject& script);
CharacterPoseAssets DecodeCharacterPoseArchive(Bytes archive);

// These are native archive section indexes, not alphabetical filename order.
enum class Live2dSection : size_t {
  Moc, Textures, Pose, Expressions, LoopMotions, OneShotMotions,
  AngerMotions, SweatMotions, TearMotions, Count
};

struct Live2dModelAssets {
  Bytes Name;
  std::array<std::vector<ResourceAsset>, size_t(Live2dSection::Count)> Sections;
  const std::vector<ResourceAsset>& Get(Live2dSection section) const {
    return Sections.at(size_t(section));
  }
};

// Opens resource.dat directly; callers need no extracted script or asset tree.
// Assets remain owned bytes until their renderer/audio consumer accepts them.
// Decoding MOC geometry and GXT pixels is a separate consumer responsibility.
class GameResources final : public ScriptSource {
 public:
  explicit GameResources(Bytes resourceDat);
  Bytes Load(int32_t scriptId) override;
  Live2dModelAssets LoadLive2d(size_t modelId) const;
  CharacterPoseAssets LoadCharacterPose(size_t poseId) const;
  size_t CharacterPoseCount() const;
  size_t ProfileSceneCount() const;
  ProfileScene LoadProfileScene(size_t sceneId) const;
  std::vector<ProfileMenuAsset> LoadProfileMenu() const;
  ProfileMenuAsset LoadProfileTexture(ProfileTextureKey key) const;
  size_t TitleTextureCount() const;
  size_t SystemTextureCount() const;
  size_t SystemVoiceBankCount() const;
  size_t SystemVoiceCount(size_t bank) const;
  ResourceAsset LoadSystemVoice(size_t bank,size_t entry) const;
  std::vector<ResourceAsset> LoadApproachTextures(size_t id) const;
  std::vector<ResourceAsset> LoadBattleFaces(size_t group,size_t costume,size_t side) const;
  size_t EffectCount() const;
  EffectResourceAssets LoadEffect(size_t id) const;
  ProfileVoiceAssets LoadProfileVoice(size_t characterId, size_t entry) const;
  ResourceAsset LoadBattleResultVoice(size_t entry) const;
  std::vector<ScriptVoiceEnvelope> LoadScriptVoiceEnvelopes(int32_t scriptId) const;
  ResourceAsset LoadImage(ImageSection section, size_t imageId) const;
  size_t ImageCount(ImageSection section) const;
  size_t ScriptCount() const { return scripts_.Entries().size(); }
  size_t Live2dCount() const { return live2d_.Entries().size(); }
  const Bytes& ScriptName(size_t id) const { return scripts_.Entries().at(id).Name; }

 private:
  explicit GameResources(const GpdaArchive& root);
  GpdaArchive scripts_;
  GpdaArchive live2d_;
  std::optional<GpdaArchive> characterPoses_;
  std::optional<GpdaArchive> profileScenes_;
  std::optional<GpdaArchive> profileVoices_;
  std::optional<GpdaArchive> battleResultVoices_;
  std::optional<GpdaArchive> profilePortraits_, profileCharacterUi_, profileUi_;
  std::optional<GpdaArchive> mainUi_;
  std::optional<GpdaArchive> battleUi_;
  struct FrontendAssets;
  std::shared_ptr<const FrontendAssets> frontend_;
  std::optional<GpdaArchive> battleFaces_;
  std::optional<GpdaArchive> effects_;
  std::map<ImageSection, GpdaArchive> images_;
};

// Native StartBattle queues root7 group/costume/left-right face banks.
class ArchiveBattleResources final : public BattleResources {
 public:
  explicit ArchiveBattleResources(GameResources& resources):resources_(resources){}
  void Begin(const BattleEntryCommand& command) override;
  void Pump() override;
  ApproachResourceStatus Status() const override { return status_; }
  void Reset() override { request_.reset();textures_={};status_={};++generation_; }
  const std::array<std::vector<PreparedProfileTexture>,2>& Textures() const { return textures_; }
  uint64_t Generation() const { return generation_; }
  std::function<void(const PreparedProfileTexture&)> Upload;
 private:
  GameResources& resources_;
  std::optional<BattleEntryCommand> request_;
  ApproachResourceStatus status_;
  std::array<std::vector<PreparedProfileTexture>,2> textures_;
  uint64_t generation_=0;
};

class ArchiveApproachResources final : public ApproachResources {
 public:
  explicit ArchiveApproachResources(GameResources& resources) : resources_(resources) {}
  void Begin(int32_t id) override;
  ApproachResourceStatus Status() const override { return {}; }
  void Reset() override { id_.reset(); textures_.clear(); }
  std::optional<int32_t> Id() const { return id_; }
  const std::vector<PreparedProfileTexture>& Textures() const { return textures_; }
 private:
  GameResources& resources_;
  std::optional<int32_t> id_;
  std::vector<PreparedProfileTexture> textures_;
};
}  // namespace Impacto::Saekano
