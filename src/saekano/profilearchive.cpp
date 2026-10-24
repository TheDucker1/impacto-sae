#include "profilearchive.h"

#include <algorithm>

namespace Impacto::Saekano {
ArchiveProfileServices::ArchiveProfileServices(GameResources& resources,
                                               ScriptNameIndex scripts,
                                               TextFormatEnvironment format,
                                               TextTimingEnvironment timing,
                                               ProfileVoiceOutput* voiceOutput)
    : resources_(resources),
      scripts_(std::move(scripts)),
      format_(std::move(format)),
      timing_(std::move(timing)),
      voice_(voiceOutput ? voiceOutput : &clockedVoice_) {}
void ArchiveProfileServices::BeginMenu(int32_t scene, uint8_t textSpeed) {
  if (scene < 0) throw FormatError("negative profile scene");
  // Validate the option before loading textures. Never default captured inputs.
  (void)timing_.ForCharacters(textSpeed);
  (void)timing_.ForVoice(0, textSpeed);
  menuReady_ = responseReady_ = false;
  actor_.reset();
  background_.reset();
  responseAssets_.reset();
  voice_->Reset();
  mouth_ = {};
  resultText_ = {};
  responseText_ = {};
  menu_.clear();
  scene_ = resources_.LoadProfileScene(size_t(scene));
  for (const auto& asset : resources_.LoadProfileMenu())
    menu_.push_back({asset.Key, asset.Name, DecodeSceneImage(asset.Data)});
  textSpeed_ = textSpeed;
  menuReady_ = true;
}
const PreparedProfileTexture* ArchiveProfileServices::MenuTexture(
    ProfileTextureKey key) const {
  const auto found = std::find_if(menu_.begin(), menu_.end(),
                                  [key](const PreparedProfileTexture& texture) {
                                    return texture.Key == key;
                                  });
  return found == menu_.end() ? nullptr : &*found;
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
  voice_->Update(now);
  return {responseReady_, responseText_.Revealed, false, voice_->Stopped()};
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
      voice_->Prepare(std::move(responseAssets_->Hca));
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
      voice_->Start(now);
      mouth_.Begin(responseAssets_->Mouth, now);
      break;
    case ProfileResponseAction::UpdatePresentation:
      mouth_.Update(now);
      break;
    case ProfileResponseAction::RevealText:
      responseText_.Reveal(now);
      break;
    case ProfileResponseAction::StopVoice:
      voice_->Stop();
      break;
    case ProfileResponseAction::StopBgm:
      throw FormatError("owning profile must route BGM stop through the host");
  }
}
}  // namespace Impacto::Saekano
