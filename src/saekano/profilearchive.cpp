#include "profilearchive.h"

#include <algorithm>

namespace Impacto::Saekano {
ArchiveProfileServices::ArchiveProfileServices(GameResources& resources,
                                               ScriptNameIndex scripts,
                                               TextFormatEnvironment format,
                                               TextTimingEnvironment timing)
    : resources_(resources),
      scripts_(std::move(scripts)),
      format_(std::move(format)),
      timing_(std::move(timing)) {}
void ArchiveProfileServices::BeginMenu(int32_t scene, uint8_t textSpeed) {
  if (scene < 0) throw FormatError("negative profile scene");
  // Validate the option before loading textures. Never default captured inputs.
  (void)timing_.ForCharacters(textSpeed);
  (void)timing_.ForVoice(0, textSpeed);
  menuReady_ = responseReady_ = playing_ = false;
  actor_.reset();
  background_.reset();
  responseAssets_.reset();
  voice_.reset();
  mouth_ = {};
  resultText_ = {};
  responseText_ = {};
  menu_.clear();
  scene_ = resources_.LoadProfileScene(size_t(scene));
  for (const auto& asset : resources_.LoadProfileMenu())
    menu_.push_back(DecodeSceneImage(asset.Data));
  textSpeed_ = textSpeed;
  menuReady_ = true;
}
bool ArchiveProfileServices::Update(std::u16string_view text, uint64_t now) {
  if (!resultText_.Prepared) {
    resultText_.PrepareUtf16(text, format_, now, true);
    resultText_.StartCharacters(timing_.ForCharacters(textSpeed_), now);
  }
  resultText_.Update(now);
  return resultText_.Revealed;
}
ProfileResponseInput ArchiveProfileServices::ResponseStatus(uint64_t now) {
  if (responseText_.Started) responseText_.Update(now);
  if (playing_) {
    if (!voice_ || now < voiceEpoch_)
      throw FormatError("invalid profile voice clock");
    const auto& info = voice_->Info();
    const uint64_t elapsed = now - voiceEpoch_;
    const uint64_t end =
        elapsed / 1000 >= (info.Frames + info.SampleRate - 1) / info.SampleRate
            ? info.Frames
            : std::min(info.Frames, elapsed * info.SampleRate / 1000);
    std::array<int16_t, 8192> block;
    if (info.Channels == 0 || info.Channels > block.size())
      throw FormatError("profile voice channel count exceeds PCM budget");
    while (voice_->Position() < end) {
      const size_t count = size_t(std::min<uint64_t>(
          end - voice_->Position(), block.size() / info.Channels));
      if (!voice_->Read(std::span(block).first(count * info.Channels)))
        throw FormatError("profile voice ended before declared PCM length");
    }
    if (voice_->Position() == info.Frames) playing_ = false;
  }
  return {responseReady_, responseText_.Revealed, false,
          bool(voice_) && !playing_};
}
void ArchiveProfileServices::Act(ProfileResponseAction action,
                                 const ProfileResponseState& response,
                                 uint64_t now) {
  switch (action) {
    case ProfileResponseAction::BeginResources: {
      responseReady_ = false;
      auto pose =
          resources_.LoadCharacterPose(size_t(response.Event.Presentation[0]));
      PreparedCharacterPose prepared;
      prepared.Pose = response.Event.Presentation[0];
      prepared.Geometry = pose.Geometry;
      for (size_t i = 0; i < prepared.Textures.size(); ++i)
        prepared.Textures[i] = DecodeSceneImage(pose.Textures[i].Data);
      actor_ = std::move(prepared);
      background_.reset();
      if (response.Background != -1)
        background_ =
            DecodeSceneImage(resources_
                                 .LoadImage(ImageSection::Background,
                                            size_t(response.Background))
                                 .Data);
      responseAssets_ = resources_.LoadProfileVoice(
          size_t(response.CharacterId), size_t(response.Event.Tail));
      responseText_.Prepare(response.Event.Response, format_, now);
      responseReady_ = true;
      break;
    }
    case ProfileResponseAction::PreparePresentation:
      if (!responseReady_ || !actor_)
        throw FormatError("profile actor is not ready");
      break;  // Typed pose/face/X700 descriptor remains in the owning state.
    case ProfileResponseAction::PrepareVoice:
      if (!responseAssets_)
        throw FormatError("profile voice resource is not ready");
      voice_ = std::make_unique<HcaWaveReader>(std::move(responseAssets_->Hca));
      playing_ = false;
      break;
    case ProfileResponseAction::BeginTextReveal:
      if (!responseAssets_)
        throw FormatError("profile mouth duration is not ready");
      responseText_.Start(
          timing_.ForVoice(uint32_t(responseAssets_->Mouth.Duration),
                           textSpeed_),
          now);
      break;
    case ProfileResponseAction::StartVoice:
      if (!voice_) throw FormatError("profile voice was not prepared");
      voiceEpoch_ = now;
      playing_ = true;
      mouth_.Begin(responseAssets_->Mouth, now);
      break;
    case ProfileResponseAction::UpdatePresentation:
      mouth_.Update(now);
      break;
    case ProfileResponseAction::RevealText:
      responseText_.Reveal(now);
      break;
    case ProfileResponseAction::StopVoice:
      if (!voice_)
        throw FormatError("profile voice has no stop acknowledgement");
      playing_ = false;
      break;
    case ProfileResponseAction::StopBgm:
      throw FormatError("owning profile must route BGM stop through the host");
  }
}
}  // namespace Impacto::Saekano
