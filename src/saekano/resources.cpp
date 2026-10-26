#include "resources.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <limits>
#include <set>

namespace Impacto::Saekano {
std::vector<ScriptVoiceEnvelope> DecodeScriptVoiceEnvelopes(
    std::span<const int32_t> voiceIds, Bytes envelopeArchive) {
  GpdaArchive archive(std::move(envelopeArchive));
  std::set<int32_t> seen;
  std::vector<ScriptVoiceEnvelope> result;
  for (auto id : voiceIds) {
    if (id < 0) continue;
    if (id > INT16_MAX) throw FormatError("script voice ID exceeds verified signed16 range");
    if (!seen.insert(id).second) continue;
    const size_t index = result.size();
    if (index >= archive.Entries().size())
      throw FormatError("script voice envelope archive is missing an entry");
    if (archive.Entries()[index].Type != 0)
      throw FormatError("unsupported script voice envelope entry type");
    result.push_back({id, archive.Entries()[index].Name,
                       DecodeProfileMouth(archive.Data(index))});
  }
  return result;
}
namespace {
Bytes RawEntry(const GpdaArchive& archive, size_t index) {
  if (archive.Entries().at(index).Type != 0)
    throw FormatError("expected uncompressed nested GPDA archive");
  return archive.Data(index);
}

Bytes CompressedEntry(const GpdaArchive& archive, size_t index, uint32_t expectedType) {
  if (archive.Entries().at(index).Type != expectedType)
    throw FormatError("unsupported GPDA resource entry type");
  return InflateGzip(archive.Data(index));
}

// Avoid retaining the complete 66 MB outer archive after taking its sections.
GpdaArchive ResourceSection(const GpdaArchive& root, size_t index) {
  return GpdaArchive(RawEntry(root, index));
}
}  // namespace

Bytes InflateGzip(std::span<const uint8_t> input, size_t outputLimit) {
  if (input.size() > std::numeric_limits<uInt>::max())
    throw FormatError("gzip input exceeds decoder range");
  z_stream stream{};
  if (inflateInit2(&stream, 15 + 16) != Z_OK)
    throw FormatError("cannot initialize gzip decoder");
  struct EndInflate {
    z_stream& Stream;
    ~EndInflate() { inflateEnd(&Stream); }
  } cleanup{stream};
  stream.next_in = const_cast<Bytef*>(input.data());
  stream.avail_in = static_cast<uInt>(input.size());
  Bytes result;
  std::array<uint8_t, 65536> block;
  for (;;) {
    stream.next_out = block.data();
    stream.avail_out = static_cast<uInt>(block.size());
    const auto before = stream.avail_in;
    const int status = inflate(&stream, Z_NO_FLUSH);
    const size_t produced = block.size() - stream.avail_out;
    if (produced > outputLimit - result.size())
      throw FormatError("gzip output exceeds resource budget");
    result.insert(result.end(), block.begin(), block.begin() + produced);
    if (status == Z_STREAM_END) {
      if (stream.avail_in != 0)
        throw FormatError("trailing data after gzip member");
      return result;
    }
    if (status != Z_OK || (produced == 0 && before == stream.avail_in))
      throw FormatError("invalid or truncated gzip resource");
  }
}

GameResources::GameResources(Bytes resourceDat)
    : GameResources(GpdaArchive(std::move(resourceDat))) {}

GameResources::GameResources(const GpdaArchive& root)
    : scripts_(ResourceSection(root, 4)), live2d_(ResourceSection(root, 0)) {
  if (root.Entries().size() > 1) mainUi_.emplace(ResourceSection(root, 1));
  if (root.Entries().size() > 6)
    characterPoses_.emplace(ResourceSection(root, 6));
  if (root.Entries().size() > 3) {
    const auto profile = ResourceSection(root, 3);
    if (!profile.Entries().empty())
      profilePortraits_.emplace(RawEntry(profile, 0));
    if (profile.Entries().size() > 3) {
      profileCharacterUi_.emplace(RawEntry(profile, 2));
      profileUi_.emplace(RawEntry(profile, 3));
    }
    if (profile.Entries().size() > 1)
      profileScenes_.emplace(RawEntry(profile, 1));
    if (profile.Entries().size() > 4)
      profileVoices_.emplace(RawEntry(profile, 4));
  }
  for (auto section : {ImageSection::Background, ImageSection::Event,
                       ImageSection::Free, ImageSection::Rule})
    if (size_t(section) < root.Entries().size())
      images_.emplace(section, ResourceSection(root, size_t(section)));
}

SharedUiAssets::SharedUiAssets(Bytes firstDat)
    : archive_(ResourceSection(GpdaArchive(std::move(firstDat)), 1)) {}
SystemSoundAssets::SystemSoundAssets(Bytes firstDat)
    : archive_(ResourceSection(GpdaArchive(std::move(firstDat)), 7)) {}
ResourceAsset SystemSoundAssets::Load(size_t entry) const {
  if (entry >= Count())
    throw FormatError("UI sound ID outside original archive");
  return {archive_.Entries()[entry].Name, RawEntry(archive_, entry)};
}
ResourceAsset SharedUiAssets::Load(size_t entry) const {
  if (entry >= Count())
    throw FormatError("shared UI entry outside original archive");
  return {archive_.Entries()[entry].Name, CompressedEntry(archive_, entry, 1)};
}
size_t GameResources::ProfileSceneCount() const {
  return profileScenes_ ? profileScenes_->Entries().size() : 0;
}
ProfileScene GameResources::LoadProfileScene(size_t sceneId) const {
  if (sceneId >= ProfileSceneCount())
    throw FormatError("profile scene ID outside original archive");
  return DecodeProfileScene(RawEntry(*profileScenes_, sceneId));
}
std::vector<ProfileMenuAsset> GameResources::LoadProfileMenu() const {
  if (!profilePortraits_ || !profileCharacterUi_ || !profileUi_)
    throw FormatError("profile menu sections are missing");
  std::vector<ProfileMenuAsset> result;
  const auto append = [&](const GpdaArchive& archive, ProfileTextureSection section,
                           int32_t character) {
    if (archive.Entries().size() > 256)
      throw FormatError("profile UI texture count exceeds budget");
    for (size_t i = 0; i < archive.Entries().size(); ++i)
      result.push_back({{section, character, uint32_t(i)}, archive.Entries()[i].Name,
                        CompressedEntry(archive, i, 1)});
  };
  append(*profilePortraits_, ProfileTextureSection::Portraits, -1);
  append(*profileUi_, ProfileTextureSection::Interface, -1);
  if (profileCharacterUi_->Entries().size() != 4)
    throw FormatError("profile UI requires four character packs");
  for (size_t i = 0; i < 4; ++i)
    append(GpdaArchive(RawEntry(*profileCharacterUi_, i)), ProfileTextureSection::Character, int32_t(i));
  return result;
}
ProfileMenuAsset GameResources::LoadProfileTexture(ProfileTextureKey key) const {
  const auto load = [&](const GpdaArchive& archive) {
    if (key.Entry >= archive.Entries().size())
      throw FormatError("profile texture entry outside original archive");
    return ProfileMenuAsset{key, archive.Entries()[key.Entry].Name,
                            CompressedEntry(archive, key.Entry, 1)};
  };
  if (key.Section == ProfileTextureSection::Character) {
    if (!profileCharacterUi_ || key.Character < 0 || key.Character >= 4)
      throw FormatError("profile character texture pack outside original archive");
    return load(GpdaArchive(RawEntry(*profileCharacterUi_, size_t(key.Character))));
  }
  if (key.Character != -1) throw FormatError("common profile texture has a character pack");
  if (key.Section == ProfileTextureSection::Portraits && profilePortraits_)
    return load(*profilePortraits_);
  if (key.Section == ProfileTextureSection::Interface && profileUi_)
    return load(*profileUi_);
  if (key.Section == ProfileTextureSection::MainUi && mainUi_)
    return load(*mainUi_);
  throw FormatError("profile texture section is unavailable");
}
ProfileVoiceAssets GameResources::LoadProfileVoice(size_t characterId,
                                                   size_t entry) const {
  if (!profileVoices_ || characterId >= 4)
    throw FormatError("profile voice character outside original archive");
  const auto load = [&](size_t section) {
    const GpdaArchive characters(RawEntry(*profileVoices_, section));
    if (characterId >= characters.Entries().size())
      throw FormatError("profile voice character archive is missing");
    const GpdaArchive waves(RawEntry(characters, characterId));
    if (entry >= waves.Entries().size())
      throw FormatError("profile voice entry outside original archive");
    return CompressedEntry(waves, entry, 1);
  };
  return {load(0), DecodeProfileMouth(load(1))};
}

CharacterPoseAssets DecodeCharacterPoseArchive(Bytes bytes) {
  GpdaArchive archive(std::move(bytes));
  if (archive.Entries().size() != 17)
    throw FormatError("unsupported character pose archive shape");
  CharacterPoseAssets result;
  auto geometry = [&](size_t index, std::span<int32_t> words) {
    const auto text = RawEntry(archive, index);
    if (text.empty()) throw FormatError("empty character pose geometry");
    const char* cursor = reinterpret_cast<const char*>(text.data());
    const char* end = cursor + text.size();
    for (auto& word : words) {
      while (cursor != end && (*cursor == ' ' || *cursor == '\r' ||
                              *cursor == '\n' || *cursor == '\t'))
        ++cursor;
      bool negative = cursor != end && *cursor == '-';
      if (cursor != end && (*cursor == '+' || *cursor == '-')) ++cursor;
      uint32_t number = 0;
      const auto parsed = std::from_chars(cursor, end, number);
      if (parsed.ec != std::errc{} || parsed.ptr == end || *parsed.ptr != ',')
        throw FormatError("invalid character pose geometry field");
      word = std::bit_cast<int32_t>(negative ? 0u - number : number);
      cursor = parsed.ptr + 1;
    }
    // Native consumes only the four/eight comma-terminated values, not a
    // null-terminated filename or an invented record count.
  };
  geometry(0, std::span(result.Geometry).first(4));
  geometry(1, std::span(result.Geometry).subspan(4));
  for (size_t i = 0; i < result.Textures.size(); ++i)
    result.Textures[i] = {archive.Entries()[i + 2].Name, RawEntry(archive, i + 2)};
  return result;
}

size_t GameResources::CharacterPoseCount() const {
  return characterPoses_ ? characterPoses_->Entries().size() : 0;
}
CharacterPoseAssets GameResources::LoadCharacterPose(size_t poseId) const {
  if (poseId >= CharacterPoseCount())
    throw FormatError("character pose ID outside original archive");
  // Root6 is a type0 gzip member containing an in-memory GPDA, unlike scene
  // images' type1 GXT members. Callback800070c6 inflates before parser80006972.
  auto result = DecodeCharacterPoseArchive(CompressedEntry(*characterPoses_, poseId, 0));
  result.Name = characterPoses_->Entries()[poseId].Name;
  return result;
}

size_t GameResources::ImageCount(ImageSection section) const {
  const auto found = images_.find(section);
  return found == images_.end() ? 0 : found->second.Entries().size();
}
ResourceAsset GameResources::LoadImage(ImageSection section, size_t imageId) const {
  if (imageId >= ImageCount(section)) throw FormatError("scene image ID outside original archive");
  const auto& archive = images_.at(section);
  return {archive.Entries()[imageId].Name, CompressedEntry(archive, imageId, 1)};
}

Bytes GameResources::Load(int32_t scriptId) {
  if (scriptId < 0 || size_t(scriptId) >= ScriptCount())
    throw FormatError("script ID outside resource archive");
  // script.dat -> numbered script archive -> object archive -> gzip object.
  GpdaArchive script(RawEntry(scripts_, size_t(scriptId)));
  GpdaArchive object(RawEntry(script, 0));
  // Script objects are gzip bytes with type 0; model leaves use type 1.
  // The native script loader inflates explicitly rather than testing that word.
  return CompressedEntry(object, 0, 0);
}
std::vector<ScriptVoiceEnvelope> GameResources::LoadScriptVoiceEnvelopes(
    int32_t scriptId) const {
  if (scriptId < 0 || size_t(scriptId) >= ScriptCount())
    throw FormatError("script ID outside resource archive");
  const GpdaArchive script(RawEntry(scripts_, size_t(scriptId)));
  const GpdaArchive object(RawEntry(script, 0));
  const ScriptObject commands(CompressedEntry(object, 0, 0));
  std::vector<int32_t> voiceIds;
  for (size_t i = 0; i < commands.Records().size(); ++i)
    if (commands.Records()[i].Opcode == 0x64)
      voiceIds.push_back(DecodeMessage(commands.Payload(i)).VoiceId);
  return DecodeScriptVoiceEnvelopes(voiceIds, CompressedEntry(object, 1, 0));
}

Live2dModelAssets GameResources::LoadLive2d(size_t modelId) const {
  if (modelId >= Live2dCount())
    throw FormatError("Live2D model ID outside resource archive");
  GpdaArchive model(RawEntry(live2d_, modelId));
  Live2dModelAssets result;
  result.Name = live2d_.Entries()[modelId].Name;
  if (model.Entries().size() != result.Sections.size())
    throw FormatError("unsupported Live2D section layout");
  for (size_t index = 0; index < result.Sections.size(); ++index) {
    GpdaArchive section(RawEntry(model, index));
    auto& assets = result.Sections[index];
    assets.reserve(section.Entries().size());
    for (size_t asset = 0; asset < section.Entries().size(); ++asset)
      assets.push_back({section.Entries()[asset].Name,
                        CompressedEntry(section, asset, 1)});
  }
  return result;
}

}  // namespace Impacto::Saekano
