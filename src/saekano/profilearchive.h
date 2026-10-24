#pragma once

#include "profilemode.h"
#include "resources.h"
#include "audio.h"
#include "textreveal.h"
#include "profilemouth.h"
#include "profilevoiceoutput.h"

namespace Impacto::Saekano {
// Deterministic headless consumer: real GXT/HCA/envelope preparation. It does
// no physical audio output, font drawing or invented lip synchronization.
class ArchiveProfileServices final : public ProfileServices {
 public:
  ArchiveProfileServices(GameResources& resources, ScriptNameIndex scripts,
                         TextFormatEnvironment format,
                         TextTimingEnvironment timing,
                         ProfileVoiceOutput* voiceOutput = nullptr);
  ArchiveProfileServices(const ArchiveProfileServices&) = delete;
  ArchiveProfileServices& operator=(const ArchiveProfileServices&) = delete;
  void BeginMenu(int32_t scene, uint8_t textSpeed) override;
  bool MenuReady() const override { return menuReady_; }
  const ProfileScene& Scene() const override { return scene_; }
  const ScriptNameIndex& Scripts() const override { return scripts_; }
  bool Update(std::u16string_view text, uint64_t now) override;
  void Reveal(uint64_t now) override { resultText_.Reveal(now); }
  ProfileResponseInput ResponseStatus(uint64_t now) override;
  void Act(ProfileResponseAction action, const ProfileResponseState& response,
           uint64_t now) override;
  const VoiceTextReveal& ResultText() const { return resultText_; }
  const VoiceTextReveal& ResponseText() const { return responseText_; }
  const ProfileMouthPlayback& Mouth() const { return mouth_; }
  std::optional<uint64_t> VoiceFrames() const {
    return voice_->FramesConsumed();
  }
  size_t MenuTextureCount() const { return menu_.size(); }
  const std::vector<PreparedProfileTexture>& MenuTextures() const {
    return menu_;
  }
  const PreparedProfileTexture* MenuTexture(ProfileTextureKey key) const;
  const PreparedCharacterPose* ActorPixels() const {
    return actor_ ? &*actor_ : nullptr;
  }
  const ImagePixels* BackgroundPixels() const {
    return background_ ? &*background_ : nullptr;
  }

 private:
  GameResources& resources_;
  ScriptNameIndex scripts_;
  TextFormatEnvironment format_;
  TextTimingEnvironment timing_;
  ProfileScene scene_;
  std::vector<PreparedProfileTexture> menu_;
  std::optional<PreparedCharacterPose> actor_;
  std::optional<ImagePixels> background_;
  std::optional<ProfileVoiceAssets> responseAssets_;
  ClockedProfileVoice clockedVoice_;
  ProfileVoiceOutput* voice_;
  VoiceTextReveal resultText_, responseText_;
  ProfileMouthPlayback mouth_;
  uint8_t textSpeed_ = 0;
  bool menuReady_ = false, responseReady_ = false;
};
}  // namespace Impacto::Saekano
